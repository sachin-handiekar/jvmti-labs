/**
 * Lab 01 — Exercise solution: capabilities at startup vs. once running
 *
 * Exercise (Chapter 1): print EVERY potential capability, twice — in
 * Agent_OnLoad, and again once the VM is live (VMInit) — and list the
 * ones that disappear. This is why an agent attached to a running JVM
 * (Chapter 11) can't always get what a startup agent can.
 *
 * The list is the 44 capabilities in JDK 17's jvmti.h, so it builds on
 * every supported JDK. (JDK 21 adds can_support_virtual_threads.)
 *
 * Run:  java -agentpath:./build/libcapabilities_by_phase.so -version
 */

#include "jvmti_utils.h"

/* X-macro: one entry per capability field in jvmtiCapabilities */
#define CAPABILITIES(X) \
    X(can_tag_objects) \
    X(can_generate_field_modification_events) \
    X(can_generate_field_access_events) \
    X(can_get_bytecodes) \
    X(can_get_synthetic_attribute) \
    X(can_get_owned_monitor_info) \
    X(can_get_current_contended_monitor) \
    X(can_get_monitor_info) \
    X(can_pop_frame) \
    X(can_redefine_classes) \
    X(can_signal_thread) \
    X(can_get_source_file_name) \
    X(can_get_line_numbers) \
    X(can_get_source_debug_extension) \
    X(can_access_local_variables) \
    X(can_maintain_original_method_order) \
    X(can_generate_single_step_events) \
    X(can_generate_exception_events) \
    X(can_generate_frame_pop_events) \
    X(can_generate_breakpoint_events) \
    X(can_suspend) \
    X(can_redefine_any_class) \
    X(can_get_current_thread_cpu_time) \
    X(can_get_thread_cpu_time) \
    X(can_generate_method_entry_events) \
    X(can_generate_method_exit_events) \
    X(can_generate_all_class_hook_events) \
    X(can_generate_compiled_method_load_events) \
    X(can_generate_monitor_events) \
    X(can_generate_vm_object_alloc_events) \
    X(can_generate_native_method_bind_events) \
    X(can_generate_garbage_collection_events) \
    X(can_generate_object_free_events) \
    X(can_force_early_return) \
    X(can_get_owned_monitor_stack_depth_info) \
    X(can_get_constant_pool) \
    X(can_set_native_method_prefix) \
    X(can_retransform_classes) \
    X(can_retransform_any_class) \
    X(can_generate_resource_exhaustion_heap_events) \
    X(can_generate_resource_exhaustion_threads_events) \
    X(can_generate_early_vmstart) \
    X(can_generate_early_class_hook_events) \
    X(can_generate_sampled_object_alloc_events)

static jvmtiCapabilities at_onload;

static void JNICALL cbVMInit(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread) {
    jvmtiCapabilities live;
    memset(&live, 0, sizeof(live));
    (*jvmti)->GetPotentialCapabilities(jvmti, &live);

    int total = 0, onload = 0, still_live = 0;
    printf("%-50s %-8s %s\n", "capability", "OnLoad", "Live");
#define PRINT_ROW(name) \
    total++; \
    onload += at_onload.name; \
    still_live += live.name; \
    printf("%-50s %-8s %s\n", #name, at_onload.name ? "yes" : "-", live.name ? "yes" : "-");
    CAPABILITIES(PRINT_ROW)
#undef PRINT_ROW

    printf("\n%d capabilities: %d available at startup, %d once the VM is live\n",
           total, onload, still_live);
    printf("Only at startup (Agent_OnLoad):\n");
#define PRINT_LOST(name) \
    if (at_onload.name && !live.name) printf("  %s\n", #name);
    CAPABILITIES(PRINT_LOST)
#undef PRINT_LOST
    fflush(stdout);
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    jvmtiEnv *jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;

    /* Snapshot what's possible during the OnLoad phase */
    memset(&at_onload, 0, sizeof(at_onload));
    jvmtiError err = (*jvmti)->GetPotentialCapabilities(jvmti, &at_onload);
    CHECK_JVMTI_ERROR(jvmti, err, "GetPotentialCapabilities");

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.VMInit = &cbVMInit;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
              JVMTI_EVENT_VM_INIT, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable VMInit");
    return err == JVMTI_ERROR_NONE ? JNI_OK : JNI_ERR;
}
