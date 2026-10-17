/**
 * Lab 07 — Exercise solution: a GC pause histogram
 *
 * Exercise (Chapter 7): instead of total/max/average, sort every
 * collection's pause into buckets. A histogram shows what an average
 * hides: one long pause among many short ones.
 *
 * GarbageCollectionStart/Finish run in a restricted environment (§7.3):
 * no JNI, no allocation, no I/O, and only a handful of JVMTI functions
 * (GetTime is one). So the callbacks only do arithmetic into a static
 * array; everything is printed at VMDeath.
 *
 * Run:  java -agentpath:./build/libgc_histogram_agent.so RuntimeTestApp
 */

#include "jvmti_utils.h"

/* Bucket upper bounds in microseconds; the last bucket is open-ended */
static const jlong bucket_limit_us[] = { 1000, 5000, 20000, 100000 };
static const char *bucket_label[]    = { "< 1 ms", "1-5 ms", "5-20 ms", "20-100 ms", ">= 100 ms" };
#define BUCKETS 5

static jlong gc_start_ns = 0;
static long  histogram[BUCKETS];
static long  collections = 0;

static void JNICALL cbGCStart(jvmtiEnv *jvmti) {
    (*jvmti)->GetTime(jvmti, &gc_start_ns);
}

static void JNICALL cbGCFinish(jvmtiEnv *jvmti) {
    jlong now = 0;
    (*jvmti)->GetTime(jvmti, &now);
    jlong pause_us = (now - gc_start_ns) / 1000;
    int b = 0;
    while (b < BUCKETS - 1 && pause_us >= bucket_limit_us[b]) b++;
    histogram[b]++;
    collections++;
}

static void JNICALL cbVMDeath(jvmtiEnv *jvmti, JNIEnv *jni) {
    printf("[GC] Pause histogram (%ld collections):\n", collections);
    for (int b = 0; b < BUCKETS; b++) {
        printf("[GC]   %-10s %4ld  ", bucket_label[b], histogram[b]);
        for (long i = 0; i < histogram[b] && i < 50; i++) putchar('#');
        putchar('\n');
    }
    fflush(stdout);
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    jvmtiEnv *jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;

    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_generate_garbage_collection_events = 1;
    jvmtiError err = (*jvmti)->AddCapabilities(jvmti, &caps);
    CHECK_JVMTI_ERROR(jvmti, err, "AddCapabilities");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.GarbageCollectionStart  = &cbGCStart;
    callbacks.GarbageCollectionFinish = &cbGCFinish;
    callbacks.VMDeath                 = &cbVMDeath;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    const jvmtiEvent events[] = {
        JVMTI_EVENT_GARBAGE_COLLECTION_START, JVMTI_EVENT_GARBAGE_COLLECTION_FINISH,
        JVMTI_EVENT_VM_DEATH,
    };
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
        err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, events[i], NULL);
        CHECK_JVMTI_ERROR(jvmti, err, "SetEventNotificationMode");
        if (err != JVMTI_ERROR_NONE) return JNI_ERR;
    }
    return JNI_OK;
}
