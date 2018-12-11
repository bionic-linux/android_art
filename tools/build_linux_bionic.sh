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


if [[ -z $ANDROID_BUILD_TOP ]]; then
  pushd .
else
  pushd $ANDROID_BUILD_TOP
fi

if [ ! -d art ]; then
  echo "Script needs to be run at the root of the android tree"
  exit 1
fi

source build/envsetup.sh >&/dev/null # for get_build_var
# Soong needs a bunch of variables set and will not run if they are missing.
# The default values of these variables is only contained in make, so use
# nothing to create the variables then remove all the other artifacts.
build/soong/soong_ui.bash --make-mode nothing
if [ $? != 0 ]; then
  exit 1
fi

out_dir=$(get_build_var OUT_DIR)
host_out=$(get_build_var HOST_OUT)

# TODO(b/31559095) Figure out a better way to do this.
#
# There is no good way to force soong to generate host-bionic builds currently
# so this is a hacky workaround.
tmp_soong_var=$(mktemp --tmpdir soong.variables.bak.XXXXXX)

cat $out_dir/soong/soong.variables > ${tmp_soong_var}
build/soong/soong_ui.bash --make-mode clean
mkdir -p $out_dir/soong

python3 <<END - ${tmp_soong_var} ${out_dir}/soong/soong.variables
import json
import sys
x = json.load(open(sys.argv[1]))
x['Allow_missing_dependencies'] = True
x['HostArch'] = 'x86_64'
x['CrossHost'] = 'linux_bionic'
x['CrossHostArch'] = 'x86_64'
if 'CrossHostSecondaryArch' in x:
  del x['CrossHostSecondaryArch']
json.dump(x, open(sys.argv[2], mode='w'))
END

rm $tmp_soong_var

# Having built with host-bionic confuses soong somewhat by making it think the
# linux_bionic targets are needed for art phony targets like
# test-art-host-run-test-dependencies. To work around this blow away all
# ninja files in OUT_DIR. The build system is smart enough to not need to
# rebuild stuff so this should be fine.
rm -f $OUT_DIR/*.ninja $OUT_DIR/soong/*.ninja

build/soong/soong_ui.bash --make-mode --skip-make $@
