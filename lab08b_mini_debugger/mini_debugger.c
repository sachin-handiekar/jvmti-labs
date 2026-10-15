/**
 * Lab 08b — A Mini Debugger
 *
 * §8.6 example. The debugging half of JVMTI — what IntelliJ, Eclipse and
 * jdb use underneath (via the JDK's JDWP agent) — in one small agent:
 *
 *   - Deferred breakpoint: the target class isn't loaded at startup, so
 *     the agent waits for ClassPrepare, maps a SOURCE LINE to a bytecode
 *     location with GetLineNumberTable, and calls SetBreakpoint
 *   - When the breakpoint hits: print every local variable in scope,
 *     using GetLocalVariableTable and GetLocalInt / GetLocalObject
 *   - "Step over" to the end of the method: SingleStep events, enabled
 *     for that ONE thread only, reported once per new source line
 *   - Field watchpoint: SetFieldModificationWatch reports every write to
 *     a field, with the old and new value and where it happened
 *
 * Compile the test app with `javac -g` — breakpoints by line and local
 * variable names need the debug tables that -g adds.
 *
 * Build:
 *   mkdir build && cd build && cmake .. && cmake --build .
 *
 * Run:
 *   javac -g DebugTestApp.java
 *   java -agentpath:./build/libmini_debugger.so DebugTestApp
 */

#include "jvmti_utils.h"
#include <stdlib.h>

/* What to debug: a breakpoint at a source line, and a field to watch */
#define TARGET_CLASS_SIG "LDebugTestApp;"
#define BREAK_METHOD     "checkout"
#define BREAK_LINE       21
#define WATCH_FIELD      "balance"

/* Stepping state. The test app is single-threaded; a real debugger
   would keep this per thread. */
static jmethodID step_method = NULL;    /* method we're stepping through */
static jint      step_depth  = 0;       /* its frame depth at the breakpoint */
static jint      step_line   = -1;      /* last line reported */

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/* Source line for a bytecode location, or -1 (needs javac -g) */
static jint line_of(jvmtiEnv *jvmti, jmethodID method, jlocation loc) {
    jint count = 0, line = -1;
    jvmtiLineNumberEntry *table = NULL;
    if ((*jvmti)->GetLineNumberTable(jvmti, method, &count, &table) != JVMTI_ERROR_NONE)
        return -1;
    jlocation best = -1;
    for (jint i = 0; i < count; i++) {   /* closest entry at or before loc */
        if (table[i].start_location <= loc && table[i].start_location >= best) {
            best = table[i].start_location;
            line = table[i].line_number;
        }
    }
    (*jvmti)->Deallocate(jvmti, (unsigned char*)table);
    return line;
}

/* First bytecode location of a source line, or -1 */
static jlocation location_of_line(jvmtiEnv *jvmti, jmethodID method, jint line) {
    jint count = 0;
    jvmtiLineNumberEntry *table = NULL;
    jlocation loc = -1;
    if ((*jvmti)->GetLineNumberTable(jvmti, method, &count, &table) != JVMTI_ERROR_NONE)
        return -1;
    for (jint i = 0; i < count; i++) {
        if (table[i].line_number == line &&
            (loc == -1 || table[i].start_location < loc))
            loc = table[i].start_location;
    }
    (*jvmti)->Deallocate(jvmti, (unsigned char*)table);
    return loc;
}

/* "Class.method" for printing */
static void method_label(jvmtiEnv *jvmti, jmethodID method, char *out, size_t n) {
    char *name = NULL, *csig = NULL;
    jclass cls;
    (*jvmti)->GetMethodName(jvmti, method, &name, NULL, NULL);
    if ((*jvmti)->GetMethodDeclaringClass(jvmti, method, &cls) == JVMTI_ERROR_NONE)
        (*jvmti)->GetClassSignature(jvmti, cls, &csig, NULL);
    /* "LDebugTestApp;" -> "DebugTestApp" */
    const char *c = csig ? csig : "?";
    size_t clen = strlen(c);
    if (csig && c[0] == 'L' && clen > 2) { c++; clen -= 2; }
    snprintf(out, n, "%.*s.%s", (int)clen, c, name ? name : "?");
    if (name) (*jvmti)->Deallocate(jvmti, (unsigned char*)name);
    if (csig) (*jvmti)->Deallocate(jvmti, (unsigned char*)csig);
}

/* Print one local variable's value, chosen by its type signature */
static void print_local(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread,
                        const jvmtiLocalVariableEntry *var) {
    const char *sig = var->signature;
    jvmtiError err = JVMTI_ERROR_NONE;
    switch (sig[0]) {
    case 'I': case 'S': case 'B': case 'C': case 'Z': {
        jint v;
        err = (*jvmti)->GetLocalInt(jvmti, thread, 0, var->slot, &v);
        if (err == JVMTI_ERROR_NONE)
            printf("        %-9s %-6s = %d\n", var->name, sig, (int)v);
        break;
    }
    case 'J': {
        jlong v;
        err = (*jvmti)->GetLocalLong(jvmti, thread, 0, var->slot, &v);
        if (err == JVMTI_ERROR_NONE)
            printf("        %-9s %-6s = %lld\n", var->name, sig, (long long)v);
        break;
    }
    case 'F': case 'D': {
        jdouble v = 0;
        if (sig[0] == 'F') {
            jfloat f;
            err = (*jvmti)->GetLocalFloat(jvmti, thread, 0, var->slot, &f);
            v = f;
        } else {
            err = (*jvmti)->GetLocalDouble(jvmti, thread, 0, var->slot, &v);
        }
        if (err == JVMTI_ERROR_NONE)
            printf("        %-9s %-6s = %g\n", var->name, sig, v);
        break;
    }
    default: {   /* object or array: 'L...;' or '[...' */
        jobject obj = NULL;
        err = (*jvmti)->GetLocalObject(jvmti, thread, 0, var->slot, &obj);
        if (err != JVMTI_ERROR_NONE) break;
        if (obj == NULL) {
            printf("        %-9s = null\n", var->name);
        } else if (strcmp(sig, "Ljava/lang/String;") == 0) {
            const char *s = (*jni)->GetStringUTFChars(jni, (jstring)obj, NULL);
            printf("        %-9s String = \"%s\"\n", var->name, s ? s : "?");
            if (s) (*jni)->ReleaseStringUTFChars(jni, (jstring)obj, s);
        } else if (strcmp(sig, "[I") == 0) {
            jintArray arr = (jintArray)obj;
            jsize len = (*jni)->GetArrayLength(jni, arr);
            jint *elems = (*jni)->GetIntArrayElements(jni, arr, NULL);
            printf("        %-9s int[]  = {", var->name);
            for (jsize i = 0; elems && i < len; i++)
                printf("%s%d", i ? ", " : "", (int)elems[i]);
            printf("}\n");
            if (elems) (*jni)->ReleaseIntArrayElements(jni, arr, elems, JNI_ABORT);
        } else {
            printf("        %-9s %s\n", var->name, sig);
        }
        (*jni)->DeleteLocalRef(jni, obj);
    }
    }
    if (err != JVMTI_ERROR_NONE) CHECK_JVMTI_ERROR(jvmti, err, var->name);
}

/* All locals whose scope covers `loc` in the method at frame depth 0 */
static void print_locals(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread,
                         jmethodID method, jlocation loc) {
    jint count = 0;
    jvmtiLocalVariableEntry *vars = NULL;
    jvmtiError err = (*jvmti)->GetLocalVariableTable(jvmti, method, &count, &vars);
    if (err == JVMTI_ERROR_ABSENT_INFORMATION) {
        printf("      (no local variable table: compile with javac -g)\n");
        return;
    }
    if (err != JVMTI_ERROR_NONE) { CHECK_JVMTI_ERROR(jvmti, err, "GetLocalVariableTable"); return; }

    printf("      Locals in scope:\n");
    for (jint i = 0; i < count; i++) {
        /* A variable is only live between start_location and
           start_location + length. Outside that range its slot may hold
           garbage — or another variable entirely. */
        if (loc >= vars[i].start_location &&
            loc <  vars[i].start_location + vars[i].length)
            print_local(jvmti, jni, thread, &vars[i]);
        (*jvmti)->Deallocate(jvmti, (unsigned char*)vars[i].name);
        (*jvmti)->Deallocate(jvmti, (unsigned char*)vars[i].signature);
        if (vars[i].generic_signature)
            (*jvmti)->Deallocate(jvmti, (unsigned char*)vars[i].generic_signature);
    }
    (*jvmti)->Deallocate(jvmti, (unsigned char*)vars);
}

/* ------------------------------------------------------------------ */
/*  ClassPrepare: set the breakpoint and the watch once the class exists */
/* ------------------------------------------------------------------ */

static void JNICALL cbClassPrepare(jvmtiEnv *jvmti, JNIEnv *jni,
                                   jthread thread, jclass klass) {
    char *sig = NULL;
    if ((*jvmti)->GetClassSignature(jvmti, klass, &sig, NULL) != JVMTI_ERROR_NONE) return;
    int is_target = strcmp(sig, TARGET_CLASS_SIG) == 0;
    (*jvmti)->Deallocate(jvmti, (unsigned char*)sig);
    if (!is_target) return;

    /* Breakpoint: find the method, map the line to a location */
    jint mcount = 0;
    jmethodID *methods = NULL;
    (*jvmti)->GetClassMethods(jvmti, klass, &mcount, &methods);
    for (jint i = 0; i < mcount; i++) {
        char *name = NULL;
        (*jvmti)->GetMethodName(jvmti, methods[i], &name, NULL, NULL);
        if (name && strcmp(name, BREAK_METHOD) == 0) {
            jlocation loc = location_of_line(jvmti, methods[i], BREAK_LINE);
            if (loc < 0) {
                printf("[Debugger] No code on line %d (compile with javac -g)\n", BREAK_LINE);
            } else {
                jvmtiError err = (*jvmti)->SetBreakpoint(jvmti, methods[i], loc);
                CHECK_JVMTI_ERROR(jvmti, err, "SetBreakpoint");
                if (err == JVMTI_ERROR_NONE)
                    printf("[Debugger] Breakpoint set: DebugTestApp.%s line %d (bytecode index %lld)\n",
                           BREAK_METHOD, BREAK_LINE, (long long)loc);
            }
        }
        if (name) (*jvmti)->Deallocate(jvmti, (unsigned char*)name);
    }
    (*jvmti)->Deallocate(jvmti, (unsigned char*)methods);

    /* Watchpoint: find the field */
    jint fcount = 0;
    jfieldID *fields = NULL;
    (*jvmti)->GetClassFields(jvmti, klass, &fcount, &fields);
    for (jint i = 0; i < fcount; i++) {
        char *name = NULL;
        (*jvmti)->GetFieldName(jvmti, klass, fields[i], &name, NULL, NULL);
        if (name && strcmp(name, WATCH_FIELD) == 0) {
            jvmtiError err = (*jvmti)->SetFieldModificationWatch(jvmti, klass, fields[i]);
            CHECK_JVMTI_ERROR(jvmti, err, "SetFieldModificationWatch");
            if (err == JVMTI_ERROR_NONE)
                printf("[Debugger] Watching writes to DebugTestApp.%s\n", WATCH_FIELD);
        }
        if (name) (*jvmti)->Deallocate(jvmti, (unsigned char*)name);
    }
    (*jvmti)->Deallocate(jvmti, (unsigned char*)fields);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/*  Breakpoint: show where we are and the locals, then start stepping  */
/* ------------------------------------------------------------------ */

static void JNICALL cbBreakpoint(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread,
                                 jmethodID method, jlocation location) {
    char label[256];
    method_label(jvmti, method, label, sizeof(label));
    printf("[Debugger] Breakpoint hit: %s line %d\n", label,
           (int)line_of(jvmti, method, location));
    print_locals(jvmti, jni, thread, method, location);

    /* "Step over" the rest of the method: SingleStep for THIS thread only
       (the last argument). Enabling it for every thread would slow the
       whole JVM to a crawl — it fires for every bytecode executed. */
    step_method = method;
    step_line   = line_of(jvmti, method, location);
    (*jvmti)->GetFrameCount(jvmti, thread, &step_depth);
    jvmtiError err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
                         JVMTI_EVENT_SINGLE_STEP, thread);
    CHECK_JVMTI_ERROR(jvmti, err, "Enable SingleStep");
    fflush(stdout);
}

/* SingleStep fires before EVERY bytecode instruction the thread runs,
   including inside the methods it calls. Report only new lines in the
   stepped method, ignore deeper frames, and stop when it returns. */
static void JNICALL cbSingleStep(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread,
                                 jmethodID method, jlocation location) {
    jint depth = 0;
    (*jvmti)->GetFrameCount(jvmti, thread, &depth);

    if (depth < step_depth) {   /* returned to the caller: stop */
        char label[256];
        method_label(jvmti, method, label, sizeof(label));
        printf("[Debugger] Returned to %s line %d; stepping off\n", label,
               (int)line_of(jvmti, method, location));
        (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_DISABLE,
                                           JVMTI_EVENT_SINGLE_STEP, thread);
        step_method = NULL;
        fflush(stdout);
        return;
    }
    if (method != step_method || depth != step_depth) return;  /* in a callee */

    jint line = line_of(jvmti, method, location);
    if (line != step_line) {
        step_line = line;
        printf("[Debugger]   step -> line %d\n", (int)line);
        fflush(stdout);
    }
}

/* ------------------------------------------------------------------ */
/*  Field watch: called BEFORE the write happens                       */
/* ------------------------------------------------------------------ */

static void JNICALL cbFieldModification(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread,
                                        jmethodID method, jlocation location,
                                        jclass field_klass, jobject object,
                                        jfieldID field, char signature_type,
                                        jvalue new_value) {
    char label[256];
    method_label(jvmti, method, label, sizeof(label));
    /* The event fires before the store, so the field still holds the old value */
    jint old_value = (*jni)->GetStaticIntField(jni, field_klass, field);
    printf("[Debugger] Watch: DebugTestApp.%s %d -> %d  (in %s line %d)\n",
           WATCH_FIELD, (int)old_value, (int)new_value.i, label,
           (int)line_of(jvmti, method, location));
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/*  Agent Lifecycle                                                    */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    jvmtiEnv *jvmti = jvmti_get_env(vm);
    if (!jvmti) return JNI_ERR;

    /* The debugging capabilities. Possessing some of them (breakpoints,
       single-step, field watches) makes the JVM run code more slowly
       even when no breakpoint is set — request them only in a debugging
       agent, never "just in case". */
    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_generate_breakpoint_events         = 1;
    caps.can_generate_single_step_events        = 1;
    caps.can_generate_field_modification_events = 1;
    caps.can_access_local_variables             = 1;
    caps.can_get_line_numbers                   = 1;
    jvmtiError err = (*jvmti)->AddCapabilities(jvmti, &caps);
    CHECK_JVMTI_ERROR(jvmti, err, "AddCapabilities");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.ClassPrepare      = &cbClassPrepare;
    callbacks.Breakpoint        = &cbBreakpoint;
    callbacks.SingleStep        = &cbSingleStep;
    callbacks.FieldModification = &cbFieldModification;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    CHECK_JVMTI_ERROR(jvmti, err, "SetEventCallbacks");
    if (err != JVMTI_ERROR_NONE) return JNI_ERR;

    /* SingleStep is NOT enabled here — only per thread, at a breakpoint */
    const jvmtiEvent events[] = {
        JVMTI_EVENT_CLASS_PREPARE, JVMTI_EVENT_BREAKPOINT,
        JVMTI_EVENT_FIELD_MODIFICATION,
    };
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
        err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, events[i], NULL);
        CHECK_JVMTI_ERROR(jvmti, err, "SetEventNotificationMode");
        if (err != JVMTI_ERROR_NONE) return JNI_ERR;
    }

    printf("[Debugger] Mini debugger loaded (Lab 08b)\n");
    return JNI_OK;
}
