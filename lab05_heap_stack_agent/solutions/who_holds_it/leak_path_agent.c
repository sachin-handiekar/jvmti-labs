/**
 * Lab 05 — Exercise solution: who keeps the leak alive?
 *
 * Exercise (Chapter 5): the heap histogram shows 20,000 CacheEntry
 * objects. Find out what holds them, by walking the reference graph with
 * FollowReferences (§5.4) one level at a time:
 *
 *   level 1: references TO CacheEntry objects    -> mark the referrers
 *   level 2: references TO the marked objects    -> mark their referrers
 *   level 3: references TO those                 -> ...
 *
 * Classes are tagged with positive ids (as in the histogram), so the
 * callback knows every object's class; marks use negative tags, so they
 * never collide with a class id. A static field is reported with the
 * class object as its referrer, whose tag is that class's id.
 *
 * Unlike IterateThroughHeap, FollowReferences only visits objects that
 * are reachable from GC roots — garbage never shows up here.
 *
 * Run:  java -agentpath:./build/libleak_path_agent.so HeapStackTestApp
 */

#include "jvmti_utils.h"
#include <stdlib.h>

#define TARGET_SIG "LHeapStackTestApp$CacheEntry;"
#define LEVELS     3

typedef struct {
    jlong   target_tag;    /* level 1: class id; later: a negative mark */
    int     by_class;      /* match the referree by class (level 1) or by its own tag */
    jlong   mark;          /* tag to put on referrers (0 = don't mark) */
    jint    class_count;
    long   *refs_by_class; /* references counted per referrer class id */
    jvmtiHeapReferenceKind *kind_by_class;  /* ...and how they refer */
    long    marked;        /* distinct referrer objects marked */
} pass_t;

static jint JNICALL reference_cb(jvmtiHeapReferenceKind reference_kind,
                                 const jvmtiHeapReferenceInfo *reference_info,
                                 jlong class_tag, jlong referrer_class_tag,
                                 jlong size, jlong *tag_ptr,
                                 jlong *referrer_tag_ptr, jint length,
                                 void *user_data) {
    pass_t *p = (pass_t *)user_data;
    int is_target = p->by_class ? class_tag == p->target_tag
                                : *tag_ptr == p->target_tag;
    if (!is_target) return JVMTI_VISIT_OBJECTS;

    /* For a static field the referrer is the class object itself: its
       own tag is its class id. Otherwise use the referrer's class. */
    jlong who = reference_kind == JVMTI_HEAP_REFERENCE_STATIC_FIELD && referrer_tag_ptr
                    ? *referrer_tag_ptr : referrer_class_tag;
    if (who <= 0 || who > p->class_count) who = 0;   /* 0 = a GC root */
    p->refs_by_class[who]++;
    p->kind_by_class[who] = reference_kind;

    if (p->mark && referrer_tag_ptr && *referrer_tag_ptr == 0) {
        *referrer_tag_ptr = p->mark;
        p->marked++;
    }
    return JVMTI_VISIT_OBJECTS;
}

static const char *kind_name(jvmtiHeapReferenceKind k) {
    switch (k) {
    case JVMTI_HEAP_REFERENCE_ARRAY_ELEMENT: return "array element";
    case JVMTI_HEAP_REFERENCE_FIELD:         return "instance field";
    case JVMTI_HEAP_REFERENCE_STATIC_FIELD:  return "static field";
    case JVMTI_HEAP_REFERENCE_STACK_LOCAL:   return "local variable";
    case JVMTI_HEAP_REFERENCE_JNI_GLOBAL:
    case JVMTI_HEAP_REFERENCE_JNI_LOCAL:     return "JNI reference";
    default:                                 return "other";
    }
}

static void JNICALL cbVMDeath(jvmtiEnv *jvmti, JNIEnv *jni) {
    jint class_count = 0;
    jclass *classes = NULL;
    if ((*jvmti)->GetLoadedClasses(jvmti, &class_count, &classes) != JVMTI_ERROR_NONE) return;

    jlong target_class = 0;
    for (jint i = 0; i < class_count; i++) {
        (*jvmti)->SetTag(jvmti, classes[i], (jlong)(i + 1));
        char *sig = NULL;
        if ((*jvmti)->GetClassSignature(jvmti, classes[i], &sig, NULL) == JVMTI_ERROR_NONE) {
            if (strcmp(sig, TARGET_SIG) == 0) target_class = i + 1;
            (*jvmti)->Deallocate(jvmti, (unsigned char*)sig);
        }
    }
    if (!target_class) {
        printf("[Leak] %s is not loaded\n", TARGET_SIG);
        (*jvmti)->Deallocate(jvmti, (unsigned char*)classes);
        return;
    }

    jvmtiHeapCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.heap_reference_callback = &reference_cb;

    printf("[Leak] What keeps %s objects alive:\n", TARGET_SIG);
    jlong target = target_class;
    for (int level = 1; level <= LEVELS; level++) {
        pass_t p;
        memset(&p, 0, sizeof(p));
        p.target_tag  = target;
        p.by_class    = level == 1;
        p.mark        = level < LEVELS ? -level : 0;
        p.class_count = class_count;
        p.refs_by_class = calloc((size_t)class_count + 1, sizeof(long));
        p.kind_by_class = calloc((size_t)class_count + 1, sizeof(jvmtiHeapReferenceKind));
        if (!p.refs_by_class || !p.kind_by_class) {
            free(p.refs_by_class);
            free(p.kind_by_class);
            break;
        }

        jvmtiError err = (*jvmti)->FollowReferences(jvmti, 0, NULL, NULL, &callbacks, &p);
        CHECK_JVMTI_ERROR(jvmti, err, "FollowReferences");

        for (jint c = 0; c <= class_count; c++) {
            if (p.refs_by_class[c] == 0) continue;
            char *sig = NULL;
            if (c > 0) (*jvmti)->GetClassSignature(jvmti, classes[c - 1], &sig, NULL);
            printf("[Leak]   level %d: %ld reference(s) from %s (%s)\n", level,
                   p.refs_by_class[c], c > 0 && sig ? sig : "a GC root",
                   kind_name(p.kind_by_class[c]));
            if (sig) (*jvmti)->Deallocate(jvmti, (unsigned char*)sig);
        }
        free(p.refs_by_class);
        free(p.kind_by_class);
        if (p.marked == 0) break;   /* reached the roots */
        target = p.mark;
    }
    (*jvmti)->Deallocate(jvmti, (unsigned char*)classes);
    fflush(stdout);
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    jvmtiEnv *jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;

    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_tag_objects = 1;
    jvmtiError err = (*jvmti)->AddCapabilities(jvmti, &caps);
    CHECK_JVMTI_ERROR(jvmti, err, "AddCapabilities");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.VMDeath = &cbVMDeath;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable VMDeath");
    return err == JVMTI_ERROR_NONE ? JNI_OK : JNI_ERR;
}
