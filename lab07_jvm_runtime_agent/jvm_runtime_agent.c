/**
 * Lab 07 — JVM Runtime Interaction Agent
 *
 * §7.7 "JVM Inspector" example. Demonstrates:
 *   - GetSystemProperty for reading JVM properties (§7.1)
 *   - GetAvailableProcessors, GetPhase and a live-thread count (§7.2)
 *   - GarbageCollectionStart / GarbageCollectionFinish events with
 *     GetTime-based duration measurement (§7.3)
 *   - Per-thread CPU time with GetCurrentThreadCpuTime, read in the
 *     ThreadEnd callback, which runs on the ending thread (§7.4)
 *   - GetObjectSize for measuring object footprint
 *
 * GC callbacks run in a restricted environment: no JNI, and only
 * callback-safe JVMTI functions. They therefore do bookkeeping ONLY
 * (timestamps and counters); the summary is logged at VMDeath.
 *
 * Build:
 *   mkdir build && cd build && cmake .. && cmake --build .
 *
 * Run:
 *   javac RuntimeTestApp.java
 *   java -agentpath:./build/libjvm_runtime_agent.so RuntimeTestApp
 */

#include "jvmti_utils.h"

#define LOG_FILE "jvm_runtime_agent.log"

/* GC bookkeeping — written only from GC callbacks (which the JVM
   serializes around collections), read at VMDeath */
static jlong gc_start_ns = 0;
static jlong gc_total_ns = 0;
static jlong gc_max_ns = 0;
static long  gc_count = 0;

/* Set in Agent_OnLoad if this JVM can report per-thread CPU time */
static int cpu_time_enabled = 0;

/* ------------------------------------------------------------------ */
/*  GC Event Callbacks — bookkeeping only (restricted environment)     */
/* ------------------------------------------------------------------ */

static void JNICALL cbGCStart(jvmtiEnv* jvmti) {
    (*jvmti)->GetTime(jvmti, &gc_start_ns);
}

static void JNICALL cbGCFinish(jvmtiEnv* jvmti) {
    jlong end_ns = 0;
    (*jvmti)->GetTime(jvmti, &end_ns);

    jlong duration = end_ns - gc_start_ns;
    gc_total_ns += duration;
    if (duration > gc_max_ns) gc_max_ns = duration;
    gc_count++;
}

/* ------------------------------------------------------------------ */
/*  VMInit — Print JVM Runtime Info                                    */
/* ------------------------------------------------------------------ */

static void JNICALL cbVMInit(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread) {
    char buf[512];
    jvmti_log(LOG_FILE, "[Runtime] VMInit — printing JVM runtime info...");

    /* Read system properties. JVMTI sees only the properties the VM
       itself defines (java.vm.*, java.home, java.class.path, ...) — not
       ones the class library adds later, such as os.name or
       java.version. GetSystemProperties lists exactly what's available. */
    const char* props[] = {"java.vm.name", "java.vm.version",
                           "java.vm.vendor", "java.home"};
    for (size_t i = 0; i < sizeof(props) / sizeof(props[0]); i++) {
        char* value = NULL;
        jvmtiError err = (*jvmti)->GetSystemProperty(jvmti, props[i], &value);
        if (err == JVMTI_ERROR_NONE && value) {
            snprintf(buf, sizeof(buf), "[Runtime] %s = %s", props[i], value);
            jvmti_log(LOG_FILE, buf);
            (*jvmti)->Deallocate(jvmti, (unsigned char*)value);
        }
    }

    /* Available processors */
    jint proc_count = 0;
    if ((*jvmti)->GetAvailableProcessors(jvmti, &proc_count) == JVMTI_ERROR_NONE) {
        snprintf(buf, sizeof(buf), "[Runtime] processors = %d", (int)proc_count);
        jvmti_log(LOG_FILE, buf);
    }

    /* Current phase (LIVE here — full JNI and JVMTI available) */
    jvmtiPhase phase;
    if ((*jvmti)->GetPhase(jvmti, &phase) == JVMTI_ERROR_NONE) {
        snprintf(buf, sizeof(buf), "[Runtime] phase = %s",
                 phase == JVMTI_PHASE_LIVE ? "LIVE" : "other");
        jvmti_log(LOG_FILE, buf);
    }

    /* Live thread count (only the JVM's own threads exist this early) */
    jint thread_count = 0;
    jthread *threads = NULL;
    if ((*jvmti)->GetAllThreads(jvmti, &thread_count, &threads) == JVMTI_ERROR_NONE) {
        snprintf(buf, sizeof(buf), "[Runtime] live threads = %d", (int)thread_count);
        jvmti_log(LOG_FILE, buf);
        (*jvmti)->Deallocate(jvmti, (unsigned char*)threads);
    }

    /* Demonstrate GetObjectSize on a real String instance. The size is
       *shallow*: just the String object itself — its characters live in
       a separate byte[] that GetObjectSize does not include. */
    jstring str = (*jni)->NewStringUTF(jni, "hello, JVMTI");
    if (str == NULL) {
        (*jni)->ExceptionClear(jni);   /* pending OutOfMemoryError */
    } else {
        jlong size = 0;
        jvmtiError err = (*jvmti)->GetObjectSize(jvmti, str, &size);
        if (err == JVMTI_ERROR_NONE) {
            snprintf(buf, sizeof(buf),
                     "[Runtime] Shallow size of a String instance: %lld bytes",
                     (long long)size);
            jvmti_log(LOG_FILE, buf);
        }
        (*jni)->DeleteLocalRef(jni, str);
    }
}

/* ------------------------------------------------------------------ */
/*  ThreadEnd — per-thread CPU time (§7.4)                             */
/* ------------------------------------------------------------------ */

/* ThreadEnd runs ON the ending thread, so GetCurrentThreadCpuTime
   reports exactly that thread's total CPU time. Compare it with wall
   time: a thread that mostly sleeps or waits uses little CPU. */
static void JNICALL cbThreadEnd(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread) {
    jlong cpu_ns = 0;
    if ((*jvmti)->GetCurrentThreadCpuTime(jvmti, &cpu_ns) != JVMTI_ERROR_NONE)
        return;

    jvmtiThreadInfo tinfo;
    memset(&tinfo, 0, sizeof(tinfo));
    if ((*jvmti)->GetThreadInfo(jvmti, thread, &tinfo) != JVMTI_ERROR_NONE)
        return;

    char buf[256];
    snprintf(buf, sizeof(buf), "[CPU] thread \"%s\" used %.3f ms of CPU",
             tinfo.name ? tinfo.name : "<unnamed>", cpu_ns / 1e6);
    jvmti_log(LOG_FILE, buf);
    if (tinfo.name) (*jvmti)->Deallocate(jvmti, (unsigned char*)tinfo.name);
}

/* ------------------------------------------------------------------ */
/*  VMDeath — report the GC statistics gathered by the callbacks       */
/* ------------------------------------------------------------------ */

static void JNICALL cbVMDeath(jvmtiEnv *jvmti, JNIEnv *jni) {
    char buf[256];
    if (gc_count > 0) {
        snprintf(buf, sizeof(buf),
                 "[GC] %ld collections, total %.3f ms, max %.3f ms, avg %.3f ms",
                 gc_count,
                 gc_total_ns / 1e6,
                 gc_max_ns / 1e6,
                 gc_total_ns / 1e6 / gc_count);
    } else {
        snprintf(buf, sizeof(buf), "[GC] No collections observed.");
    }
    jvmti_log(LOG_FILE, buf);
    printf("%s\n", buf);
}

/* ------------------------------------------------------------------ */
/*  Agent Lifecycle                                                    */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    printf("[Agent] jvm_runtime_agent loaded (Lab 07)\n");

    jvmtiEnv* jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;

    jvmtiError err;

    /* Request GC event capabilities, plus CPU-time support if this
       JVM offers it (degrade gracefully if not — §12.4) */
    jvmtiCapabilities potential;
    memset(&potential, 0, sizeof(potential));
    (*jvmti)->GetPotentialCapabilities(jvmti, &potential);

    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_generate_garbage_collection_events = 1;
    if (potential.can_get_current_thread_cpu_time) {
        caps.can_get_current_thread_cpu_time = 1;
        cpu_time_enabled = 1;
    }
    err = (*jvmti)->AddCapabilities(jvmti, &caps);
    CHECK_JVMTI_ERROR(jvmti, err, "AddCapabilities");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    /* Register callbacks */
    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.GarbageCollectionStart  = &cbGCStart;
    callbacks.GarbageCollectionFinish = &cbGCFinish;
    callbacks.VMInit                  = &cbVMInit;
    callbacks.VMDeath                 = &cbVMDeath;
    if (cpu_time_enabled) callbacks.ThreadEnd = &cbThreadEnd;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    /* Enable events */
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
              JVMTI_EVENT_GARBAGE_COLLECTION_START, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable GC Start");
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
              JVMTI_EVENT_GARBAGE_COLLECTION_FINISH, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable GC Finish");
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
              JVMTI_EVENT_VM_INIT, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable VMInit");
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
              JVMTI_EVENT_VM_DEATH, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable VMDeath");
    if (cpu_time_enabled) {
        err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
                  JVMTI_EVENT_THREAD_END, NULL);
        CHECK_JVMTI_ERROR(jvmti, err, "Enable ThreadEnd");
    }

    jvmti_log(LOG_FILE, "[Runtime] Agent loaded — monitoring GC and runtime.");
    return JNI_OK;
}
