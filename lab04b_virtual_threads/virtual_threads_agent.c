/**
 * Lab 04b — Virtual Threads (JDK 21+)
 *
 * §4.7 example. Shows what changes for an agent once an application
 * uses virtual threads, by observing 11,000 of them:
 *
 *   - ThreadStart/ThreadEnd do NOT fire for virtual threads; the
 *     dedicated VirtualThreadStart/VirtualThreadEnd events do, once the
 *     agent holds can_support_virtual_threads (JVMTI_VERSION_21)
 *   - GetAllThreads returns platform threads only — including the
 *     carrier threads virtual threads run on, but never the virtual
 *     threads themselves. The thread-dump agent of lab 4 cannot see them
 *   - GetThreadState and GetStackTrace DO work on a virtual thread's
 *     jthread, even while it's unmounted (parked, not on any carrier)
 *   - Per-thread bookkeeping must be cheap: the agent keeps counters and
 *     a few sample threads, not state per virtual thread
 *
 * Requires JDK 21 or newer, at build time (jvmti.h) and run time.
 *
 * Build:
 *   mkdir build && cd build && cmake .. && cmake --build .
 *
 * Run:
 *   javac VirtualThreadTestApp.java
 *   java -agentpath:./build/libvirtual_threads_agent.so VirtualThreadTestApp
 */

#include "jvmti_utils.h"

#define SAMPLE_PREFIX "parked-"   /* sample threads whose name starts with this */
#define MAX_SAMPLES   3
#define MAX_FRAMES    16

static jrawMonitorID lock;
static long platform_starts = 0;   /* ThreadStart events */
static long virtual_starts  = 0;   /* VirtualThreadStart events */
static long virtual_ends    = 0;   /* VirtualThreadEnd events */

/* A few virtual threads kept (as global references) for inspection at
   VMDeath. Keeping one reference per virtual thread would not scale. */
static jthread samples[MAX_SAMPLES];
static int     sample_count = 0;

/* ------------------------------------------------------------------ */
/*  Events                                                             */
/* ------------------------------------------------------------------ */

static void JNICALL cbThreadStart(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread) {
    (*jvmti)->RawMonitorEnter(jvmti, lock);
    platform_starts++;
    (*jvmti)->RawMonitorExit(jvmti, lock);
}

/* Runs on the new virtual thread itself, as it starts */
static void JNICALL cbVirtualThreadStart(jvmtiEnv *jvmti, JNIEnv *jni,
                                         jthread vthread) {
    int want_sample = 0;
    (*jvmti)->RawMonitorEnter(jvmti, lock);
    virtual_starts++;
    want_sample = sample_count < MAX_SAMPLES;
    (*jvmti)->RawMonitorExit(jvmti, lock);
    if (!want_sample) return;

    jvmtiThreadInfo info;
    memset(&info, 0, sizeof(info));
    if ((*jvmti)->GetThreadInfo(jvmti, vthread, &info) != JVMTI_ERROR_NONE) return;
    if (info.name && strncmp(info.name, SAMPLE_PREFIX, strlen(SAMPLE_PREFIX)) == 0) {
        jthread ref = (*jni)->NewGlobalRef(jni, vthread);  /* outlives this callback */
        (*jvmti)->RawMonitorEnter(jvmti, lock);
        if (sample_count < MAX_SAMPLES) samples[sample_count++] = ref;
        else (*jni)->DeleteGlobalRef(jni, ref);
        (*jvmti)->RawMonitorExit(jvmti, lock);
    }
    if (info.name) (*jvmti)->Deallocate(jvmti, (unsigned char*)info.name);
}

static void JNICALL cbVirtualThreadEnd(jvmtiEnv *jvmti, JNIEnv *jni,
                                       jthread vthread) {
    (*jvmti)->RawMonitorEnter(jvmti, lock);
    virtual_ends++;
    (*jvmti)->RawMonitorExit(jvmti, lock);
}

/* ------------------------------------------------------------------ */
/*  Report                                                             */
/* ------------------------------------------------------------------ */

static const char *state_name(jint state) {
    if (!(state & JVMTI_THREAD_STATE_ALIVE)) return "TERMINATED";
    if (state & JVMTI_THREAD_STATE_RUNNABLE) return "RUNNABLE";
    if (state & JVMTI_THREAD_STATE_BLOCKED_ON_MONITOR_ENTER) return "BLOCKED";
    if (state & JVMTI_THREAD_STATE_WAITING_WITH_TIMEOUT) return "TIMED_WAITING";
    if (state & JVMTI_THREAD_STATE_WAITING) return "WAITING";
    return "UNKNOWN";
}

static void print_stack(jvmtiEnv *jvmti, jthread thread) {
    jvmtiFrameInfo frames[MAX_FRAMES];
    jint count = 0;
    jvmtiError err = (*jvmti)->GetStackTrace(jvmti, thread, 0, MAX_FRAMES,
                                             frames, &count);
    if (err != JVMTI_ERROR_NONE) {
        CHECK_JVMTI_ERROR(jvmti, err, "GetStackTrace");
        return;
    }
    for (jint i = 0; i < count; i++) {
        char *name = NULL, *csig = NULL;
        jclass cls;
        (*jvmti)->GetMethodName(jvmti, frames[i].method, &name, NULL, NULL);
        if ((*jvmti)->GetMethodDeclaringClass(jvmti, frames[i].method, &cls) == JVMTI_ERROR_NONE)
            (*jvmti)->GetClassSignature(jvmti, cls, &csig, NULL);
        printf("      at %s.%s\n", csig ? csig : "?", name ? name : "?");
        if (name) (*jvmti)->Deallocate(jvmti, (unsigned char*)name);
        if (csig) (*jvmti)->Deallocate(jvmti, (unsigned char*)csig);
    }
}

static void JNICALL cbVMDeath(jvmtiEnv *jvmti, JNIEnv *jni) {
    (*jvmti)->RawMonitorEnter(jvmti, lock);
    long p_starts = platform_starts, v_starts = virtual_starts, v_ends = virtual_ends;
    (*jvmti)->RawMonitorExit(jvmti, lock);

    printf("\n[VThreads] === Report at VMDeath ===\n");
    printf("[VThreads] ThreadStart events:        %ld   (platform threads only)\n", p_starts);
    printf("[VThreads] VirtualThreadStart events: %ld\n", v_starts);
    printf("[VThreads] VirtualThreadEnd events:   %ld\n", v_ends);
    printf("[VThreads] Virtual threads alive:     %ld\n", v_starts - v_ends);

    /* What GetAllThreads sees: platform threads, carriers included */
    jint count = 0, carriers = 0, virtuals = 0;
    jthread *threads = NULL;
    if ((*jvmti)->GetAllThreads(jvmti, &count, &threads) == JVMTI_ERROR_NONE) {
        for (jint i = 0; i < count; i++) {
            jvmtiThreadInfo info;
            memset(&info, 0, sizeof(info));
            if ((*jni)->IsVirtualThread(jni, threads[i])) virtuals++;
            if ((*jvmti)->GetThreadInfo(jvmti, threads[i], &info) == JVMTI_ERROR_NONE && info.name) {
                if (strncmp(info.name, "ForkJoinPool-", 13) == 0) carriers++;
                (*jvmti)->Deallocate(jvmti, (unsigned char*)info.name);
            }
        }
        (*jvmti)->Deallocate(jvmti, (unsigned char*)threads);
    }
    printf("[VThreads] GetAllThreads returned:    %d threads: %d virtual, %d carrier (ForkJoinPool-*)\n",
           (int)count, (int)virtuals, (int)carriers);

    /* The sampled virtual threads: still alive, parked, not on a carrier */
    for (int s = 0; s < sample_count; s++) {
        jvmtiThreadInfo info;
        jint state = 0;
        memset(&info, 0, sizeof(info));
        (*jvmti)->GetThreadInfo(jvmti, samples[s], &info);
        (*jvmti)->GetThreadState(jvmti, samples[s], &state);
        printf("[VThreads] Sample \"%s\": virtual=%s state=%s\n",
               info.name ? info.name : "?",
               (*jni)->IsVirtualThread(jni, samples[s]) ? "yes" : "no",
               state_name(state));
        if (s == 0) print_stack(jvmti, samples[s]);   /* they're all alike */
        if (info.name) (*jvmti)->Deallocate(jvmti, (unsigned char*)info.name);
    }
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/*  Agent Lifecycle                                                    */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    /* The virtual thread events and capability are part of JVMTI 21 */
    jvmtiEnv *jvmti = NULL;
    if ((*vm)->GetEnv(vm, (void**)&jvmti, JVMTI_VERSION_21) != JNI_OK || !jvmti) {
        fprintf(stderr, "[VThreads] This agent needs JDK 21 or newer (JVMTI_VERSION_21)\n");
        return JNI_ERR;
    }

    jvmtiError err = (*jvmti)->CreateRawMonitor(jvmti, "vthreads_lock", &lock);
    CHECK_JVMTI_ERROR(jvmti, err, "CreateRawMonitor");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_support_virtual_threads = 1;
    err = (*jvmti)->AddCapabilities(jvmti, &caps);
    CHECK_JVMTI_ERROR(jvmti, err, "AddCapabilities(can_support_virtual_threads)");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.ThreadStart        = &cbThreadStart;
    callbacks.VirtualThreadStart = &cbVirtualThreadStart;
    callbacks.VirtualThreadEnd   = &cbVirtualThreadEnd;
    callbacks.VMDeath            = &cbVMDeath;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    const jvmtiEvent events[] = {
        JVMTI_EVENT_THREAD_START, JVMTI_EVENT_VIRTUAL_THREAD_START,
        JVMTI_EVENT_VIRTUAL_THREAD_END, JVMTI_EVENT_VM_DEATH,
    };
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
        err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, events[i], NULL);
        CHECK_JVMTI_ERROR(jvmti, err, "SetEventNotificationMode");
        if (err != JVMTI_ERROR_NONE) return JNI_ERR;
    }

    printf("[VThreads] Agent loaded (Lab 04b): tracking virtual threads\n");
    return JNI_OK;
}
