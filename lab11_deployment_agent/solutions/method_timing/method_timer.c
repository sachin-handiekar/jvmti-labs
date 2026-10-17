/**
 * Lab 11 — Exercise solution: how long does each call take?
 *
 * Exercise (Chapter 11): extend the production agent's method_entry /
 * method_exit options into a timer — log each call's duration for the
 * monitored package. Entry and exit arrive as separate events, possibly
 * nested (a calls b calls c), so each thread keeps a small stack of start
 * times in thread-local storage (Chapter 9): push on entry, pop on exit.
 *
 * MethodExit fires for exits by exception too (was_popped_by_exception),
 * so the pushes and pops always balance. As §11.6 warns, entry/exit
 * events are expensive — fine for a focused diagnosis, not for
 * always-on production use; profile with sampling instead (Chapter 10).
 *
 * Run:  java -agentpath:./build/libmethod_timer.so=package=com/example \
 *            -cp . com.example.DeployTestApp
 */

#include "jvmti_utils.h"
#include <stdlib.h>

#define MAX_DEPTH 64

typedef struct {
    jlong start[MAX_DEPTH];
    int   depth;
} timer_stack_t;

static char package[256] = "com/example";

/* Is this method in the monitored package? Fills label if so. */
static int monitored(jvmtiEnv *jvmti, jmethodID method, char *label, size_t n) {
    jclass cls;
    char *sig = NULL, *name = NULL, *msig = NULL;
    int match = 0;
    if ((*jvmti)->GetMethodDeclaringClass(jvmti, method, &cls) != JVMTI_ERROR_NONE ||
        (*jvmti)->GetClassSignature(jvmti, cls, &sig, NULL) != JVMTI_ERROR_NONE)
        return 0;
    if (sig[0] == 'L' && strncmp(sig + 1, package, strlen(package)) == 0) {
        match = 1;
        if (label && (*jvmti)->GetMethodName(jvmti, method, &name, &msig, NULL) == JVMTI_ERROR_NONE) {
            size_t len = strlen(sig);   /* "Lcom/example/X;" -> "com/example/X" */
            snprintf(label, n, "%.*s.%s%s", (int)(len - 2), sig + 1, name, msig);
            (*jvmti)->Deallocate(jvmti, (unsigned char*)name);
            (*jvmti)->Deallocate(jvmti, (unsigned char*)msig);
        }
    }
    (*jvmti)->Deallocate(jvmti, (unsigned char*)sig);
    return match;
}

static timer_stack_t *stack_of(jvmtiEnv *jvmti, jthread thread) {
    timer_stack_t *ts = NULL;
    if ((*jvmti)->GetThreadLocalStorage(jvmti, thread, (void**)&ts) == JVMTI_ERROR_NONE && !ts) {
        ts = calloc(1, sizeof(timer_stack_t));
        if (ts) (*jvmti)->SetThreadLocalStorage(jvmti, thread, ts);
    }
    return ts;
}

static void JNICALL cbMethodEntry(jvmtiEnv *jvmti, JNIEnv *jni,
                                  jthread thread, jmethodID method) {
    if (!monitored(jvmti, method, NULL, 0)) return;
    timer_stack_t *ts = stack_of(jvmti, thread);
    if (!ts) return;
    jlong now = 0;
    (*jvmti)->GetTime(jvmti, &now);
    if (ts->depth < MAX_DEPTH) ts->start[ts->depth] = now;
    ts->depth++;   /* count even past MAX_DEPTH so pops stay balanced */
}

static void JNICALL cbMethodExit(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread,
                                 jmethodID method, jboolean was_popped_by_exception,
                                 jvalue return_value) {
    char label[512];
    if (!monitored(jvmti, method, label, sizeof(label))) return;
    timer_stack_t *ts = stack_of(jvmti, thread);
    if (!ts || ts->depth == 0) return;   /* entered before we started */
    ts->depth--;
    if (ts->depth >= MAX_DEPTH) return;

    jlong now = 0;
    (*jvmti)->GetTime(jvmti, &now);
    char buf[640];
    snprintf(buf, sizeof(buf), "[Timing] %*s%s took %.3f ms%s",
             ts->depth * 2, "", label, (double)(now - ts->start[ts->depth]) / 1e6,
             was_popped_by_exception ? " (exception)" : "");
    jvmti_log("method_timer.log", buf);
}

static void JNICALL cbThreadEnd(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread) {
    timer_stack_t *ts = NULL;
    if ((*jvmti)->GetThreadLocalStorage(jvmti, thread, (void**)&ts) == JVMTI_ERROR_NONE && ts) {
        (*jvmti)->SetThreadLocalStorage(jvmti, thread, NULL);
        free(ts);
    }
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    if (options && strncmp(options, "package=", 8) == 0)
        snprintf(package, sizeof(package), "%s", options + 8);

    jvmtiEnv *jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;
    jvmti_log("method_timer.log", "[Timing] Method timer loaded");   /* opens the log */

    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_generate_method_entry_events = 1;
    caps.can_generate_method_exit_events  = 1;
    jvmtiError err = (*jvmti)->AddCapabilities(jvmti, &caps);
    CHECK_JVMTI_ERROR(jvmti, err, "AddCapabilities");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.MethodEntry = &cbMethodEntry;
    callbacks.MethodExit  = &cbMethodExit;
    callbacks.ThreadEnd   = &cbThreadEnd;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    const jvmtiEvent events[] = {
        JVMTI_EVENT_METHOD_ENTRY, JVMTI_EVENT_METHOD_EXIT, JVMTI_EVENT_THREAD_END,
    };
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
        err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, events[i], NULL);
        CHECK_JVMTI_ERROR(jvmti, err, "SetEventNotificationMode");
        if (err != JVMTI_ERROR_NONE) return JNI_ERR;
    }
    return JNI_OK;
}
