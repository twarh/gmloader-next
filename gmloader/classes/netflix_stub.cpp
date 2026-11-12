#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jni.h"
#include "jni_internals.h"
#include "netflix_stub.h"

/*
    Stand-in for the Netflix GameMaker extension (netflixAndroid).

    Katana ZERO pipes every extension call through
    RunnerJNILib.CallExtensionFunction(String, String, int, double[], Object[]).
    There is no netflixAndroid class to talk to here, so the calls are answered
    from a static table. All values are strings because every EXTN record in
    game.droid declares ret=1 (String).

    NETFLIX_STUB_<FN> overrides a table value at runtime, e.g.
    NETFLIX_STUB_CloudSave_Read='{"name":"Player"}'.
*/

struct NetflixStubEntry {
    const char *name;
    const char *value;
};

static const NetflixStubEntry netflix_stub_table[] = {
    /* Netflix_Init, CheckUserAuth, ShowAccessButton, HideAccessButton */
    {"Init", "1"},
    {"CheckUserAuth", "1"},
    {"ShowAccessButton", "1"},
    {"HideAccessButton", "1"},
    {"Version", "1.0"},
    /* CloudSave_* */
    {"CloudSave_Save", "1"},
    {"CloudSave_Read", ""},
    {"CloudSave_ResolveConflict", "0"},
    /* Telemetry_* */
    {"Telemetry_FirstTimeUserExperienceStart", "1"},
    {"Telemetry_FirstTimeUserExperienceStepComplete", "1"},
    {"Telemetry_FirstTimeUserExperienceComplete", "1"},
    {"Telemetry_NewGameStart", "1"},
    {"Telemetry_GameComplete", "1"},
    {"Telemetry_ProgressCheckpointComplete", "1"},
    /* LogError, external name Netflix_logError */
    {"LogError", "1"},
    {"logError", "1"},
};

static const char *netflix_stub_base_name(const char *name)
{
    static const char prefix[] = "Netflix_";

    if (name == NULL)
        return NULL;

    if (strncmp(name, prefix, sizeof(prefix) - 1) == 0)
        return name + sizeof(prefix) - 1;

    return name;
}

static const char *netflix_stub_lookup(const char *base)
{
    if (base == NULL)
        return NULL;

    for (size_t i = 0; i < sizeof(netflix_stub_table) / sizeof(netflix_stub_table[0]); i++) {
        if (strcmp(base, netflix_stub_table[i].name) == 0)
            return netflix_stub_table[i].value;
    }

    return NULL;
}

static const char *netflix_stub_override(const char *base)
{
    char key[128];

    if (base == NULL)
        return NULL;

    snprintf(key, sizeof(key), "NETFLIX_STUB_%s", base);
    return getenv(key);
}

/* GetStringUTFChars returns a strdup'd copy; NULL when the fake jstring is empty. */
static const char *netflix_stub_jstring(JNIEnv *env, jstring js)
{
    if (env == NULL || js == NULL)
        return NULL;

    String *str = (String *)js;
    if (str->str == NULL)
        return NULL;

    return env->GetStringUTFChars(js, NULL);
}

/* Argument decoding is opt-in: it is skipped by default so that a malformed or
   unmodelled jdouble[]/Object[] layout cannot take the call down on entry. */
static int netflix_stub_decode_enabled(void)
{
    const char *value = getenv("NETFLIX_STUB_DECODE");

    return (value != NULL) && (value[0] != '\0') && (strcmp(value, "0") != 0);
}

static void netflix_stub_append(char *buf, size_t cap, const char *fmt, ...)
{
    size_t len = strlen(buf);
    va_list va;

    if (len >= cap - 1)
        return;

    va_start(va, fmt);
    vsnprintf(buf + len, cap - len, fmt, va);
    va_end(va);
}

jobject netflix_stub_call(JNIEnv *env, jstring ext_, jstring fn_, jint argc,
                          jdoubleArray dargs, jobjectArray oargs)
{
    fprintf(stderr, "[netflix-stub] enter\n");
    fflush(stderr);

    if (env == NULL)
        return NULL;

    const int decode = netflix_stub_decode_enabled();
    const char *ext = netflix_stub_jstring(env, ext_);
    const char *fn = netflix_stub_jstring(env, fn_);
    const char *ext_name = ext ? ext : "(null)";
    const char *fn_name = fn ? fn : "(null)";

    const char *base = netflix_stub_base_name(fn);
    const char *value = netflix_stub_lookup(base);
    if (value == NULL) {
        base = netflix_stub_base_name(ext);
        value = netflix_stub_lookup(base);
    }
    if (value == NULL)
        value = "";

    const char *override = netflix_stub_override(base);
    if (override != NULL)
        value = override;

    char dbuf[512] = "";
    char sbuf[1024] = "";

    /* Only the functions this fake JNI implements are used:
       GetArrayLength, GetDoubleArrayElements/ReleaseDoubleArrayElements,
       GetObjectArrayElement, GetStringUTFChars/ReleaseStringUTFChars,
       NewStringUTF. Decoding is gated behind NETFLIX_STUB_DECODE=1 and every
       array/element is NULL-checked before it is touched. */
    if (decode && dargs != NULL) {
        ArrayObject *darr = (ArrayObject *)dargs;
        if (darr->element_size == sizeof(jdouble) && darr->elements != NULL) {
            jsize dcount = env->GetArrayLength(dargs);
            jdouble *delems = (dcount > 0) ? env->GetDoubleArrayElements(dargs, NULL) : NULL;
            if (dcount == 0) {
                /* nothing to print */
            } else if (delems == NULL) {
                netflix_stub_append(dbuf, sizeof(dbuf), "<null>");
            } else {
                for (jsize i = 0; i < dcount && i < 16; i++) {
                    if (i)
                        netflix_stub_append(dbuf, sizeof(dbuf), ", %g", (double)delems[i]);
                    else
                        netflix_stub_append(dbuf, sizeof(dbuf), "%g", (double)delems[i]);
                }
                if (dcount > 16)
                    netflix_stub_append(dbuf, sizeof(dbuf), ", ...");
                env->ReleaseDoubleArrayElements(dargs, delems, JNI_ABORT);
            }
        } else if (darr->elements == NULL) {
            netflix_stub_append(dbuf, sizeof(dbuf), "<null elements>");
        } else {
            netflix_stub_append(dbuf, sizeof(dbuf), "<non-double-array>");
        }
    }

    if (decode && oargs != NULL) {
        ArrayObject *sarr = (ArrayObject *)oargs;
        if (sarr->elements == NULL) {
            netflix_stub_append(sbuf, sizeof(sbuf), "<null elements>");
        } else {
            jsize scount = env->GetArrayLength(oargs);
            if (sarr->instance_clazz != &String::clazz) {
                netflix_stub_append(sbuf, sizeof(sbuf), "<non-string-array %s>",
                                    sarr->instance_clazz ? sarr->instance_clazz->classname : "?");
            } else {
                for (jsize i = 0; i < scount && i < 16; i++) {
                    if (i)
                        netflix_stub_append(sbuf, sizeof(sbuf), ", ");

                    jobject elem = env->GetObjectArrayElement(oargs, i);
                    const char *s = (elem != NULL) ? netflix_stub_jstring(env, (jstring)elem) : NULL;
                    if (s != NULL) {
                        netflix_stub_append(sbuf, sizeof(sbuf), "\"%s\"", s);
                        env->ReleaseStringUTFChars((jstring)elem, s);
                    } else {
                        netflix_stub_append(sbuf, sizeof(sbuf), "null");
                    }
                }
                if (scount > 16)
                    netflix_stub_append(sbuf, sizeof(sbuf), ", ...");
            }
        }
    }

    if (decode) {
        fprintf(stderr, "[netflix-stub] %s.%s argc=%d d=[%s] s=[%s] -> \"%s\"\n",
                ext_name, fn_name, (int)argc, dbuf, sbuf, value);
    } else {
        fprintf(stderr, "[netflix-stub] %s.%s argc=%d -> \"%s\"\n",
                ext_name, fn_name, (int)argc, value);
    }
    fflush(stderr);

    if (fn)
        env->ReleaseStringUTFChars(fn_, fn);
    if (ext)
        env->ReleaseStringUTFChars(ext_, ext);

    return env->NewStringUTF(value);
}
