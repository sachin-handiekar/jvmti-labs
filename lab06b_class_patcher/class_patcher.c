/**
 * Lab 06b — Class Patcher: rewriting bytecode with ClassFileLoadHook
 *
 * §6.8 "Rewriting a Class" example. Changes what a running Java program
 * prints — without touching its source — by rewriting a string literal
 * inside the class file, in plain C with no bytecode library:
 *
 *   - Parses the class file's constant pool (§6.4) and finds the
 *     CONSTANT_Utf8 entry that a CONSTANT_String (a Java string literal)
 *     points to
 *   - Builds a new class file with that one entry replaced. Constant-pool
 *     *indices* don't change, so nothing else in the class needs fixing
 *   - Hands the new bytes back from ClassFileLoadHook in a buffer from
 *     jvmti->Allocate (never malloc — the JVM frees it)
 *
 * Two ways to run it, sharing the same hook:
 *   - At startup (Agent_OnLoad): the class is patched as it loads
 *   - Attached to a running JVM (Agent_OnAttach): the class is already
 *     loaded, so the agent calls RetransformClasses (§6.6), which runs
 *     the hook again on the class's original bytes. The program's output
 *     changes mid-run.
 *
 * Build:
 *   mkdir build && cd build && cmake .. && cmake --build .
 *
 * Run (load time):
 *   javac PatchTestApp.java
 *   java -agentpath:./build/libclass_patcher.so PatchTestApp
 *
 * Run (attach + retransform — see README):
 *   java -XX:+EnableDynamicAgentLoading PatchTestApp --loop 10
 *   java AgentLoader <pid> /abs/path/build/libclass_patcher.so
 */

#include "jvmti_utils.h"
#include <stdlib.h>

/* What to patch. Kept as constants so the example stays focused on the
   class-file mechanics; an agent option would work just as well. */
#define TARGET_CLASS "PatchTestApp"
#define FROM_TEXT    "Hello from the original class file"
#define TO_TEXT      "Hello from JVMTI: this class was patched in memory"

/* Constant-pool tags (JVM Specification §4.4) */
enum {
    CP_UTF8 = 1, CP_INTEGER = 3, CP_FLOAT = 4, CP_LONG = 5, CP_DOUBLE = 6,
    CP_CLASS = 7, CP_STRING = 8, CP_FIELDREF = 9, CP_METHODREF = 10,
    CP_INTERFACE_METHODREF = 11, CP_NAME_AND_TYPE = 12,
    CP_METHOD_HANDLE = 15, CP_METHOD_TYPE = 16, CP_DYNAMIC = 17,
    CP_INVOKE_DYNAMIC = 18, CP_MODULE = 19, CP_PACKAGE = 20
};

static unsigned read_u2(const unsigned char *p) {
    return ((unsigned)p[0] << 8) | p[1];
}

/* Size in bytes of the constant-pool entry at p (tag included), or 0 for
   an unknown tag. CONSTANT_Long and CONSTANT_Double also take up TWO
   pool slots — a famous quirk of the format the caller must handle. */
static size_t cp_entry_size(const unsigned char *p, size_t remaining) {
    switch (p[0]) {
    case CP_UTF8:
        return remaining >= 3 ? 3 + read_u2(p + 1) : 0;
    case CP_CLASS: case CP_STRING: case CP_METHOD_TYPE:
    case CP_MODULE: case CP_PACKAGE:
        return 3;
    case CP_METHOD_HANDLE:
        return 4;
    case CP_INTEGER: case CP_FLOAT: case CP_FIELDREF: case CP_METHODREF:
    case CP_INTERFACE_METHODREF: case CP_NAME_AND_TYPE: case CP_DYNAMIC:
    case CP_INVOKE_DYNAMIC:
        return 5;
    case CP_LONG: case CP_DOUBLE:
        return 9;
    default:
        return 0;
    }
}

/**
 * Rewrites the string literal `from` to `to` in a class file. Returns 1
 * and sets *out / *out_len (allocated with jvmti->Allocate) on success,
 * 0 if the literal isn't there or the class file looks malformed — in
 * which case the class must be left untouched.
 */
static int patch_string_literal(jvmtiEnv *jvmti,
                                const unsigned char *data, jint len,
                                const char *from, const char *to,
                                unsigned char **out, jint *out_len) {
    const size_t n = (size_t)len;
    if (n < 10 || read_u2(data) != 0xCAFE || read_u2(data + 2) != 0xBABE)
        return 0;

    const unsigned cp_count = read_u2(data + 8);   /* entries 1..count-1 */
    size_t *offset = calloc(cp_count, sizeof(size_t));
    if (!offset) return 0;

    /* Pass 1: record where every entry starts, and which Utf8 entry the
       literal lives in. A CONSTANT_String holds only the INDEX of a Utf8
       entry, so string literals and names (class, method, field names)
       are all stored the same way; we look for a Utf8 whose text matches
       AND that some CONSTANT_String refers to. */
    size_t pos = 10, from_len = strlen(from);
    unsigned match = 0, string_refs_match = 0;
    for (unsigned i = 1; i < cp_count; i++) {
        if (pos >= n) goto malformed;
        size_t size = cp_entry_size(data + pos, n - pos);
        if (size == 0 || pos + size > n) goto malformed;
        offset[i] = pos;
        if (data[pos] == CP_UTF8 && size - 3 == from_len &&
            memcmp(data + pos + 3, from, from_len) == 0)
            match = i;
        if (data[pos] == CP_LONG || data[pos] == CP_DOUBLE)
            i++;                       /* 8-byte constants use two slots */
        pos += size;
    }
    for (unsigned i = 1; match && i < cp_count; i++) {
        if (offset[i] && data[offset[i]] == CP_STRING &&
            read_u2(data + offset[i] + 1) == match)
            string_refs_match = 1;
    }
    if (!match || !string_refs_match) { free(offset); return 0; }

    /* Pass 2: new class file = everything before the entry, the new
       entry, everything after it. Indices are unchanged, so the bytecode
       (`ldc #n`) still points at the right constant. */
    size_t entry = offset[match], old_size = 3 + from_len;
    size_t to_len = strlen(to);
    size_t new_len = n - old_size + 3 + to_len;
    free(offset);
    if (to_len > 0xFFFF || new_len > 0x7FFFFFFF) return 0;

    unsigned char *buf = NULL;
    if ((*jvmti)->Allocate(jvmti, (jlong)new_len, &buf) != JVMTI_ERROR_NONE)
        return 0;
    memcpy(buf, data, entry);
    buf[entry]     = CP_UTF8;
    buf[entry + 1] = (unsigned char)(to_len >> 8);
    buf[entry + 2] = (unsigned char)(to_len & 0xFF);
    memcpy(buf + entry + 3, to, to_len);
    memcpy(buf + entry + 3 + to_len, data + entry + old_size,
           n - entry - old_size);

    *out = buf;
    *out_len = (jint)new_len;
    return 1;

malformed:
    free(offset);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  ClassFileLoadHook — called for loading AND for retransformation    */
/* ------------------------------------------------------------------ */

static void JNICALL cbClassFileLoadHook(
    jvmtiEnv *jvmti, JNIEnv *jni,
    jclass class_being_redefined, jobject loader,
    const char *name, jobject protection_domain,
    jint class_data_len, const unsigned char *class_data,
    jint *new_class_data_len, unsigned char **new_class_data)
{
    if (name == NULL || strcmp(name, TARGET_CLASS) != 0) return;

    unsigned char *patched = NULL;
    jint patched_len = 0;
    if (patch_string_literal(jvmti, class_data, class_data_len,
                             FROM_TEXT, TO_TEXT, &patched, &patched_len)) {
        *new_class_data = patched;          /* the JVM takes ownership */
        *new_class_data_len = patched_len;
        printf("[Patcher] %s %s: \"%s\" -> \"%s\" (%d -> %d bytes)\n",
               class_being_redefined ? "Retransformed" : "Patched",
               name, FROM_TEXT, TO_TEXT, (int)class_data_len, (int)patched_len);
    } else {
        printf("[Patcher] %s: literal not found, class left unchanged\n", name);
    }
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/*  Shared setup                                                       */
/* ------------------------------------------------------------------ */

static jvmtiEnv *setup(JavaVM *vm, int need_retransform) {
    jvmtiEnv *jvmti = jvmti_get_env(vm);
    if (!jvmti) return NULL;

    /* Load-time patching needs no capability at all. Retransforming an
       already-loaded class needs can_retransform_classes — and the hook
       is only called for retransformation in environments that hold it. */
    if (need_retransform) {
        jvmtiCapabilities caps;
        memset(&caps, 0, sizeof(caps));
        caps.can_retransform_classes = 1;
        jvmtiError err = (*jvmti)->AddCapabilities(jvmti, &caps);
        CHECK_JVMTI_ERROR(jvmti, err, "AddCapabilities(can_retransform_classes)");
        if (err != JVMTI_ERROR_NONE) return NULL;
    }

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.ClassFileLoadHook = &cbClassFileLoadHook;
    jvmtiError err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    if (err != JVMTI_ERROR_NONE) return NULL;

    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
              JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable ClassFileLoadHook");
    return err == JVMTI_ERROR_NONE ? jvmti : NULL;
}

/* At startup: the target hasn't loaded yet, so the hook will patch it
   when it does. */
JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    printf("[Patcher] Loaded at startup; will patch %s as it loads\n", TARGET_CLASS);
    return setup(vm, 0) ? JNI_OK : JNI_ERR;
}

/* Attached to a running JVM: the target is probably loaded already —
   find it and ask the JVM to retransform it, which runs our hook on the
   class's original bytes and swaps in the result. */
JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM *vm, char *options, void *reserved) {
    printf("[Patcher] Attached to running JVM; retransforming %s\n", TARGET_CLASS);
    fflush(stdout);
    jvmtiEnv *jvmti = setup(vm, 1);
    if (!jvmti) return JNI_ERR;

    jint count = 0;
    jclass *classes = NULL;
    jvmtiError err = (*jvmti)->GetLoadedClasses(jvmti, &count, &classes);
    CHECK_JVMTI_ERROR(jvmti, err, "GetLoadedClasses");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    jclass target = NULL;
    for (jint i = 0; i < count && !target; i++) {
        char *sig = NULL;
        if ((*jvmti)->GetClassSignature(jvmti, classes[i], &sig, NULL) == JVMTI_ERROR_NONE) {
            if (strcmp(sig, "L" TARGET_CLASS ";") == 0) target = classes[i];
            (*jvmti)->Deallocate(jvmti, (unsigned char*)sig);
        }
    }

    if (target) {
        err = (*jvmti)->RetransformClasses(jvmti, 1, &target);
        CHECK_JVMTI_ERROR(jvmti, err, "RetransformClasses");
    } else {
        printf("[Patcher] %s is not loaded yet; it will be patched when it loads\n",
               TARGET_CLASS);
    }
    (*jvmti)->Deallocate(jvmti, (unsigned char*)classes);
    return JNI_OK;
}
