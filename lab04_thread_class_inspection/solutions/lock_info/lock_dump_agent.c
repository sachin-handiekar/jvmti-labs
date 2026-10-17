/**
 * Lab 04 — Exercise solution: who holds the lock?
 *
 * Exercise (Chapter 4): extend the thread dump with lock information
 * (§4.6): for every thread, the monitors it owns (GetOwnedMonitorInfo),
 * the one it is blocked on or waiting for (GetCurrentContendedMonitor),
 * and that monitor's current owner (GetObjectMonitorUsage). That last
 * step turns a thread dump into the start of a deadlock detector:
 * "thread A is blocked on a lock held by thread B".
 *
 * Monitors are labelled like jstack does: class name plus identity hash,
 * read through JNI (System.identityHashCode) — safe here, at VMDeath.
 *
 * Run:  java -agentpath:./build/liblock_dump_agent.so StackTestApp
 */

#include "jvmti_utils.h"

static jclass    system_class = NULL;   /* global ref, set at VMInit */
static jmethodID identity_hash = NULL;

/* "<java/lang/Object@1b6d3586>" */
static void monitor_label(jvmtiEnv *jvmti, JNIEnv *jni, jobject obj,
                          char *out, size_t n) {
    char *sig = NULL;
    jclass cls = (*jni)->GetObjectClass(jni, obj);
    (*jvmti)->GetClassSignature(jvmti, cls, &sig, NULL);
    jint hash = (*jni)->CallStaticIntMethod(jni, system_class, identity_hash, obj);
    if ((*jni)->ExceptionCheck(jni)) { (*jni)->ExceptionClear(jni); hash = 0; }
    /* "Ljava/lang/Object;" -> "java/lang/Object" */
    const char *name = sig ? sig : "?";
    size_t len = strlen(name);
    if (sig && name[0] == 'L' && len > 2) { name++; len -= 2; }
    snprintf(out, n, "<%.*s@%x>", (int)len, name, (unsigned)hash);
    if (sig) (*jvmti)->Deallocate(jvmti, (unsigned char*)sig);
    (*jni)->DeleteLocalRef(jni, cls);
}

static void thread_name(jvmtiEnv *jvmti, jthread thread, char *out, size_t n) {
    jvmtiThreadInfo info;
    memset(&info, 0, sizeof(info));
    snprintf(out, n, "<unnamed>");
    if ((*jvmti)->GetThreadInfo(jvmti, thread, &info) == JVMTI_ERROR_NONE && info.name) {
        snprintf(out, n, "%s", info.name);
        (*jvmti)->Deallocate(jvmti, (unsigned char*)info.name);
    }
}

static void JNICALL cbVMInit(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread) {
    jclass sys = (*jni)->FindClass(jni, "java/lang/System");
    if (sys == NULL) { (*jni)->ExceptionClear(jni); return; }
    system_class  = (jclass)(*jni)->NewGlobalRef(jni, sys);
    identity_hash = (*jni)->GetStaticMethodID(jni, sys, "identityHashCode",
                                              "(Ljava/lang/Object;)I");
    if ((*jni)->ExceptionCheck(jni)) { (*jni)->ExceptionClear(jni); identity_hash = NULL; }
}

static void JNICALL cbVMDeath(jvmtiEnv *jvmti, JNIEnv *jni) {
    if (!identity_hash) return;
    jint count = 0;
    jthread *threads = NULL;
    if ((*jvmti)->GetAllThreads(jvmti, &count, &threads) != JVMTI_ERROR_NONE) return;

    printf("========== Lock report ==========\n");
    for (jint i = 0; i < count; i++) {
        char name[128], label[256];
        thread_name(jvmti, threads[i], name, sizeof(name));

        /* Monitors this thread holds */
        jint owned_count = 0;
        jobject *owned = NULL;
        if ((*jvmti)->GetOwnedMonitorInfo(jvmti, threads[i], &owned_count, &owned)
                == JVMTI_ERROR_NONE) {
            for (jint m = 0; m < owned_count; m++) {
                monitor_label(jvmti, jni, owned[m], label, sizeof(label));
                printf("\"%s\" owns %s\n", name, label);
                (*jni)->DeleteLocalRef(jni, owned[m]);
            }
            (*jvmti)->Deallocate(jvmti, (unsigned char*)owned);
        }

        /* The monitor it is blocked on (or waiting on, in Object.wait) */
        jobject contended = NULL;
        if ((*jvmti)->GetCurrentContendedMonitor(jvmti, threads[i], &contended)
                == JVMTI_ERROR_NONE && contended != NULL) {
            monitor_label(jvmti, jni, contended, label, sizeof(label));
            jint state = 0;
            (*jvmti)->GetThreadState(jvmti, threads[i], &state);
            int blocked = (state & JVMTI_THREAD_STATE_BLOCKED_ON_MONITOR_ENTER) != 0;

            jvmtiMonitorUsage usage;
            memset(&usage, 0, sizeof(usage));
            char owner[128] = "nobody";
            if ((*jvmti)->GetObjectMonitorUsage(jvmti, contended, &usage) == JVMTI_ERROR_NONE) {
                if (usage.owner) thread_name(jvmti, usage.owner, owner, sizeof(owner));
                if (usage.waiters)
                    (*jvmti)->Deallocate(jvmti, (unsigned char*)usage.waiters);
                if (usage.notify_waiters)
                    (*jvmti)->Deallocate(jvmti, (unsigned char*)usage.notify_waiters);
            }
            printf("\"%s\" %s %s, owned by \"%s\"\n", name,
                   blocked ? "is BLOCKED entering" : "is waiting on", label, owner);
            (*jni)->DeleteLocalRef(jni, contended);
        }
    }
    printf("=================================\n");
    (*jvmti)->Deallocate(jvmti, (unsigned char*)threads);
    fflush(stdout);
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    jvmtiEnv *jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;

    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_get_owned_monitor_info       = 1;
    caps.can_get_current_contended_monitor = 1;
    caps.can_get_monitor_info             = 1;
    jvmtiError err = (*jvmti)->AddCapabilities(jvmti, &caps);
    CHECK_JVMTI_ERROR(jvmti, err, "AddCapabilities");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.VMInit  = &cbVMInit;
    callbacks.VMDeath = &cbVMDeath;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_INIT, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable VMInit");
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable VMDeath");
    return JNI_OK;
}
