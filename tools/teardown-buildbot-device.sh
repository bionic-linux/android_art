#!/bin/bash
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

# This script undoes (most of) the work done by tools/setup-buildbot-device.sh.
# Make sure to keep these files in sync.

green='\033[0;32m'
nc='\033[0m'

# Setup as root, as some actions performed here require it.
adb root
adb wait-for-device

if [[ -n "$ART_TEST_CHROOT" ]]; then

  # remove_filesystem_from_chroot DIR-IN-CHROOT FSTYPE REMOVE-DIR-IN-CHROOT
  # -----------------------------------------------------------------------
  # Unmount filesystem with type FSTYPE mounted in directory DIR-IN-CHROOT
  # under the chroot directory.
  # Remove DIR-IN-CHROOT under the chroot if REMOVE-DIR-IN-CHROOT is
  # true.
  remove_filesystem_from_chroot() {
    local dir_in_chroot=$1
    local fstype=$2
    local remove_dir=$3
    local dir="$ART_TEST_CHROOT/$dir_in_chroot"
    adb shell test -d "$dir" \
      && adb shell mount | grep -q "^$fstype on $dir type $fstype " \
      && if adb shell umount "$dir"; then
           $remove_dir && adb shell rmdir "$dir"
         else
           adb shell lsof "$dir"
         fi
  }

  # Tear down the chroot dir.
  echo -e "${green}Tear down the chroot dir in $ART_TEST_CHROOT${nc}"

  # Check that ART_TEST_CHROOT is correctly defined.
  [[ "x$ART_TEST_CHROOT" = x/* ]] || { echo "$ART_TEST_CHROOT is not an absolute path"; exit 1; }

  # Remove /dev from chroot.
  remove_filesystem_from_chroot dev tmpfs true

  # Remove /sys/kernel/debug from chroot.
  remove_filesystem_from_chroot sys/kernel/debug debugfs false
  # Remove /sys from chroot.
  remove_filesystem_from_chroot sys sysfs true

  # Remove /proc from chroot.
  remove_filesystem_from_chroot proc proc true

  # Remove /etc from chroot.
  adb shell rm -f "$ART_TEST_CHROOT/etc"
  adb shell rm -rf "$ART_TEST_CHROOT/system/etc"

  # Remove directories used for ART testing in chroot.
  adb shell rm -rf "$ART_TEST_CHROOT/data/local/tmp"
  adb shell rm -rf "$ART_TEST_CHROOT/data/dalvik-cache"
  adb shell rm -rf "$ART_TEST_CHROOT/tmp"

  # Remove property_contexts file(s) from chroot.
  property_context_files="/property_contexts \
    /system/etc/selinux/plat_property_contexts \
    /vendor/etc/selinux/nonplat_property_context \
    /plat_property_contexts \
    /nonplat_property_contexts"
  for f in $property_context_files; do
    adb shell rm -f "$ART_TEST_CHROOT$f"
  done
fi
