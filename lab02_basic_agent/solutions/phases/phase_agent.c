/**
 * Lab 02 — Exercise solution: which phase is the JVM in?
 *
 * Exercise (Chapter 2): print the JVM's phase (GetPhase) at every
 * lifecycle point — Agent_OnLoad, VMStart, VMInit, VMDeath — to see why
 * some JVMTI and JNI functions only work at certain times.
 *
 *   ONLOAD     Agent_OnLoad: only set capabilities, callbacks, events
 *   PRIMORDIAL between OnLoad and VMStart: nothing is running yet
 *   START      VMStart: JNI is available, but the VM isn't fully up
 *   LIVE       VMInit onwards: everything is available
 *   DEAD       after VMDeath: no more JVMTI calls
 *
 * Run:  java -agentpath:./build/libphase_agent.so HelloApp
 */

#include "jvmti_utils.h"

static const char *phase_name(jvmtiEnv *jvmti) {
    jvmtiPhase phase;
    if ((*jvmti)->GetPhase(jvmti, &phase) != JVMTI_ERROR_NONE) return "?";
    switch (phase) {
    case JVMTI_PHASE_ONLOAD:     return "ONLOAD";
    case JVMTI_PHASE_PRIMORDIAL: return "PRIMORDIAL";
    case JVMTI_PHASE_START:      return "START";
    case JVMTI_PHASE_LIVE:       return "LIVE";
    case JVMTI_PHASE_DEAD:       return "DEAD";
    default:                     return "?";
    }
}

static void JNICALL cbVMStart(jvmtiEnv *jvmti, JNIEnv *jni) {
    printf("[Phase] VMStart       -> %s\n", phase_name(jvmti));
}

static void JNICALL cbVMInit(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread) {
    printf("[Phase] VMInit        -> %s\n", phase_name(jvmti));
}

static void JNICALL cbVMDeath(jvmtiEnv *jvmti, JNIEnv *jni) {
    printf("[Phase] VMDeath       -> %s\n", phase_name(jvmti));
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    jvmtiEnv *jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;
    printf("[Phase] Agent_OnLoad  -> %s\n", phase_name(jvmti));

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.VMStart = &cbVMStart;
    callbacks.VMInit  = &cbVMInit;
    callbacks.VMDeath = &cbVMDeath;
    jvmtiError err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    const jvmtiEvent events[] = {
        JVMTI_EVENT_VM_START, JVMTI_EVENT_VM_INIT, JVMTI_EVENT_VM_DEATH,
    };
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
        err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, events[i], NULL);
        CHECK_JVMTI_ERROR(jvmti, err, "SetEventNotificationMode");
        if (err != JVMTI_ERROR_NONE) return JNI_ERR;
    }
    return JNI_OK;
}

/* Agent_OnUnload runs after the VM is gone; JVMTI can no longer be used,
   so we can't even ask for the phase here (it would be DEAD). */
JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm) {
    printf("[Phase] Agent_OnUnload   (VM gone; no JVMTI calls)\n");
}
