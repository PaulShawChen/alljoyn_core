/******************************************************************************
 * Copyright 2010 - 2011, Qualcomm Innovation Center, Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 *
 ******************************************************************************/
/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class org_alljoyn_bus_samples_simpleservice_Service */

#ifndef _Included_org_alljoyn_bus_samples_simpleservice_Service
#define _Included_org_alljoyn_bus_samples_simpleservice_Service
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     org_alljoyn_bus_samples_simpleservice_Service
 * Method:    simpleOnCreate
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_org_alljoyn_bus_samples_simpleservice_Service_simpleOnCreate
    (JNIEnv *, jobject);

/*
 * Class:     org_alljoyn_bus_samples_simpleservice_Service
 * Method:    simpleOnDestroy
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_alljoyn_bus_samples_simpleservice_Service_simpleOnDestroy
    (JNIEnv *, jobject);

/*
 * Class:     org_alljoyn_bus_samples_simpleservice_Service
 * Method:    startService
 * Signature: (Ljava/lang/String;Ljava/lang/String;)Z
 */
JNIEXPORT jboolean JNICALL Java_org_alljoyn_bus_samples_simpleservice_Service_startService
    (JNIEnv *, jobject, jstring, jstring);

/*
 * Class:     org_alljoyn_bus_samples_simpleservice_Service
 * Method:    stopService
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_alljoyn_bus_samples_simpleservice_Service_stopService
    (JNIEnv *, jobject, jstring);

#ifdef __cplusplus
}
#endif
#endif
