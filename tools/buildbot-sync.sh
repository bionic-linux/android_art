#! /bin/bash
#
# Copyright (C) 2018 The Android Open Source Project
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

# Push ART artifacts and its dependencies to a chroot directory for on-device testing.

adb wait-for-device

if [[ -z "$ANDROID_BUILD_TOP" ]]; then
  echo 'ANDROID_BUILD_TOP environment variable is empty; did you forget to run `lunch`?'
  exit 1
fi

if [[ -z "$ANDROID_PRODUCT_OUT" ]]; then
  echo 'ANDROID_PRODUCT_OUT environment variable is empty; did you forget to run `lunch`?'
  exit 1
fi

if [[ -z "$ART_TEST_CHROOT" ]]; then
  echo 'ART_TEST_CHROOT environment variable is empty; please set it before running this script.'
  exit 1
fi

# Sync the system directory to the chroot.
adb push "$ANDROID_PRODUCT_OUT/system" "$ART_TEST_CHROOT/"
# Overwrite the default public.libraries.txt file with a smaller one that
# contains only the public libraries pushed to the chroot directory.
adb push "$ANDROID_BUILD_TOP/art/tools/public.libraries.buildbot.txt" \
  "$ART_TEST_CHROOT/system/etc/public.libraries.txt"

# Manually "activate" the flattened Debug Runtime APEX by syncing it to the
# /apex directory in the chroot.
#
# We copy the files from `/system/apex/com.android.runtime.debug` to
# `/apex/com.android.runtime` in the chroot directory, instead of simply using a
# symlink, as Bionic's linker relies on the real path name of a binary
# (e.g. `/apex/com.android.runtime/bin/dex2oat`) to select the linker
# configuration.
#
# TODO: Handle the case of build targets using non-flatted APEX packages.
# As a workaround, one can run `export TARGET_FLATTEN_APEX=true` before building
# a target to have its APEX packages flattened.
adb shell rm -rf "$ART_TEST_CHROOT/apex/com.android.runtime"
adb shell cp -a "$ART_TEST_CHROOT/system/apex/com.android.runtime.debug" \
  "$ART_TEST_CHROOT/apex/com.android.runtime"

# Sync the data directory to the chroot.
adb push "$ANDROID_PRODUCT_OUT/data" "$ART_TEST_CHROOT/"
