/**
 * jvmti_utils.h — Shared utility macros and helpers for JVMTI agent labs.
 *
 * Provides:
 *   - jvmti_get_env()     : Acquire a jvmtiEnv* from a JavaVM*
 *   - CHECK_JVMTI_ERROR() : Error-reporting macro (§2.3)
 *   - jvmti_log()         : Simple append-to-file logger
 *
 * Each lab can optionally #include this header to reduce boilerplate.
 * Lab 01 intentionally does NOT use it, to show the raw JVMTI patterns.
 */

#ifndef JVMTI_UTILS_H
#define JVMTI_UTILS_H

#include <jvmti.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  JVMTI environment acquisition                                     */
/* ------------------------------------------------------------------ */

/**
 * Acquires a jvmtiEnv pointer from the given JavaVM.
 * Returns NULL on failure (with an error message printed to stderr).
 */
static inline jvmtiEnv* jvmti_get_env(JavaVM *vm) {
    jvmtiEnv *jvmti = NULL;
    jint res = (*vm)->GetEnv(vm, (void **)&jvmti, JVMTI_VERSION_1_2);
    if (res != JNI_OK || jvmti == NULL) {
        fprintf(stderr, "[JVMTI] Unable to access JVMTI! (error=%d)\n", (int)res);
        return NULL;
    }
    return jvmti;
}

/* ------------------------------------------------------------------ */
/*  Error-reporting macro (§2.3)                                      */
/* ------------------------------------------------------------------ */

/**
 * Report a JVMTI error with its symbolic name, then continue.
 * Matches the macro introduced in Lab 02 and used in every lab.
 *
 * Example:
 *   jvmtiError err = (*jvmti)->AddCapabilities(jvmti, &caps);
 *   CHECK_JVMTI_ERROR(jvmti, err, "AddCapabilities");
 *   if (err != JVMTI_ERROR_NONE) return JNI_ERR;   // abort if critical
 */
#define CHECK_JVMTI_ERROR(jvmti, err, msg) \
    do { \
        if ((err) != JVMTI_ERROR_NONE) { \
            char *err_name = NULL; \
            (*jvmti)->GetErrorName((jvmti), (err), &err_name); \
            fprintf(stderr, "[Agent ERROR] %s: %s (%d)\n", \
                    (msg), err_name ? err_name : "unknown", (err)); \
            if (err_name) (*jvmti)->Deallocate((jvmti), (unsigned char*)err_name); \
        } \
    } while(0)

/* ------------------------------------------------------------------ */
/*  Simple file logger                                                 */
/* ------------------------------------------------------------------ */

/**
 * Appends a message line to the given log file.
 *
 * The file is opened once and the stream kept open. stdio streams lock
 * internally, so each fprintf from concurrent callbacks lands as a whole
 * line. (Reopening the file for every message loses lines when threads
 * race: on Windows, append mode is not atomic.) The first call opens the
 * stream, so make it from Agent_OnLoad, before any callbacks can run.
 * Each line is flushed immediately so nothing is lost on a crash.
 *
 * NOTE: Do not call from GC or ObjectFree callbacks — those run in a
 * restricted environment where only simple bookkeeping is safe
 * (see §3.7 and §7.3).
 */
static inline void jvmti_log(const char* filename, const char* msg) {
    static FILE* fp = NULL;
    static char fp_name[512] = "";
    if (fp == NULL || strcmp(fp_name, filename) != 0) {
        if (fp) fclose(fp);
        fp = fopen(filename, "a");
        snprintf(fp_name, sizeof(fp_name), "%s", filename);
        if (!fp) return;
    }
    fprintf(fp, "%s\n", msg);
    fflush(fp);
}

#endif /* JVMTI_UTILS_H */
