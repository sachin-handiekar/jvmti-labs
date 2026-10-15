/**
 * Lab 05 — Heap Histogram and Stack Analysis Agent
 *
 * §5.8 "Heap Histogram Agent" example. At VMDeath it:
 *   - Tags every loaded class with its index (SetTag, §5.3) — the
 *     can_tag_objects capability is what makes the heap API available
 *   - Walks the heap once with IterateThroughHeap (§5.2). The JVM passes
 *     each object's *class* tag to the callback, so the walk can group
 *     objects by class without any per-object lookups
 *   - Prints totals and the top 10 classes by bytes — a heap histogram,
 *     the same view `jmap -histo` gives you
 *   - Dumps the stacks of the live threads with GetStackTrace, to pair
 *     "what is on the heap" with "what the threads are doing" (§5.7)
 *
 * Note: the older JVMTI 1.0 functions (IterateOverHeap,
 * IterateOverReachableObjects) are deprecated; IterateThroughHeap
 * visits each live object exactly once.
 *
 * Build:
 *   mkdir build && cd build && cmake .. && cmake --build .
 *
 * Run:
 *   javac HeapStackTestApp.java
 *   java -agentpath:./build/libheap_stack_agent.so HeapStackTestApp
 */

#include "jvmti_utils.h"
#include <stdlib.h>

#define LOG_FILE "heap_stack_agent.log"
#define TOP_N    10

/* ------------------------------------------------------------------ */
/*  Heap histogram via class tags + IterateThroughHeap (§5.2, §5.3)    */
/* ------------------------------------------------------------------ */

typedef struct {
    jint   class_count;   /* classes tagged 1..class_count */
    jlong *objects;       /* per class tag: object count   */
    jlong *bytes;         /* per class tag: shallow bytes  */
    jlong  total_objects;
    jlong  total_bytes;
} histogram_t;

/* Called once per live object, with every Java thread stopped — keep it
   fast and don't call JNI or most JVMTI functions here (§5.6). class_tag
   is the tag of the object's class: our index into the histogram. */
static jint JNICALL heap_iteration_cb(jlong class_tag, jlong size,
                                       jlong* tag_ptr, jint length,
                                       void* user_data) {
    histogram_t* h = (histogram_t*)user_data;
    h->total_objects++;
    h->total_bytes += size;
    if (class_tag > 0 && class_tag <= h->class_count) {
        h->objects[class_tag]++;
        h->bytes[class_tag] += size;
    }
    return JVMTI_VISIT_OBJECTS;
}

/* Walk the heap once, then print totals and the TOP_N classes by bytes */
static void walk_and_report(jvmtiEnv *jvmti, jclass *classes, histogram_t *h) {
    char buf[1024];
    jvmtiHeapCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.heap_iteration_callback = &heap_iteration_cb;

    jvmti_log(LOG_FILE, "[Heap] Walking the heap...");
    jvmtiError err = (*jvmti)->IterateThroughHeap(jvmti,
                                       0,      /* heap_filter: 0 = no filter */
                                       NULL,   /* klass: NULL = all classes */
                                       &callbacks,
                                       h);     /* user_data -> callback */
    CHECK_JVMTI_ERROR(jvmti, err, "IterateThroughHeap");
    if (err != JVMTI_ERROR_NONE) return;

    snprintf(buf, sizeof(buf),
             "[Heap] %lld objects, %lld bytes in %d classes",
             (long long)h->total_objects, (long long)h->total_bytes,
             (int)h->class_count);
    jvmti_log(LOG_FILE, buf);
    jvmti_log(LOG_FILE, "[Heap] Top classes by shallow size:");
    jvmti_log(LOG_FILE, "  rank     objects        bytes  class");

    /* Pick the TOP_N largest by bytes (simple selection; n is small) */
    for (int rank = 1; rank <= TOP_N; rank++) {
        jint best = 0;
        for (jint t = 1; t <= h->class_count; t++) {
            if (h->bytes[t] > 0 && (best == 0 || h->bytes[t] > h->bytes[best]))
                best = t;
        }
        if (best == 0) break;

        char *sig = NULL;
        (*jvmti)->GetClassSignature(jvmti, classes[best - 1], &sig, NULL);
        snprintf(buf, sizeof(buf), "  %4d  %10lld  %11lld  %s", rank,
                 (long long)h->objects[best], (long long)h->bytes[best],
                 sig ? sig : "?");
        jvmti_log(LOG_FILE, buf);
        if (sig) (*jvmti)->Deallocate(jvmti, (unsigned char*)sig);
        h->bytes[best] = -h->bytes[best];   /* mark as printed */
    }
}

static void dump_heap_histogram(jvmtiEnv *jvmti) {
    jint class_count = 0;
    jclass *classes = NULL;
    jvmtiError err = (*jvmti)->GetLoadedClasses(jvmti, &class_count, &classes);
    CHECK_JVMTI_ERROR(jvmti, err, "GetLoadedClasses");
    if (err != JVMTI_ERROR_NONE) return;

    /* Tag class i with i+1 (0 means "untagged") */
    for (jint i = 0; i < class_count; i++) {
        (*jvmti)->SetTag(jvmti, classes[i], (jlong)(i + 1));
    }

    histogram_t h;
    memset(&h, 0, sizeof(h));
    h.class_count = class_count;
    h.objects = calloc((size_t)class_count + 1, sizeof(jlong));
    h.bytes   = calloc((size_t)class_count + 1, sizeof(jlong));
    if (h.objects && h.bytes) walk_and_report(jvmti, classes, &h);

    free(h.objects);
    free(h.bytes);
    (*jvmti)->Deallocate(jvmti, (unsigned char*)classes);
}

/* ------------------------------------------------------------------ */
/*  Thread stacks (§5.7: pair heap data with what threads are doing)   */
/* ------------------------------------------------------------------ */

static void dump_all_thread_stacks(jvmtiEnv *jvmti) {
    jthread* threads = NULL;
    jint thread_count = 0;
    jvmtiError err;

    err = (*jvmti)->GetAllThreads(jvmti, &thread_count, &threads);
    if (err != JVMTI_ERROR_NONE || !threads) return;

    char buf[1024];
    snprintf(buf, sizeof(buf), "[Stacks] %d live threads:", (int)thread_count);
    jvmti_log(LOG_FILE, buf);

    for (int t = 0; t < thread_count; t++) {
        jvmtiThreadInfo tinfo;
        memset(&tinfo, 0, sizeof(tinfo));
        (*jvmti)->GetThreadInfo(jvmti, threads[t], &tinfo);

        snprintf(buf, sizeof(buf), "  Thread: %s",
                 tinfo.name ? tinfo.name : "<unnamed>");
        jvmti_log(LOG_FILE, buf);

        jvmtiFrameInfo frames[32];
        jint frame_count = 0;
        err = (*jvmti)->GetStackTrace(jvmti, threads[t],
                                       0, 32, frames, &frame_count);
        if (err == JVMTI_ERROR_NONE) {
            for (int f = 0; f < frame_count; f++) {
                char* method_name = NULL;
                char* class_sig = NULL;
                jclass decl_class;
                if ((*jvmti)->GetMethodDeclaringClass(jvmti, frames[f].method,
                                                       &decl_class) == JVMTI_ERROR_NONE) {
                    (*jvmti)->GetClassSignature(jvmti, decl_class,
                                                 &class_sig, NULL);
                }
                (*jvmti)->GetMethodName(jvmti, frames[f].method,
                                         &method_name, NULL, NULL);
                snprintf(buf, sizeof(buf), "    at %s.%s (bci=%lld)",
                         class_sig ? class_sig : "?",
                         method_name ? method_name : "?",
                         (long long)frames[f].location);
                jvmti_log(LOG_FILE, buf);

                if (method_name)
                    (*jvmti)->Deallocate(jvmti, (unsigned char*)method_name);
                if (class_sig)
                    (*jvmti)->Deallocate(jvmti, (unsigned char*)class_sig);
            }
        }

        if (tinfo.name)
            (*jvmti)->Deallocate(jvmti, (unsigned char*)tinfo.name);
    }

    (*jvmti)->Deallocate(jvmti, (unsigned char*)threads);
}

/* ------------------------------------------------------------------ */
/*  VMDeath — run the analysis while the app's data is still around    */
/* ------------------------------------------------------------------ */

static void JNICALL cbVMDeath(jvmtiEnv *jvmti, JNIEnv *jni) {
    dump_heap_histogram(jvmti);
    dump_all_thread_stacks(jvmti);
    jvmti_log(LOG_FILE, "[HeapStack] VMDeath — analysis complete.");
    printf("[Agent] Heap histogram and thread stacks written to %s\n", LOG_FILE);
}

/* ------------------------------------------------------------------ */
/*  Agent Lifecycle                                                    */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    printf("[Agent] heap_stack_agent loaded (Lab 05)\n");

    jvmtiEnv* jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;

    jvmtiError err;

    /* The heap iteration and tagging APIs require can_tag_objects */
    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_tag_objects = 1;
    err = (*jvmti)->AddCapabilities(jvmti, &caps);
    CHECK_JVMTI_ERROR(jvmti, err, "AddCapabilities");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.VMDeath = &cbVMDeath;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
              JVMTI_EVENT_VM_DEATH, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable VMDeath");

    jvmti_log(LOG_FILE, "[HeapStack] Agent loaded — heap/stack analysis ready.");
    return JNI_OK;
}
