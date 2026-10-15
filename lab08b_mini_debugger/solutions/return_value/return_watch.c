/**
 * Lab 08b — Exercise solution: what did the method return?
 *
 * Exercise (Chapter 8): breakpoints and single-stepping can't show a
 * method's return value — it only exists on the operand stack for an
 * instant. The MethodExit event can: its last argument IS the value
 * being returned.
 *
 * MethodExit is as expensive as MethodEntry (it fires for every method
 * return), so the agent uses the same trick as single-stepping in §8.6:
 *   1. a breakpoint on the first instruction of the method we care about
 *   2. when it hits, enable MethodExit for THAT thread only, and remember
 *      the frame depth
 *   3. on the MethodExit for that same frame, print the value, and turn
 *      MethodExit off again
 *
 * Run:  java -agentpath:./build/libreturn_watch.so DebugTestApp
 */

#include "jvmti_utils.h"

#define TARGET_CLASS_SIG "LDebugTestApp;"
#define TARGET_METHOD    "checkout"

static jmethodID target_method = NULL;
static jint      target_depth  = 0;

static void JNICALL cbClassPrepare(jvmtiEnv *jvmti, JNIEnv *jni,
                                   jthread thread, jclass klass) {
    char *sig = NULL;
    if ((*jvmti)->GetClassSignature(jvmti, klass, &sig, NULL) != JVMTI_ERROR_NONE) return;
    int is_target = strcmp(sig, TARGET_CLASS_SIG) == 0;
    (*jvmti)->Deallocate(jvmti, (unsigned char*)sig);
    if (!is_target) return;

    jint count = 0;
    jmethodID *methods = NULL;
    (*jvmti)->GetClassMethods(jvmti, klass, &count, &methods);
    for (jint i = 0; i < count; i++) {
        char *name = NULL;
        (*jvmti)->GetMethodName(jvmti, methods[i], &name, NULL, NULL);
        if (name && strcmp(name, TARGET_METHOD) == 0) {
            /* Location 0 = the method's first instruction: "break on entry" */
            jvmtiError err = (*jvmti)->SetBreakpoint(jvmti, methods[i], 0);
            CHECK_JVMTI_ERROR(jvmti, err, "SetBreakpoint");
        }
        if (name) (*jvmti)->Deallocate(jvmti, (unsigned char*)name);
    }
    (*jvmti)->Deallocate(jvmti, (unsigned char*)methods);
}

static void JNICALL cbBreakpoint(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread,
                                 jmethodID method, jlocation location) {
    target_method = method;
    (*jvmti)->GetFrameCount(jvmti, thread, &target_depth);
    printf("[ReturnWatch] %s() called; watching for its return\n", TARGET_METHOD);
    fflush(stdout);
    jvmtiError err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
                         JVMTI_EVENT_METHOD_EXIT, thread);   /* this thread only */
    CHECK_JVMTI_ERROR(jvmti, err, "Enable MethodExit");
}

/* Fires for EVERY method returning on this thread — println and all.
   Only the exit of the watched frame matters. */
static void JNICALL cbMethodExit(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread,
                                 jmethodID method, jboolean was_popped_by_exception,
                                 jvalue return_value) {
    if (method != target_method) return;
    jint depth = 0;
    (*jvmti)->GetFrameCount(jvmti, thread, &depth);
    if (depth != target_depth) return;   /* a recursive call, not ours */

    if (was_popped_by_exception)
        printf("[ReturnWatch] %s() exited by throwing an exception\n", TARGET_METHOD);
    else   /* checkout returns an int, so the value is in .i */
        printf("[ReturnWatch] %s() returned %d\n", TARGET_METHOD, (int)return_value.i);
    fflush(stdout);
    (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_DISABLE,
                                       JVMTI_EVENT_METHOD_EXIT, thread);
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    jvmtiEnv *jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;

    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_generate_breakpoint_events  = 1;
    caps.can_generate_method_exit_events = 1;
    jvmtiError err = (*jvmti)->AddCapabilities(jvmti, &caps);
    CHECK_JVMTI_ERROR(jvmti, err, "AddCapabilities");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.ClassPrepare = &cbClassPrepare;
    callbacks.Breakpoint   = &cbBreakpoint;
    callbacks.MethodExit   = &cbMethodExit;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    /* MethodExit is NOT enabled globally — only per thread, at the breakpoint */
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_CLASS_PREPARE, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable ClassPrepare");
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_BREAKPOINT, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable Breakpoint");
    return JNI_OK;
}
