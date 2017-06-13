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

if [ ! -d art ]; then
  echo "Script needs to be run at the root of the android tree" >&2
  exit 1
fi

# Logic for setting out_dir from build/make/core/envsetup.mk:
if [[ -z $OUT_DIR ]]; then
  if [[ -z $OUT_DIR_COMMON_BASE ]]; then
    out_dir=out
  else
    out_dir=${OUT_DIR_COMMON_BASE}/${PWD##*/}
  fi
else
  out_dir=${OUT_DIR}
fi

java_libraries_dir=${out_dir}/target/common/obj/JAVA_LIBRARIES
common_targets="vogar core-tests apache-harmony-jdwp-tests-hostdex jsr166-tests mockito-target ${out_dir}/host/linux-x86/bin/jack"
mode="target"
j_arg="-j$(nproc)"
showcommands=
make_command=
public_libraries=

while true; do
  if [[ "$1" == "--host" ]]; then
    mode="host"
    shift
  elif [[ "$1" == "--target" ]]; then
    mode="target"
    shift
  elif [[ "$1" == -j* ]]; then
    j_arg=$1
    shift
  elif [[ "$1" == "--showcommands" ]]; then
    showcommands="showcommands"
    shift
  elif [[ "$1" == "" ]]; then
    break
  else
    echo "Unknown options $@" >&2
    exit 1
  fi
done

if [[ $mode == "host" ]]; then
  make_command="make $j_arg $showcommands build-art-host-tests $common_targets"
  make_command+=" ${out_dir}/host/linux-x86/lib/libjavacoretests.so "
  make_command+=" ${out_dir}/host/linux-x86/lib64/libjavacoretests.so"
elif [[ $mode == "target" ]]; then
  if [[ "${ART_TEST_ANDROID_ROOT:-/system}" != "/system" ]]; then
    # If targetting custom installation location on device, add linker
    # hints to the make targets.
    public_libraries="${out_dir}/target/product/${TARGET_PRODUCT}/system/etc/public.libraries.txt"
  fi
  make_command="make $j_arg $showcommands $public_libraries build-art-target-tests $common_targets"
  make_command+=" libjavacrypto libjavacoretests libnetd_client linker toybox toolbox sh"
  make_command+=" ${out_dir}/host/linux-x86/bin/adb libstdc++ "
fi

echo "Executing $make_command"
$make_command
make_status=$?

if [[ $make_status -ne 0 && ! -z "$public_libraries" && ! -f "$public_libraries" ]]; then
  # Non /system target builds will typically use the master-art
  # manifest with the armv8-eng lunch combo. The public libraries file
  # has a valid make target in master-art for lunch combo's that
  # support running target tests.
  echo "Building for a device target with a custom installation location requires the master-art manifest with a supported lunch combo." >&2
fi
exit $make_status
