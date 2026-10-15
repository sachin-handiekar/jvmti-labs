/**
 * Lab 03 — Exercise solution: whose methods are these?
 *
 * Exercise (Chapter 3): add a filter=<prefix> option, and count the
 * MethodEntry events whose class name starts with it separately from
 * the total. Run it on EventTestApp to see how little of the work is
 * the application's own code.
 *
 * Filtering in the callback isn't free: every event still fires, and we
 * look up each method's class to decide. Real agents cache the decision
 * per jmethodID — or avoid MethodEntry entirely (Chapter 10).
 *
 * Run:  java -agentpath:./build/libfiltered_counter.so=filter=EventTestApp EventTestApp
 */

#include "jvmti_utils.h"
#include <stdlib.h>

static jrawMonitorID lock;
static long total_entries = 0;
static long matching_entries = 0;
static char filter[256] = "";        /* class-name prefix, e.g. com/example */

static void JNICALL cbMethodEntry(jvmtiEnv *jvmti, JNIEnv *jni,
                                  jthread thread, jmethodID method) {
    int matches = 0;
    jclass cls;
    char *sig = NULL;
    if ((*jvmti)->GetMethodDeclaringClass(jvmti, method, &cls) == JVMTI_ERROR_NONE &&
        (*jvmti)->GetClassSignature(jvmti, cls, &sig, NULL) == JVMTI_ERROR_NONE) {
        /* "Lcom/example/Foo;" -> compare from after the 'L' */
        matches = sig[0] == 'L' && strncmp(sig + 1, filter, strlen(filter)) == 0;
        (*jvmti)->Deallocate(jvmti, (unsigned char*)sig);
    }
    (*jvmti)->RawMonitorEnter(jvmti, lock);
    total_entries++;
    matching_entries += matches;
    (*jvmti)->RawMonitorExit(jvmti, lock);
}

static void JNICALL cbVMDeath(jvmtiEnv *jvmti, JNIEnv *jni) {
    (*jvmti)->RawMonitorEnter(jvmti, lock);
    printf("[Filter] MethodEntry events: %ld total, %ld in classes starting with \"%s\" (%.2f%%)\n",
           total_entries, matching_entries, filter,
           total_entries ? 100.0 * (double)matching_entries / (double)total_entries : 0.0);
    (*jvmti)->RawMonitorExit(jvmti, lock);
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    if (options && strncmp(options, "filter=", 7) == 0) {
        snprintf(filter, sizeof(filter), "%s", options + 7);
    }
    if (filter[0] == '\0') {
        fprintf(stderr, "[Filter] usage: -agentpath:...=filter=<class-name prefix>\n");
        return JNI_ERR;
    }

    jvmtiEnv *jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;

    jvmtiError err = (*jvmti)->CreateRawMonitor(jvmti, "filter_lock", &lock);
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
    callbacks.VMDeath     = &cbVMDeath;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_METHOD_ENTRY, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable MethodEntry");
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable VMDeath");
    return JNI_OK;
}
