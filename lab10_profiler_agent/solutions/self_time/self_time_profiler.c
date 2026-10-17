/**
 * Lab 10 — Exercise solution: top methods by self time
 *
 * Exercise (Chapter 10): a flamegraph shows *total* time — a method plus
 * everything it calls. Add a report of *self* time: how often each method
 * was the one actually running, at the top of the sampled stack. That's
 * the last frame of each collapsed-stack key, so it costs nothing extra
 * to collect: at VMDeath, sum the sample counts by leaf frame.
 *
 * This file is lab 10's profiler with one addition: print_self_time(),
 * called from cbVMDeath.
 *
 * Run:  java -agentpath:./build/libself_time_profiler.so ProfilerTestApp
 */

#include "jvmti_utils.h"
#include <stdlib.h>

/* ── Configuration ── */
#define MAX_FRAMES     64
#define MAX_SAMPLES    10000
#define SAMPLE_KEY_LEN 2048
#define INTERVAL_MS    10

/* ── Sample storage ── */
typedef struct {
    char key[SAMPLE_KEY_LEN];  /* semicolon-separated stack */
    int  count;
} StackSample;

static StackSample samples[MAX_SAMPLES];
static int sample_count = 0;
/* profiler_lock guards samples[] AND the two flags below; it also
   paces the sampler (RawMonitorWait with a timeout) and lets VMDeath
   wait for the sampler to finish (RawMonitorNotifyAll). */
static jrawMonitorID profiler_lock;
static int profiling_active = 0;
static int sampler_running  = 0;

/* ── Record or increment a stack sample ── */
static void record_sample(const char *key) {
    /* Linear search — adequate for a minimal profiler */
    for (int i = 0; i < sample_count; i++) {
        if (strcmp(samples[i].key, key) == 0) {
            samples[i].count++;
            return;
        }
    }
    if (sample_count < MAX_SAMPLES) {
        /* snprintf always NUL-terminates (strncpy may not) */
        snprintf(samples[sample_count].key, SAMPLE_KEY_LEN, "%s", key);
        samples[sample_count].count = 1;
        sample_count++;
    }
}

/* ── Sample one thread's stack ── */
static void sample_one_thread(jvmtiEnv *jvmti, jthread thread) {
    jvmtiFrameInfo frames[MAX_FRAMES];
    jint frame_count;

    /* A CPU profiler only cares about threads running Java code.
       Without this filter, idle JDK threads (Reference Handler,
       Finalizer, Common-Cleaner...) are sampled at every tick and
       dominate the flamegraph. Threads blocked in native calls report
       RUNNABLE too, so exclude IN_NATIVE as well. */
    jint state = 0;
    if ((*jvmti)->GetThreadState(jvmti, thread, &state) != JVMTI_ERROR_NONE)
        return;
    if (!(state & JVMTI_THREAD_STATE_RUNNABLE) ||
        (state & JVMTI_THREAD_STATE_IN_NATIVE))
        return;

    jvmtiError err = (*jvmti)->GetStackTrace(jvmti, thread, 0,
                                              MAX_FRAMES, frames, &frame_count);
    if (err != JVMTI_ERROR_NONE || frame_count == 0) return;

    /* A native top frame (location == -1) means the thread is parked
       inside the VM or OS — e.g. Reference Handler blocks in a native
       method yet still reports RUNNABLE without IN_NATIVE. Not CPU
       time spent in Java code, so drop the sample. */
    if (frames[0].location == -1) return;

    /* Build the collapsed stack key (bottom → top) */
    char key[SAMPLE_KEY_LEN];
    key[0] = '\0';
    int pos = 0;

    for (int i = frame_count - 1; i >= 0; i--) {
        char *name = NULL, *csig = NULL;
        jclass dcls;

        (*jvmti)->GetMethodName(jvmti, frames[i].method, &name, NULL, NULL);
        if ((*jvmti)->GetMethodDeclaringClass(jvmti, frames[i].method,
                                               &dcls) == JVMTI_ERROR_NONE) {
            (*jvmti)->GetClassSignature(jvmti, dcls, &csig, NULL);
        }

        /* Format: Lcom/foo/Bar; → com/foo/Bar
           Strip BOTH the leading 'L' and the trailing ';' BEFORE
           formatting. The ';' is the frame separator in collapsed
           stack format — leaving it embedded in the key would make
           flamegraph.pl split every frame at the wrong place. */
        const char *class_name = "?";
        if (csig) {
            size_t slen = strlen(csig);
            if (csig[0] == 'L' && slen > 1) {
                if (csig[slen - 1] == ';')
                    csig[slen - 1] = '\0';   /* drop trailing ';' */
                class_name = csig + 1;        /* drop leading 'L'  */
            } else {
                class_name = csig;            /* array/primitive sig */
            }
        }

        int remaining = SAMPLE_KEY_LEN - pos;
        if (remaining <= 1) {  /* key buffer full — truncate stack */
            if (name) (*jvmti)->Deallocate(jvmti, (unsigned char*)name);
            if (csig) (*jvmti)->Deallocate(jvmti, (unsigned char*)csig);
            break;
        }
        int len = snprintf(key + pos, (size_t)remaining,
                           "%s%s.%s",
                           (pos > 0) ? ";" : "",
                           class_name,
                           name ? name : "?");
        if (len > 0)
            pos += (len < remaining) ? len : remaining - 1;

        if (name) (*jvmti)->Deallocate(jvmti, (unsigned char*)name);
        if (csig) (*jvmti)->Deallocate(jvmti, (unsigned char*)csig);
    }

    if (pos > 0) {
        (*jvmti)->RawMonitorEnter(jvmti, profiler_lock);
        record_sample(key);
        (*jvmti)->RawMonitorExit(jvmti, profiler_lock);
    }
}

/* ── Sampling loop (runs on a dedicated agent thread) ── */
static void JNICALL sample_thread_func(jvmtiEnv *jvmti, JNIEnv *jni, void *arg) {
    printf("[Profiler] Sampling thread started\n");

    (*jvmti)->RawMonitorEnter(jvmti, profiler_lock);
    sampler_running = 1;
    while (profiling_active) {
        /* Don't hold the lock while walking stacks */
        (*jvmti)->RawMonitorExit(jvmti, profiler_lock);

        jthread *threads = NULL;
        jint thread_count;

        jvmtiError err = (*jvmti)->GetAllThreads(jvmti, &thread_count, &threads);
        if (err == JVMTI_ERROR_NONE) {
            for (int i = 0; i < thread_count; i++) {
                sample_one_thread(jvmti, threads[i]);
            }
            (*jvmti)->Deallocate(jvmti, (unsigned char*)threads);
        }

        /* Wait ~10ms between samples. A timed RawMonitorWait doubles
           as a portable sleep — and VMDeath can cut it short. */
        (*jvmti)->RawMonitorEnter(jvmti, profiler_lock);
        if (profiling_active)
            (*jvmti)->RawMonitorWait(jvmti, profiler_lock, INTERVAL_MS);
    }
    sampler_running = 0;
    (*jvmti)->RawMonitorNotifyAll(jvmti, profiler_lock);  /* wake VMDeath */
    (*jvmti)->RawMonitorExit(jvmti, profiler_lock);
    printf("[Profiler] Sampling thread stopped\n");
}

/* ── Write flamegraph-format output ── */
static void write_output(void) {
    FILE *f = fopen("profiler_output.txt", "w");
    if (!f) { fprintf(stderr, "[Profiler] Cannot open output file\n"); return; }

    for (int i = 0; i < sample_count; i++) {
        fprintf(f, "%s %d\n", samples[i].key, samples[i].count);
    }
    fclose(f);
    printf("[Profiler] Wrote %d unique stacks to profiler_output.txt\n", sample_count);
}

/* ── Self time: sum sample counts by the stack's top (last) frame ── */
#define MAX_LEAVES 256
#define TOP_SELF   5

static void print_self_time(void) {
    static char leaf[MAX_LEAVES][SAMPLE_KEY_LEN];
    static int  leaf_count[MAX_LEAVES];
    int leaves = 0, total = 0;

    for (int i = 0; i < sample_count; i++) {
        const char *last = strrchr(samples[i].key, ';');
        const char *name = last ? last + 1 : samples[i].key;
        total += samples[i].count;
        int k = 0;
        while (k < leaves && strcmp(leaf[k], name) != 0) k++;
        if (k == leaves) {
            if (leaves == MAX_LEAVES) continue;
            size_t len = strlen(name);   /* always < SAMPLE_KEY_LEN: it's a key's tail */
            if (len >= SAMPLE_KEY_LEN) len = SAMPLE_KEY_LEN - 1;
            memcpy(leaf[k], name, len);
            leaf[k][len] = '\0';
            leaf_count[k] = 0;
            leaves++;
        }
        leaf_count[k] += samples[i].count;
    }

    printf("[Profiler] Top methods by self time (%d samples):\n", total);
    for (int rank = 1; rank <= TOP_SELF; rank++) {
        int best = -1;
        for (int k = 0; k < leaves; k++)
            if (leaf_count[k] > 0 && (best < 0 || leaf_count[k] > leaf_count[best])) best = k;
        if (best < 0) break;
        printf("[Profiler]   %d. %-40s %4d samples (%5.1f%%)\n", rank, leaf[best],
               leaf_count[best], total ? 100.0 * leaf_count[best] / total : 0.0);
        leaf_count[best] = -leaf_count[best];   /* mark as printed */
    }
}

/* ── Callbacks ── */
static void JNICALL cbVMInit(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread) {
    profiling_active = 1;

    /* RunAgentThread requires a real (unstarted) java.lang.Thread
       object — passing NULL fails with JVMTI_ERROR_INVALID_THREAD.
       Create one via JNI (safe here: VM_INIT means full JNI is live). */
    jclass thread_class = (*jni)->FindClass(jni, "java/lang/Thread");
    jmethodID ctor = (*jni)->GetMethodID(jni, thread_class, "<init>", "()V");
    jthread agent_thread = (*jni)->NewObject(jni, thread_class, ctor);
    if (agent_thread == NULL) {
        (*jni)->ExceptionClear(jni);
        fprintf(stderr, "[Profiler] Failed to create agent thread object\n");
        profiling_active = 0;
        return;
    }

    jvmtiError err = (*jvmti)->RunAgentThread(jvmti, agent_thread,
        sample_thread_func, NULL, JVMTI_THREAD_NORM_PRIORITY);
    CHECK_JVMTI_ERROR(jvmti, err, "RunAgentThread");
    if (err != JVMTI_ERROR_NONE) profiling_active = 0;
}

static void JNICALL cbVMDeath(jvmtiEnv *jvmti, JNIEnv *jni) {
    (*jvmti)->RawMonitorEnter(jvmti, profiler_lock);

    /* Signal the sampler to stop and WAIT until it has — a fixed
       sleep() races with a sampler that is mid-way through a slow
       stack walk. The 1s timeout guards against a sampler that
       never started or is stuck. */
    profiling_active = 0;
    (*jvmti)->RawMonitorNotifyAll(jvmti, profiler_lock);
    for (int waited = 0; sampler_running && waited < 1000; waited += 100)
        (*jvmti)->RawMonitorWait(jvmti, profiler_lock, 100);

    write_output();
    print_self_time();
    (*jvmti)->RawMonitorExit(jvmti, profiler_lock);
    /* profiler_lock is deliberately NOT destroyed: daemon threads
       keep running through VMDeath, and destroying a monitor another
       thread may still touch is a use-after-free. The OS reclaims it. */
}

/* ── Agent entry point ── */
JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    jvmtiEnv *jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;

    jvmtiError err = (*jvmti)->CreateRawMonitor(jvmti, "ProfilerLock", &profiler_lock);
    CHECK_JVMTI_ERROR(jvmti, err, "CreateRawMonitor");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.VMInit  = &cbVMInit;
    callbacks.VMDeath = &cbVMDeath;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
              JVMTI_EVENT_VM_INIT, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable VMInit");
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
              JVMTI_EVENT_VM_DEATH, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable VMDeath");

    printf("[Profiler] Sampling profiler agent loaded (interval: 10ms)\n");
    return JNI_OK;
}
