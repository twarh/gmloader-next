#pragma once
#include "jni.h"

/*
    Stand-in for the Netflix GameMaker extension.

    libyoyo.so routes every Java extension call through
    RunnerJNILib.CallExtensionFunction, so this shim answers those calls with
    canned string results instead of NULL.
*/
jobject netflix_stub_call(JNIEnv *env, jstring ext, jstring fn, jint argc,
                          jdoubleArray dargs, jobjectArray oargs);
