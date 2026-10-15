/**
 * Lab 09 — Advanced Techniques: Raw Monitors, TLS, Reentrancy
 *
 * §9.6 "Thread-Safe Method Counter" example. Counts every method entry
 * on every thread — exactly, and without taking a lock per call:
 *
 *   - Thread-Local Storage (§9.2): each thread gets its own counter in a
 *     struct attached with SetThreadLocalStorage, so MethodEntry — which
 *     fires millions of times, on every thread — never contends on a lock
 *   - Raw monitor (§9.1): at ThreadEnd each thread's count is merged into
 *     a global total under agent_lock — one lock acquisition per thread,
 *     not one per method call. The same lock serializes the log file
 *   - Reentrancy guard (§9.3): a per-thread flag that stops the callback
 *     from re-entering itself if anything it calls runs Java code
 *
 * Build:
 *   mkdir build && cd build && cmake .. && cmake --build .
 *
 * Run:
 *   javac AdvancedTestApp.java
 *   java -agentpath:./build/libadvanced_agent.so AdvancedTestApp
 */

#include "jvmti_utils.h"
#include <stdlib.h>

#define LOG_FILE  "advanced_agent.log"
#define LOG_EVERY 10000   /* log one sample method per N entries per thread */

/* ------------------------------------------------------------------ */
/*  Raw monitor: guards the global total and the log file              */
/* ------------------------------------------------------------------ */

static jrawMonitorID agent_lock = NULL;
static long long total_entries = 0;    /* merged from finished threads */
static int       threads_merged = 0;

static void log_locked(jvmtiEnv *jvmti, const char *msg) {
    (*jvmti)->RawMonitorEnter(jvmti, agent_lock);
    jvmti_log(LOG_FILE, msg);
    (*jvmti)->RawMonitorExit(jvmti, agent_lock);
}

/* ------------------------------------------------------------------ */
/*  Thread-Local Storage                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    long long method_entries;   /* touched only by the owning thread */
    int       in_callback;      /* reentrancy guard */
} thread_data_t;

/* Returns this thread's data, creating it on first use. Threads that
   were already running when the agent loaded (like main) never got a
   ThreadStart event, so they are set up lazily here. */
static thread_data_t* get_thread_data(jvmtiEnv* jvmti, jthread thread) {
    thread_data_t* data = NULL;
    jvmtiError err = (*jvmti)->GetThreadLocalStorage(jvmti, thread,
                                                      (void**)&data);
    if (err == JVMTI_ERROR_NONE && data == NULL) {
        data = (thread_data_t*)calloc(1, sizeof(thread_data_t));
        if (data) (*jvmti)->SetThreadLocalStorage(jvmti, thread, data);
    }
    return data;
}

static void thread_name(jvmtiEnv* jvmti, jthread thread, char *out, size_t n) {
    jvmtiThreadInfo tinfo;
    memset(&tinfo, 0, sizeof(tinfo));
    snprintf(out, n, "<unnamed>");
    if ((*jvmti)->GetThreadInfo(jvmti, thread, &tinfo) == JVMTI_ERROR_NONE
        && tinfo.name) {
        snprintf(out, n, "%s", tinfo.name);
        (*jvmti)->Deallocate(jvmti, (unsigned char*)tinfo.name);
    }
}

/* ------------------------------------------------------------------ */
/*  Event Callbacks                                                    */
/* ------------------------------------------------------------------ */

static void JNICALL cbMethodEntry(jvmtiEnv* jvmti, JNIEnv* jni,
                                   jthread thread, jmethodID method) {
    thread_data_t* tdata = get_thread_data(jvmti, thread);
    if (!tdata) return;

    /* Reentrancy guard: if anything below ran Java code (a JNI call, a
       toString()...), that code would fire MethodEntry on this same
       thread and recurse into us. The flag makes the nested call return
       immediately. Our JVMTI-only callback can't recurse, but the guard
       costs one branch and keeps the agent safe as it grows. */
    if (tdata->in_callback) return;
    tdata->in_callback = 1;

    /* No lock: only this thread ever touches its own counter */
    tdata->method_entries++;

    if (tdata->method_entries % LOG_EVERY == 0) {
        char *class_sig = NULL, *method_name = NULL;
        jclass declaring_class;
        if ((*jvmti)->GetMethodDeclaringClass(jvmti, method,
                                               &declaring_class) == JVMTI_ERROR_NONE) {
            (*jvmti)->GetClassSignature(jvmti, declaring_class, &class_sig, NULL);
        }
        (*jvmti)->GetMethodName(jvmti, method, &method_name, NULL, NULL);

        char name[128], buf[512];
        thread_name(jvmti, thread, name, sizeof(name));
        snprintf(buf, sizeof(buf), "[Sample] %s: entry #%lld is %s.%s",
                 name, tdata->method_entries,
                 class_sig ? class_sig : "?", method_name ? method_name : "?");
        log_locked(jvmti, buf);

        if (class_sig) (*jvmti)->Deallocate(jvmti, (unsigned char*)class_sig);
        if (method_name) (*jvmti)->Deallocate(jvmti, (unsigned char*)method_name);
    }

    tdata->in_callback = 0;
}

static void JNICALL cbThreadStart(jvmtiEnv* jvmti, JNIEnv* jni,
                                   jthread thread) {
    get_thread_data(jvmti, thread);   /* allocate TLS up front */

    char name[128], buf[256];
    thread_name(jvmti, thread, name, sizeof(name));
    snprintf(buf, sizeof(buf), "[ThreadStart] %s — TLS allocated", name);
    log_locked(jvmti, buf);
}

static void JNICALL cbThreadEnd(jvmtiEnv* jvmti, JNIEnv* jni,
                                 jthread thread) {
    thread_data_t* data = NULL;
    if ((*jvmti)->GetThreadLocalStorage(jvmti, thread,
                                         (void**)&data) != JVMTI_ERROR_NONE
        || data == NULL) {
        return;
    }

    char name[128], buf[256];
    thread_name(jvmti, thread, name, sizeof(name));

    /* Merge into the global total: one lock acquisition per thread */
    (*jvmti)->RawMonitorEnter(jvmti, agent_lock);
    total_entries += data->method_entries;
    threads_merged++;
    snprintf(buf, sizeof(buf), "[ThreadEnd] %s — %lld method entries",
             name, data->method_entries);
    jvmti_log(LOG_FILE, buf);
    (*jvmti)->RawMonitorExit(jvmti, agent_lock);

    /* Free TLS in ThreadEnd, or it leaks once per thread (§9.2) */
    (*jvmti)->SetThreadLocalStorage(jvmti, thread, NULL);
    free(data);
}

static void JNICALL cbVMDeath(jvmtiEnv* jvmti, JNIEnv* jni) {
    char buf[256];
    (*jvmti)->RawMonitorEnter(jvmti, agent_lock);
    snprintf(buf, sizeof(buf),
             "[Agent] VMDeath — %lld method entries across %d finished threads",
             total_entries, threads_merged);
    jvmti_log(LOG_FILE, buf);
    printf("%s\n", buf);
    (*jvmti)->RawMonitorExit(jvmti, agent_lock);

    /* agent_lock is deliberately NOT destroyed: daemon threads keep
       firing MethodEntry while the VM shuts down, and destroying a
       monitor they may still enter is a use-after-free (§9.1, §9.7).
       The OS reclaims it when the process exits. */
}

/* ------------------------------------------------------------------ */
/*  Agent Lifecycle                                                    */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    printf("[Agent] advanced_agent loaded (Lab 09)\n");

    jvmtiEnv* jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;

    jvmtiError err;

    /* Raw monitors can be created in any phase, even OnLoad */
    err = (*jvmti)->CreateRawMonitor(jvmti, "agent_lock", &agent_lock);
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
    callbacks.MethodEntry  = &cbMethodEntry;
    callbacks.ThreadStart  = &cbThreadStart;
    callbacks.ThreadEnd    = &cbThreadEnd;
    callbacks.VMDeath      = &cbVMDeath;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    const jvmtiEvent events[] = {
        JVMTI_EVENT_METHOD_ENTRY, JVMTI_EVENT_THREAD_START,
        JVMTI_EVENT_THREAD_END,   JVMTI_EVENT_VM_DEATH,
    };
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
        err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
                                                 events[i], NULL);
        CHECK_JVMTI_ERROR(jvmti, err, "SetEventNotificationMode");
        if (err != JVMTI_ERROR_NONE) return JNI_ERR;
    }

    jvmti_log(LOG_FILE, "[Agent] Agent_OnLoad complete — advanced features active.");
    return JNI_OK;
}
