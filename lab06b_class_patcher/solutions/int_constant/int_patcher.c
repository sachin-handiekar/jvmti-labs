/**
 * Lab 06b — Exercise solution: patch a number instead of a string
 *
 * Exercise (Chapter 6): change a numeric constant instead of a string
 * literal. A CONSTANT_Integer entry is always 5 bytes (tag + 4-byte
 * big-endian value), so the new class file is the same size: copy the
 * bytes and overwrite four of them — no rebuilding needed.
 *
 * One catch: javac only puts an int in the constant pool if it doesn't
 * fit a 16-bit immediate. Small values (-32768..32767) are encoded in
 * the instruction itself (iconst_*, bipush, sipush) and can't be found
 * this way. That's why IntPatchTestApp uses 100000.
 *
 * Run:  java -agentpath:./build/libint_patcher.so IntPatchTestApp
 */

#include "jvmti_utils.h"
#include <stdlib.h>

#define TARGET_CLASS "IntPatchTestApp"
#define FROM_VALUE   100000
#define TO_VALUE     250000

enum {
    CP_UTF8 = 1, CP_INTEGER = 3, CP_FLOAT = 4, CP_LONG = 5, CP_DOUBLE = 6,
    CP_CLASS = 7, CP_STRING = 8, CP_FIELDREF = 9, CP_METHODREF = 10,
    CP_INTERFACE_METHODREF = 11, CP_NAME_AND_TYPE = 12,
    CP_METHOD_HANDLE = 15, CP_METHOD_TYPE = 16, CP_DYNAMIC = 17,
    CP_INVOKE_DYNAMIC = 18, CP_MODULE = 19, CP_PACKAGE = 20
};

static unsigned read_u2(const unsigned char *p) { return ((unsigned)p[0] << 8) | p[1]; }

static long read_s4(const unsigned char *p) {
    unsigned long v = ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
                      ((unsigned long)p[2] << 8)  |  (unsigned long)p[3];
    return v >= 0x80000000UL ? (long)(v - 0x100000000UL) : (long)v;
}

static void write_s4(unsigned char *p, long value) {
    unsigned long v = (unsigned long)value & 0xFFFFFFFFUL;
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

/* Same as lab 6b: entry size from its tag, 0 if unknown */
static size_t cp_entry_size(const unsigned char *p, size_t remaining) {
    switch (p[0]) {
    case CP_UTF8:  return remaining >= 3 ? 3 + read_u2(p + 1) : 0;
    case CP_CLASS: case CP_STRING: case CP_METHOD_TYPE:
    case CP_MODULE: case CP_PACKAGE:                         return 3;
    case CP_METHOD_HANDLE:                                   return 4;
    case CP_INTEGER: case CP_FLOAT: case CP_FIELDREF: case CP_METHODREF:
    case CP_INTERFACE_METHODREF: case CP_NAME_AND_TYPE: case CP_DYNAMIC:
    case CP_INVOKE_DYNAMIC:                                  return 5;
    case CP_LONG: case CP_DOUBLE:                            return 9;
    default:                                                 return 0;
    }
}

static void JNICALL cbClassFileLoadHook(
    jvmtiEnv *jvmti, JNIEnv *jni, jclass class_being_redefined, jobject loader,
    const char *name, jobject protection_domain,
    jint class_data_len, const unsigned char *class_data,
    jint *new_class_data_len, unsigned char **new_class_data)
{
    if (name == NULL || strcmp(name, TARGET_CLASS) != 0) return;
    const size_t n = (size_t)class_data_len;
    if (n < 10 || read_u2(class_data) != 0xCAFE) return;

    /* Find the CONSTANT_Integer entry holding FROM_VALUE */
    unsigned cp_count = read_u2(class_data + 8);
    size_t pos = 10, found = 0;
    for (unsigned i = 1; i < cp_count && pos < n; i++) {
        size_t size = cp_entry_size(class_data + pos, n - pos);
        if (size == 0 || pos + size > n) return;          /* malformed: leave it */
        if (class_data[pos] == CP_INTEGER && read_s4(class_data + pos + 1) == FROM_VALUE)
            found = pos;
        if (class_data[pos] == CP_LONG || class_data[pos] == CP_DOUBLE) i++;
        pos += size;
    }
    if (!found) {
        printf("[IntPatcher] %d is not in the constant pool of %s\n", FROM_VALUE, name);
        return;
    }

    /* Same size: copy the class file and overwrite the 4 value bytes */
    unsigned char *buf = NULL;
    if ((*jvmti)->Allocate(jvmti, class_data_len, &buf) != JVMTI_ERROR_NONE) return;
    memcpy(buf, class_data, n);
    write_s4(buf + found + 1, TO_VALUE);
    *new_class_data = buf;
    *new_class_data_len = class_data_len;
    printf("[IntPatcher] Patched %s: %d -> %d (class file still %d bytes)\n",
           name, FROM_VALUE, TO_VALUE, (int)class_data_len);
    fflush(stdout);
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    jvmtiEnv *jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.ClassFileLoadHook = &cbClassFileLoadHook;
    jvmtiError err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
              JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable ClassFileLoadHook");
    return err == JVMTI_ERROR_NONE ? JNI_OK : JNI_ERR;
}
