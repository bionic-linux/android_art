#!/system/bin/sh
#
# Copyright (C) 2020 The Android Open Source Project
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

# Script to recompile the non-APEX jars in the boot class path and
# system server jars for on-device signing following an ART Module
# update.
#
# For testing purposes, prepare Android device using:
#
#   $ adb root
#   $ adb shell setenforce 0
#
# TODO: this is script is currently for proof-of-concept. It does not
# respect many of the available dalvik.vm properties, e.g. affinity,
# threads, etc nor dalvik.vm.dex2oat-updatable-bcp-packages-file,
# dalvik.vm.dex2oat-resolve-startup-strings.
#
# TODO: some of the system server jars seem to be installed on device
# for 32-bit and 64-bit, but services.jar artifacts are just one arch.
# Not sure why both flavors are present. Does this script need to generate
# both?
#
# TODO: Logging failure / error handling.

# Output directory for generated artifacts. Real location is TBD.
output=$PWD/out

function mkdir_clean() {
  # mkdir_clean <dir_path>
  local dir_path=$1
  rm -rf "${dir_path}"
  mkdir -p "${dir_path}"
}

function check_artifact() {
  # check_artifact <old_file> <new_file>
  local old_file="$1"
  local new_file="$2"
  for i in "${old_file}" "${new_file}" ; do
    if [ ! -f "${i}" ]; then
      echo "Missing file $i"
      return -1
    fi
  done
  local lhs=$(stat -L -c "%n (%s bytes)" ${new_file})
  local rhs=$(stat -L -c "%n (%s bytes)" ${old_file})
  echo -n " ${lhs} compared to ${rhs} "
  cmp -s "${old_file}" "${new_file}"
  if [[ $? -eq 0 ]]; then
    echo "MATCH"
  else
    echo "DIFFER"
  fi
  return 0
}

# Determine candidate architectures for this device.
case `getprop ro.product.cpu.abi` in
  arm*)
    arch32=arm
    arch64=arm64
    ;;
  x86*)
    arch32=x86
    arch64=x86_64
    ;;
  *)
    echo "Unknown abi"
    exit -1
esac

# Determine architectures to use for Zygote.
case `getprop ro.zygote` in
  zygote32)
    archs="$arch32"
    systemserver_arch="$arch32"
    dex2oat=/apex/com.android.art/bin/dex2oat32
    ;;
  zygote64_32)
    archs="$arch32 $arch64"
    systemserver_arch="$arch64"
    dex2oat=/apex/com.android.art/bin/dex2oat64
    ;;
  zygote64)
    archs="$arch64"
    systemserver_arch="$arch64"
    dex2oat=/apex/com.android.art/bin/dex2oat64
    ;;
  *)
    echo "Unknown ro.zygote value"
    exit -1
    ;;
esac

# Determine which boot class path jars to compile.
device_bcp_list=""
device_bcp_dex_files=""
device_bcp_dex_locations=""
for jar in ${DEX2OATBOOTCLASSPATH//:/ }; do
  if [[ ${jar} = *com.android.art* ]]; then
    continue
  fi
  device_bcp_list="${device_bcp_list}${device_bcp_list:+:}${jar}"
  device_bcp_dex_files="$device_bcp_dex_files --dex-file=${jar}"
  device_bcp_dex_locations="$device_bcp_dex_locations --dex-location=$jar"
done

# Compile the boot class path elements that are present on device.
for arch in ${archs}; do
  swap_dir="${output}/tmp"
  mkdir_clean "${swap_dir}"

  arch_output="${output}/$arch"
  mkdir_clean "${arch_output}"

  invocation_dir="${output}/$arch"
  mkdir_clean "${invocation_dir}"

  echo "Compiling ${device_bcp_list} ($arch)"
  ${dex2oat} --avoid-storing-invocation \
    --write-invocation-to=${invocation_dir}/boot.invocation \
    --runtime-arg -Xms`getprop dalvik.vm.image-dex2oat-Xms 64m` \
    --runtime-arg -Xmx`getprop dalvik.vm.image-dex2oat-Xmx 64m` \
    --compiler-filter=speed-profile \
    --profile-file=/system/etc/boot-image.prof \
    --dirty-image-objects=/system/etc/dirty-image-objects \
    --runtime-arg -Xbootclasspath:${DEX2OATBOOTCLASSPATH} \
    --runtime-arg -Xbootclasspath-locations:${DEX2OATBOOTCLASSPATH} \
    --boot-image=/apex/com.android.art/javalib/boot.art \
    ${device_bcp_dex_files} \
    ${device_bcp_dex_locations} \
    --generate-debug-info \
    --generate-build-id \
    --image-format=lz4hc \
    --strip \
    --oat-file=${arch_output}/boot.oat \
    --oat-location=${arch_output}/boot.oat \
    --image=${arch_output}/boot.art \
    --android-root=out/empty \
    --no-inline-from=core-oj.jar \
    --force-determinism \
    --abort-on-hard-verifier-error \
    --instruction-set=$arch \
    --instruction-set-variant=generic \
    --instruction-set-features=default \
    --generate-mini-debug-info \
    --swap-file=${swap_dir}/swap
  rm -rf "${swap_dir}"

  # Report on sizes of generated outputs vs existing.
  for jar in ${device_bcp_list//:/ }; do
    jarname=${jar##?*/}
    stem=${jarname/%.jar}
    for ext in art oat vdex; do
      check_artifact "/system/framework/${arch}/boot-${stem}.${ext}" \
                     "${arch_output}/boot-$stem.$ext"
    done
  done
done

# Compile system server and related jars.
classloader_context=""
for jar in ${SYSTEMSERVERCLASSPATH//:/ }; do
  # Skip class path components in APEXes
  if [[ ${jar} = "/apex"* ]]; then
    continue
  fi

  jarname=${jar##?*/}
  stem=${jarname/.jar}
  profile_file=/system/framework/$jarname.prof
  if [ -f "${profile_file}" ] ; then
    profile_arg=--profile-file=${profile_file}
    filter=speed-profile
  else
    profile_arg=""
    filter=speed
  fi

  echo "compiling $jar (${systemserver_arch} ${filter} PCL[${classloader_context}])"
  $dex2oat --avoid-storing-invocation \
           --write-invocation-to=${invocation_dir}/${stem}.invocation \
           --runtime-arg -Xms`getprop dalvik.vm.dex2oat-Xms 64m` \
           --runtime-arg -Xmx`getprop dalvik.vm.dex2oat-Xmx 512m` \
           --runtime-arg -Xbootclasspath:${DEX2OATBOOTCLASSPATH} \
           --runtime-arg -Xbootclasspath-locations:${DEX2OATBOOTCLASSPATH} \
           --class-loader-context=PCL[${classloader_context}] \
           --stored-class-loader-context=PCL[${classloader_context}] \
           --boot-image=/apex/com.android.art/javalib/boot.art:${output}/boot-framework.art \
           --dex-file=${jar} \
           --dex-location=${jar} \
           --oat-file=${arch_output}/${stem}.odex \
           --app-image-file=${oat_file_dir}/${stem}.art \
           --android-root=out/empty \
           --instruction-set=${systemserver_arch} \
           --instruction-set-variant=generic \
           --instruction-set-features=default \
           --no-generate-debug-info \
           --generate-build-id \
           --abort-on-hard-verifier-error \
           --force-determinism \
           --no-inline-from=core-oj.jar \
           --copy-dex-files=false \
           --compiler-filter=speed \
           --generate-mini-debug-info \
           --compilation-reason=prebuilt \
           --image-format=lz4 \
           --resolve-startup-const-strings=true \
           ${profile_arg}
  for ext in odex vdex; do
    check_artifact "/system/framework/oat/${systemserver_arch}/${stem}.${ext}" \
                   "${arch_output}/${stem}.${ext}"
  done
  classloader_context=${classloader_context}${classloader_context:+:}${jar}
done
