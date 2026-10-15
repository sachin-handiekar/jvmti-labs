/**
 * Lab 06 — Class Load Monitor (ClassFileLoadHook)
 *
 * §6.7 "Class Load Monitor" example:
 *   - Registers a ClassFileLoadHook callback, which the JVM calls with
 *     the raw class-file bytes of every class before it is defined
 *   - Requests can_generate_all_class_hook_events so the hook also
 *     fires for classes loaded very early (and for retransformation)
 *   - Filters out JDK classes (java/, jdk/, sun/) — §6.3: never
 *     instrument these, and don't flood the output with them
 *   - Prints a class-count / bytecode-size summary at VMDeath
 *
 * The hook leaves *new_class_data untouched, so every class is loaded
 * unmodified. To transform a class you would allocate a buffer with
 * jvmti->Allocate, write the new class file into it, and set
 * *new_class_data / *new_class_data_len (§6.4).
 *
 * Build:
 *   mkdir build && cd build && cmake .. && cmake --build .
 *
 * Run:
 *   javac TransformTestApp.java
 *   java -agentpath:./build/libclass_transform_agent.so TransformTestApp
 */

#include "jvmti_utils.h"

/* Class loading is concurrent — guard the counters with a raw
   monitor (Lab 9) so the summary is exact. */
static jrawMonitorID count_lock = NULL;
static int  class_count = 0;
static long total_bytes = 0;

static int is_jdk_class(const char *name) {
    return strncmp(name, "java/", 5) == 0 ||
           strncmp(name, "jdk/", 4) == 0 ||
           strncmp(name, "sun/", 4) == 0;
}

static void JNICALL cbClassFileLoadHook(
    jvmtiEnv *jvmti, JNIEnv *jni,
    jclass class_being_redefined, jobject loader,
    const char *name, jobject protection_domain,
    jint class_data_len, const unsigned char *class_data,
    jint *new_class_data_len, unsigned char **new_class_data)
{
    (*jvmti)->RawMonitorEnter(jvmti, count_lock);
    class_count++;
    total_bytes += class_data_len;
    (*jvmti)->RawMonitorExit(jvmti, count_lock);

    /* name can be NULL (e.g. for hidden classes) */
    if (name && !is_jdk_class(name)) {
        printf("[Agent] Loading: %-50s (%5d bytes)\n", name, (int)class_data_len);
        /* stdout is fully buffered when redirected — flush so agent
           output interleaves correctly with the application's */
        fflush(stdout);
    }

    /* We don't modify any bytecode in this example */
}

static void JNICALL cbVMDeath(jvmtiEnv *jvmti, JNIEnv *jni) {
    (*jvmti)->RawMonitorEnter(jvmti, count_lock);
    printf("\n[Agent] === Class Loading Summary ===\n");
    printf("[Agent] Total classes loaded: %d\n", class_count);
    printf("[Agent] Total bytecode size:  %ld bytes\n", total_bytes);
    fflush(stdout);
    (*jvmti)->RawMonitorExit(jvmti, count_lock);
    /* The monitor is deliberately not destroyed: other threads may
       still be inside the hook while the VM shuts down (§9.7). */
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    jvmtiEnv *jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;

    jvmtiError err = (*jvmti)->CreateRawMonitor(jvmti, "class_count_lock",
                                                 &count_lock);
    CHECK_JVMTI_ERROR(jvmti, err, "CreateRawMonitor");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    /* ClassFileLoadHook itself needs no capability; this one extends it
       to classes loaded during early startup and to retransformation. */
    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_generate_all_class_hook_events = 1;
    err = (*jvmti)->AddCapabilities(jvmti, &caps);
    CHECK_JVMTI_ERROR(jvmti, err, "AddCapabilities");

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.ClassFileLoadHook = &cbClassFileLoadHook;
    callbacks.VMDeath           = &cbVMDeath;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
              JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable ClassFileLoadHook");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
              JVMTI_EVENT_VM_DEATH, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable VMDeath");

    printf("[Agent] Class load monitor agent loaded (Lab 06)\n");
    return JNI_OK;
}
