/**
 * Lab 13 — Exercise solution: WHERE is it allocated?
 *
 * Exercise (Chapter 13): the allocation tracker says *what* is allocated
 * ([B, String...). Fixing it needs *where*. In the SampledObjectAlloc
 * callback — which runs on the allocating thread — call GetStackTrace,
 * skip JDK frames (java/, jdk/, sun/: the String constructor, HashMap.put
 * ...) and charge the sample to the first application frame.
 *
 * Grouping by that frame turns "lots of byte[]" into "smallAllocations
 * allocates most of it", which is something you can go and change.
 *
 * Run:  java -agentpath:./build/liballoc_sites.so AllocTestApp
 */

#include "jvmti_utils.h"

#define MAX_SITES   64
#define MAX_FRAMES  16
#define SAMPLING_INTERVAL_BYTES (64 * 1024)

typedef struct {
    jmethodID method;     /* the allocating application method */
    long      samples;
    long      bytes;
} site_t;

static site_t sites[MAX_SITES];
static int site_count = 0;
static long jdk_only = 0;        /* samples with no application frame */
static jrawMonitorID lock;

static int is_jdk_class(const char *sig) {
    return strncmp(sig, "Ljava/", 6) == 0 || strncmp(sig, "Ljdk/", 5) == 0 ||
           strncmp(sig, "Lsun/", 5) == 0;
}

static void JNICALL cbSampledObjectAlloc(jvmtiEnv *jvmti, JNIEnv *jni,
                                         jthread thread, jobject object,
                                         jclass object_klass, jlong size) {
    jvmtiFrameInfo frames[MAX_FRAMES];
    jint count = 0;
    if ((*jvmti)->GetStackTrace(jvmti, thread, 0, MAX_FRAMES, frames, &count) != JVMTI_ERROR_NONE)
        return;

    /* First frame (innermost first) that belongs to the application */
    jmethodID site = NULL;
    for (jint i = 0; i < count && !site; i++) {
        jclass cls;
        char *sig = NULL;
        if ((*jvmti)->GetMethodDeclaringClass(jvmti, frames[i].method, &cls) == JVMTI_ERROR_NONE &&
            (*jvmti)->GetClassSignature(jvmti, cls, &sig, NULL) == JVMTI_ERROR_NONE) {
            if (!is_jdk_class(sig)) site = frames[i].method;
            (*jvmti)->Deallocate(jvmti, (unsigned char*)sig);
        }
    }

    (*jvmti)->RawMonitorEnter(jvmti, lock);
    if (!site) {
        jdk_only++;
    } else {
        int k = 0;
        while (k < site_count && sites[k].method != site) k++;
        if (k == site_count && site_count < MAX_SITES) {
            sites[k].method = site;
            site_count++;
        }
        if (k < site_count) {
            sites[k].samples++;
            sites[k].bytes += (long)size;
        }
    }
    (*jvmti)->RawMonitorExit(jvmti, lock);
}

static void JNICALL cbVMDeath(jvmtiEnv *jvmti, JNIEnv *jni) {
    (*jvmti)->RawMonitorEnter(jvmti, lock);
    long total = jdk_only;
    for (int k = 0; k < site_count; k++) total += sites[k].samples;

    printf("[Sites] Allocation sites (%ld samples):\n", total);
    for (int rank = 1; rank <= 5; rank++) {
        int best = -1;
        for (int k = 0; k < site_count; k++)
            if (sites[k].samples > 0 && (best < 0 || sites[k].samples > sites[best].samples))
                best = k;
        if (best < 0) break;

        char *name = NULL, *csig = NULL;
        jclass cls;
        (*jvmti)->GetMethodName(jvmti, sites[best].method, &name, NULL, NULL);
        if ((*jvmti)->GetMethodDeclaringClass(jvmti, sites[best].method, &cls) == JVMTI_ERROR_NONE)
            (*jvmti)->GetClassSignature(jvmti, cls, &csig, NULL);
        size_t clen = csig ? strlen(csig) : 0;
        printf("[Sites]   %d. %.*s.%-22s %4ld samples (%5.1f%%), %8ld bytes\n", rank,
               clen > 2 ? (int)(clen - 2) : 1, clen > 2 ? csig + 1 : "?",
               name ? name : "?", sites[best].samples,
               total ? 100.0 * (double)sites[best].samples / (double)total : 0.0,
               sites[best].bytes);
        if (name) (*jvmti)->Deallocate(jvmti, (unsigned char*)name);
        if (csig) (*jvmti)->Deallocate(jvmti, (unsigned char*)csig);
        sites[best].samples = -sites[best].samples;   /* mark as printed */
    }
    printf("[Sites]   (JDK-internal only: %ld samples)\n", jdk_only);
    (*jvmti)->RawMonitorExit(jvmti, lock);
    fflush(stdout);
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    jvmtiEnv *jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;

    jvmtiError err = (*jvmti)->CreateRawMonitor(jvmti, "sites_lock", &lock);
    CHECK_JVMTI_ERROR(jvmti, err, "CreateRawMonitor");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_generate_sampled_object_alloc_events = 1;
    err = (*jvmti)->AddCapabilities(jvmti, &caps);
    CHECK_JVMTI_ERROR(jvmti, err, "AddCapabilities");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    err = (*jvmti)->SetHeapSamplingInterval(jvmti, SAMPLING_INTERVAL_BYTES);
    CHECK_JVMTI_ERROR(jvmti, err, "SetHeapSamplingInterval");

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.SampledObjectAlloc = &cbSampledObjectAlloc;
    callbacks.VMDeath            = &cbVMDeath;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
              JVMTI_EVENT_SAMPLED_OBJECT_ALLOC, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable SampledObjectAlloc");
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable VMDeath");
    return JNI_OK;
}
