# Copyright (C) 2017 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

# --- amm.jar ----------------
include $(CLEAR_VARS)
LOCAL_SRC_FILES := $(call all-java-files-under, src/amm)
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := amm
include $(BUILD_JAVA_LIBRARY)

AMM_DEX := $(intermediates.COMMON)/amm.dex

# Generate amm.dex in the desired location by copying it from wherever the
# build system generates it by default.
$(AMM_DEX): PRIVATE_AMM_SOURCE_DEX := $(built_dex)
$(AMM_DEX): $(built_dex)
	cp $(PRIVATE_AMM_SOURCE_DEX) $@

# --- amm-debug.jar ----------
include $(CLEAR_VARS)
LOCAL_SRC_FILES := $(call all-java-files-under, src/amm-debug)
LOCAL_JAR_MANIFEST := etc/amm-debug.mf
LOCAL_JAVA_RESOURCE_FILES := $(AMM_DEX)
LOCAL_IS_HOST_MODULE := true
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := amm-debug
include $(BUILD_HOST_JAVA_LIBRARY)

# --- amm script ----------------
include $(CLEAR_VARS)
LOCAL_IS_HOST_MODULE := true
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE := amm
LOCAL_SRC_FILES := amm
include $(BUILD_PREBUILT)

# --- ammtestjni.so -------------
include $(CLEAR_VARS)
LOCAL_MODULE := libammtestjni
LOCAL_SRC_FILES := $(call all-c-files-under, test/jni)
LOCAL_SDK_VERSION := current
include $(BUILD_SHARED_LIBRARY)

# --- amm-test.apk --------------
include $(CLEAR_VARS)
LOCAL_PACKAGE_NAME := AmmTest
LOCAL_MODULE_TAGS := samples tests
LOCAL_SDK_VERSION := current
LOCAL_SRC_FILES := $(call all-java-files-under, test/src)
LOCAL_STATIC_JAVA_LIBRARIES := amm junit android-support-test
LOCAL_JNI_SHARED_LIBRARIES := libammtestjni
LOCAL_MANIFEST_FILE := test/AndroidManifest.xml
LOCAL_RESOURCE_DIR := $(LOCAL_PATH)/test/res
include $(BUILD_PACKAGE)
AMM_TEST_APK := $(LOCAL_BUILT_MODULE)

.PHONY: amm-test
amm-test: PRIVATE_AMM_TEST_APK := $(AMM_TEST_APK)
amm-test: $(AMM_TEST_APK)
	adb install -r $(PRIVATE_AMM_TEST_APK)
	adb shell am instrument -w com.android.amm.test/android.support.test.runner.AndroidJUnitRunner

# Clean up local variables.
AMM_DEX :=
AMM_TEST_APK :=

