/**
 * Lab 09 — Exercise solution: lock per call vs. thread-local counting
 *
 * Exercise (Chapter 9): measure the claim behind §9.6. Count every
 * method entry two ways and time the whole run (VMInit -> VMDeath):
 *
 *   mode=lock  RawMonitorEnter / count++ / RawMonitorExit on EVERY entry
 *   mode=tls   count in thread-local storage, merge once per thread at
 *              ThreadEnd (lab 9's approach)
 *
 * Both count the same events (totals vary a little from run to run, as
 * the JIT and the JDK do slightly different work); the time is what
 * differs. Run each mode a
 * few times — timings are noisy — and with more threads in the test app
 * to see contention grow.
 *
 * Run:  java -agentpath:./build/libcounter_bench.so=mode=lock AdvancedTestApp
 *       java -agentpath:./build/libcounter_bench.so=mode=tls  AdvancedTestApp
 */

#include "jvmti_utils.h"
#include <stdlib.h>

static int use_tls = 0;
static jrawMonitorID lock;
static long long total = 0;
static jlong start_ns = 0;

static long long *tls_counter(jvmtiEnv *jvmti, jthread thread) {
    long long *c = NULL;
    if ((*jvmti)->GetThreadLocalStorage(jvmti, thread, (void**)&c) == JVMTI_ERROR_NONE && !c) {
        c = calloc(1, sizeof(long long));
        if (c) (*jvmti)->SetThreadLocalStorage(jvmti, thread, c);
    }
    return c;
}

static void JNICALL cbMethodEntry(jvmtiEnv *jvmti, JNIEnv *jni,
                                  jthread thread, jmethodID method) {
    if (use_tls) {
        long long *c = tls_counter(jvmti, thread);
        if (c) (*c)++;                       /* no lock */
    } else {
        (*jvmti)->RawMonitorEnter(jvmti, lock);   /* a lock on every call */
        total++;
        (*jvmti)->RawMonitorExit(jvmti, lock);
    }
}

static void merge(jvmtiEnv *jvmti, jthread thread) {
    long long *c = NULL;
    if ((*jvmti)->GetThreadLocalStorage(jvmti, thread, (void**)&c) != JVMTI_ERROR_NONE || !c)
        return;
    (*jvmti)->RawMonitorEnter(jvmti, lock);   /* once per thread */
    total += *c;
    (*jvmti)->RawMonitorExit(jvmti, lock);
    (*jvmti)->SetThreadLocalStorage(jvmti, thread, NULL);
    free(c);
}

static void JNICALL cbThreadEnd(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread) {
    if (use_tls) merge(jvmti, thread);
}

static void JNICALL cbVMInit(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread) {
    (*jvmti)->GetTime(jvmti, &start_ns);
}

static void JNICALL cbVMDeath(jvmtiEnv *jvmti, JNIEnv *jni) {
    jlong end_ns = 0;
    (*jvmti)->GetTime(jvmti, &end_ns);
    /* Threads still alive now (daemon threads, and the thread running
       VMDeath) never reached ThreadEnd: fold in their counts too, or the
       tls total would come out short */
    if (use_tls) {
        jint count = 0;
        jthread *threads = NULL;
        if ((*jvmti)->GetAllThreads(jvmti, &count, &threads) == JVMTI_ERROR_NONE) {
            for (jint i = 0; i < count; i++) merge(jvmti, threads[i]);
            (*jvmti)->Deallocate(jvmti, (unsigned char*)threads);
        }
    }
    (*jvmti)->RawMonitorEnter(jvmti, lock);
    printf("[Bench] mode=%s: %lld method entries in %.1f ms\n",
           use_tls ? "tls" : "lock", total, (double)(end_ns - start_ns) / 1e6);
    (*jvmti)->RawMonitorExit(jvmti, lock);
    fflush(stdout);
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    if (options && strcmp(options, "mode=tls") == 0) use_tls = 1;
    else if (!options || strcmp(options, "mode=lock") != 0) {
        fprintf(stderr, "[Bench] usage: -agentpath:...=mode=lock|mode=tls\n");
        return JNI_ERR;
    }

    jvmtiEnv *jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;
    jvmtiError err = (*jvmti)->CreateRawMonitor(jvmti, "bench_lock", &lock);
    CHECK_JVMTI_ERROR(jvmti, err, "CreateRawMonitor");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_generate_method_entry_events = 1;
    err = (*jvmti)->AddCapabilities(jvmti, &caps);
    CHECK_JVMTI_ERROR(jvmti, err, "AddCapabilities");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.MethodEntry = &cbMethodEntry;
    callbacks.ThreadEnd   = &cbThreadEnd;
    callbacks.VMInit      = &cbVMInit;
    callbacks.VMDeath     = &cbVMDeath;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    const jvmtiEvent events[] = {
        JVMTI_EVENT_METHOD_ENTRY, JVMTI_EVENT_THREAD_END,
        JVMTI_EVENT_VM_INIT, JVMTI_EVENT_VM_DEATH,
    };
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
        err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, events[i], NULL);
        CHECK_JVMTI_ERROR(jvmti, err, "SetEventNotificationMode");
        if (err != JVMTI_ERROR_NONE) return JNI_ERR;
    }
    return JNI_OK;
}
