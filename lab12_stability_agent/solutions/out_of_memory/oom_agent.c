/**
 * Lab 12 — Exercise solution: notice when the heap runs out
 *
 * Exercise (Chapter 12): an agent that keeps running in production must
 * notice resource exhaustion — and capture what it can — *before* the
 * application handles (or dies from) an OutOfMemoryError. The
 * ResourceExhausted event fires when the JVM is about to throw it.
 *
 * The callback reports what ran out (the flags say heap vs. threads, and
 * whether an OutOfMemoryError will be thrown) and which thread asked for
 * the memory. Keep it small: the heap is full, so allocating — even a
 * Java string through JNI — will fail. Native memory (printf) is fine.
 *
 * Run:  java -Xmx32m -agentpath:./build/liboom_agent.so OomTestApp
 */

#include "jvmti_utils.h"

static int events = 0;

static void JNICALL cbResourceExhausted(jvmtiEnv *jvmti, JNIEnv *jni,
                                        jint flags, const void *reserved,
                                        const char *description) {
    events++;
    jthread thread = NULL;
    jvmtiThreadInfo info;
    memset(&info, 0, sizeof(info));
    if ((*jvmti)->GetCurrentThread(jvmti, &thread) == JVMTI_ERROR_NONE)
        (*jvmti)->GetThreadInfo(jvmti, thread, &info);

    printf("[OOM] ResourceExhausted #%d: %s\n", events, description ? description : "?");
    printf("[OOM]   what:   %s%s%s\n",
           flags & JVMTI_RESOURCE_EXHAUSTED_JAVA_HEAP ? "Java heap " : "",
           flags & JVMTI_RESOURCE_EXHAUSTED_THREADS   ? "threads "   : "",
           flags & JVMTI_RESOURCE_EXHAUSTED_OOM_ERROR ? "(an OutOfMemoryError will be thrown)" : "");
    printf("[OOM]   thread: %s\n", info.name ? info.name : "?");
    fflush(stdout);
    if (info.name) (*jvmti)->Deallocate(jvmti, (unsigned char*)info.name);
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    jvmtiEnv *jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;

    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_generate_resource_exhaustion_heap_events    = 1;
    caps.can_generate_resource_exhaustion_threads_events = 1;
    jvmtiError err = (*jvmti)->AddCapabilities(jvmti, &caps);
    CHECK_JVMTI_ERROR(jvmti, err, "AddCapabilities");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.ResourceExhausted = &cbResourceExhausted;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
              JVMTI_EVENT_RESOURCE_EXHAUSTED, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable ResourceExhausted");
    return err == JVMTI_ERROR_NONE ? JNI_OK : JNI_ERR;
}
