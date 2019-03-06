#!/bin/bash
#
# Copyright (C) 2015 The Android Open Source Project
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

# The work does by this script is (mostly) undone by tools/teardown-buildbot-device.sh.
# Make sure to keep these files in sync.

red='\033[0;31m'
green='\033[0;32m'
yellow='\033[0;33m'
nc='\033[0m'

if [ "$1" = --verbose ]; then
  verbose=true
else
  verbose=false
fi

# Setup as root, as some actions performed here require it.
adb root
adb wait-for-device

echo -e "${green}Date on host${nc}"
date

echo -e "${green}Date on device${nc}"
adb shell date

host_seconds_since_epoch=$(date -u +%s)
device_seconds_since_epoch=$(adb shell date -u +%s)

abs_time_difference_in_seconds=$(expr $host_seconds_since_epoch - $device_seconds_since_epoch)
if [ $abs_time_difference_in_seconds -lt 0 ]; then
  abs_time_difference_in_seconds=$(expr 0 - $abs_time_difference_in_seconds)
fi

seconds_per_hour=3600

# Kill logd first, so that when we set the adb buffer size later in this file,
# it is brought up again.
echo -e "${green}Killing logd, seen leaking on fugu/N${nc}"
adb shell pkill -9 -U logd logd && echo -e "${green}...logd killed${nc}"

# Update date on device if the difference with host is more than one hour.
if [ $abs_time_difference_in_seconds -gt $seconds_per_hour ]; then
  echo -e "${green}Update date on device${nc}"
  adb shell date -u @$host_seconds_since_epoch
fi

echo -e "${green}Turn off selinux${nc}"
adb shell setenforce 0
$verbose && adb shell getenforce

echo -e "${green}Setting local loopback${nc}"
adb shell ifconfig lo up
$verbose && adb shell ifconfig

# Ensure netd is running, as otherwise the logcat would be spammed
# with the following messages on devices running Android O:
#
#   E NetdConnector: Communications error: java.io.IOException: No such file or directory
#   E mDnsConnector: Communications error: java.io.IOException: No such file or directory
#
# Netd was initially disabled as an attempt to solve issues with
# network-related libcore and JDWP tests failing on devices running
# Android O (MR1) (see b/74725685). These tests are currently
# disabled. When a better solution has been found, we should remove
# the following lines.
echo -e "${green}Turning on netd${nc}"
adb shell start netd
$verbose && adb shell getprop init.svc.netd

if $verbose; then
  echo -e "${green}List properties${nc}"
  adb shell getprop

  echo -e "${green}Uptime${nc}"
  adb shell uptime

  echo -e "${green}Battery info${nc}"
  adb shell dumpsys battery
fi

# Fugu only handles buffer size up to 16MB.
product_name=$(adb shell getprop ro.build.product)

if [ "x$product_name" = xfugu ]; then
  buffer_size=16MB
else
  buffer_size=32MB
fi

echo -e "${green}Setting adb buffer size to ${buffer_size}${nc}"
adb logcat -G ${buffer_size}
$verbose && adb logcat -g

echo -e "${green}Removing adb spam filter${nc}"
adb logcat -P ""
$verbose && adb logcat -p

echo -e "${green}Kill stalled dalvikvm processes${nc}"
# 'ps' on M can sometimes hang.
timeout 2s adb shell "ps" >/dev/null
if [ $? = 124 ]; then
  echo -e "${green}Rebooting device to fix 'ps'${nc}"
  adb reboot
  adb wait-for-device root
else
  processes=$(adb shell "ps" | grep dalvikvm | awk '{print $2}')
  for i in $processes; do adb shell kill -9 $i; done
fi

# Chroot environment.
# ===================

# Linker configuration.
# ---------------------

# Default linker configuration file name/stem.
ld_config_file_path="/system/etc/ld.config.txt";
# VNDK-lite linker configuration file name.
ld_config_vndk_lite_file_path="/system/etc/ld.config.vndk_lite.txt";

# Find linker configuration path name on the "host system".
#
# The logic here partly replicates (and simplifies) Bionic's linker logic around
# configuration file search (see `get_ld_config_file_path` in
# bionic/linker/linker.cpp).
get_ld_host_system_config_file_path() {
  # Check whether the "host device" uses a VNDK-lite linker configuration.
  local vndk_lite=$(adb shell getprop "ro.vndk.lite" false)
  if [[ "$vndk_lite" = true ]]; then
    if adb shell test -f "$ld_config_vndk_lite_file_path"; then
      echo "$ld_config_vndk_lite_file_path"
      return
    fi
  fi
  # Check the "host device"'s VNDK version, if any.
  local vndk_version=$(adb shell getprop "ro.vndk.version")
  if [[ -n "$vndk_version" ]] && [[ "$vndk_version" != current ]]; then
    # Insert the VNDK version after the last period (and add another period).
    local ld_config_file_vdnk_path=$(echo "$ld_config_file_path" \
      | sed -e "s/^\\(.*\\)\\.\\([^.]\\)/\\1.${vndk_version}.\\2/")
    if adb shell test -f "$ld_config_file_vdnk_path"; then
      echo "$ld_config_file_vdnk_path"
      return
    fi
  else
    if adb shell test -f "$ld_config_file_path"; then
      echo "$ld_config_file_path"
      return
    fi
  fi
  # If all else fails, return the default linker configuration name.
  echo -e "${yellow}Cannot find linker configuration; using default path name.${nc}" >&2
  echo "$ld_config_file_path"
  return
}

# Find linker configuration path name on the "guest system".
#
# The logic here tries to "guess" the name of the linker configuration file,
# based on the contents of the build directory.
# Note: This requires that the "guest system" is built (e.g. by running
# art/tools/buildbot-build.sh) before running this script.
get_ld_guest_system_config_file_path() {
  if [[ -z "$ANDROID_PRODUCT_OUT" ]]; then
    echo -e "${red}ANDROID_PRODUCT_OUT environment variable is empty;" \
      "did you forget to run \`lunch\`${nc}?" >&2
    exit 1
  fi
  local ld_config_file_location="$ANDROID_PRODUCT_OUT/system/etc"
  local ld_config_file_path_number=$(find "$ld_config_file_location" -name "ld.*.txt" | wc -l)
  if [[ "$ld_config_file_path_number" -eq 0 ]]; then
    echo -e "${red}No linker configuration file found in \`$ld_config_file_location\`${nc}" >&2
    exit 1
  fi
  if [[ "$ld_config_file_path_number" -gt 1 ]]; then
    echo -e \
      "${red}More than one linker configuration file found in \`$ld_config_file_location\`${nc}" >&2
    exit 1
  fi
  # Strip the build prefix to make the path name relative to the "guest root directory".
  find "$ld_config_file_location" -name "ld.*.txt" | sed -e "s|^$ANDROID_PRODUCT_OUT||"
}

# Chroot environment setup.
# -------------------------

if [[ -n "$ART_TEST_CHROOT" ]]; then
  # Prepare the chroot dir.
  echo -e "${green}Prepare the chroot dir in $ART_TEST_CHROOT${nc}"

  # Check that ART_TEST_CHROOT is correctly defined.
  [[ "x$ART_TEST_CHROOT" = x/* ]] || { echo "$ART_TEST_CHROOT is not an absolute path"; exit 1; }

  # Create chroot.
  adb shell mkdir -p "$ART_TEST_CHROOT"

  # Provide property_contexts file(s) in chroot.
  # This is required to have Android system properties work from the chroot.
  # Notes:
  # - In Android N, only '/property_contexts' is expected.
  # - In Android O+, property_context files are expected under /system and /vendor.
  # (See bionic/libc/bionic/system_properties.cpp or
  # bionic/libc/system_properties/contexts_split.cpp for more information.)
  property_context_files="/property_contexts \
    /system/etc/selinux/plat_property_contexts \
    /vendor/etc/selinux/nonplat_property_context \
    /plat_property_contexts \
    /nonplat_property_contexts"
  for f in $property_context_files; do
    adb shell test -f "$f" \
      "&&" mkdir -p "$ART_TEST_CHROOT$(dirname $f)" \
      "&&" cp -f "$f" "$ART_TEST_CHROOT$f"
  done

  # Create directories required for ART testing in chroot.
  adb shell mkdir -p "$ART_TEST_CHROOT/tmp"
  adb shell mkdir -p "$ART_TEST_CHROOT/data/dalvik-cache"
  adb shell mkdir -p "$ART_TEST_CHROOT/data/local/tmp"

  # Populate /etc in chroot with required files.
  adb shell mkdir -p "$ART_TEST_CHROOT/system/etc"
  adb shell "cd $ART_TEST_CHROOT && ln -sf system/etc etc"

  # Check the linker configurations files on the "host system" and the "guest
  # system". If these file names are different, create a symlink from the "host
  # system" linker configuration file to the "guest system" linker configuration
  # file within the chroot environment.
  ld_host_system_config_file_path=$(get_ld_host_system_config_file_path)
  echo -e \
    "${green}Find host system linker configuration: \`$ld_host_system_config_file_path\`${nc}"
  ld_guest_system_config_file_path=$(get_ld_guest_system_config_file_path)
  echo -e \
    "${green}Find guest system linker configuration: \`$ld_guest_system_config_file_path\`${nc}"
  if [[ "$ld_host_system_config_file_path" != "$ld_guest_system_config_file_path" ]]; then
    echo -e "${green}Create linker configuration symlink in chroot environment:" \
      "\`$ART_TEST_CHROOT/$ld_host_system_config_file_path\`" \
      "-> \`$ld_guest_system_config_file_path\`${nc}"
    adb shell \
      ln -sf "$ld_guest_system_config_file_path" "$ART_TEST_CHROOT/$ld_host_system_config_file_path"
  fi

  # Provide /proc in chroot.
  adb shell mkdir -p "$ART_TEST_CHROOT/proc"
  adb shell mount | grep -q "^proc on $ART_TEST_CHROOT/proc type proc " \
    || adb shell mount -t proc proc "$ART_TEST_CHROOT/proc"

  # Provide /sys in chroot.
  adb shell mkdir -p "$ART_TEST_CHROOT/sys"
  adb shell mount | grep -q "^sysfs on $ART_TEST_CHROOT/sys type sysfs " \
    || adb shell mount -t sysfs sysfs "$ART_TEST_CHROOT/sys"
  # Provide /sys/kernel/debug in chroot.
  adb shell mount | grep -q "^debugfs on $ART_TEST_CHROOT/sys/kernel/debug type debugfs " \
    || adb shell mount -t debugfs debugfs "$ART_TEST_CHROOT/sys/kernel/debug"

  # Provide /dev in chroot.
  adb shell mkdir -p "$ART_TEST_CHROOT/dev"
  adb shell mount | grep -q "^tmpfs on $ART_TEST_CHROOT/dev type tmpfs " \
    || adb shell mount -o bind /dev "$ART_TEST_CHROOT/dev"

  # Create /apex directory in chroot.
  #
  # Note that we do not mount a tmpfs in this directory, as we simply copy the
  # contents of the flattened Runtime APEX in it during the execution of
  # art/tools/buildbot-sync.sh. (This may change in the future, if we start
  # using scripts art/tools/mount-buildbot-apexes.sh and
  # art/tools/unmount-buildbot-apexes.sh.)
  adb shell mkdir -p "$ART_TEST_CHROOT/apex"
fi
