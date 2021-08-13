#!/usr/bin/env python3
#
# Copyright (C) 2021 The Android Open Source Project
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

""" This scripts builds Java files needed to execute given run-test """

import argparse, os, tempfile, shutil, subprocess, glob, sys, textwrap, re, json

ZIP="prebuilts/build-tools/linux-x86/bin/soong_zip"

def build(args, tmp, mode):
  join = os.path.join
  test = args.test
  assert(re.match("[0-9]+-.*", test))
  srcdir = join("art", "test", test)
  dstdir = join(tmp, mode, test)

  # Don't build tests that are disabled since they might not compile (e.g. on jvm).
  knownfailures = json.loads(open(join("art", "test", "knownfailures.json"), "rt").read())
  def is_knownfalure(kf):
    return test in kf.get("tests", []) and mode == kf.get("variant") and not kf.get("env_vars")
  if any(is_knownfalure(kf) for kf in knownfailures):
    return

  # Copy all source files to the temporary directory.
  shutil.copytree(srcdir, dstdir)

  # Copy the default scripts if the test does not have a custom ones.
  for name in ["build", "run", "check"]:
    src, dst = f"art/test/etc/default-{name}", join(dstdir, name)
    if os.path.exists(dst):
      shutil.copy2(src, dstdir)  # Copy default script next to the custom script.
    else:
      shutil.copy2(src, dst)  # Use just the default script.
    os.chmod(dst, 0o755)

  # Execute the build script.
  build_top = os.getcwd()
  java_home = os.environ.get("JAVA_HOME")
  tools_dir = os.path.abspath(join(os.path.dirname(__file__), "../../../out/bin"))
  env = {
    "PATH": os.environ.get("PATH"),
    "ANDROID_BUILD_TOP": build_top,
    "ART_TEST_RUN_TEST_BOOTCLASSPATH": join(build_top, args.bootclasspath),
    "TEST_NAME":   test,
    "SOONG_ZIP":   join(build_top, "prebuilts/build-tools/linux-x86/bin/soong_zip"),
    "ZIPALIGN":    join(build_top, "prebuilts/build-tools/linux-x86/bin/zipalign"),
    "JAVA":        join(java_home, "bin/java"),
    "JAVAC":       join(java_home, "bin/javac") + " -g -Xlint:-options -source 1.8 -target 1.8",
    "D8":          join(tools_dir, "d8"),
    "HIDDENAPI":   join(tools_dir, "hiddenapi"),
    "JASMIN":      join(tools_dir, "jasmin"),
    "SMALI":       join(tools_dir, "smali"),
    "NEED_DEX":    {"host": "true", "target": "true", "jvm": "false"}[mode],
    "USE_DESUGAR": "true",
  }
  proc = subprocess.run([join(dstdir, "build"), "--" + mode], cwd=dstdir, env=env)
  assert(proc.returncode == 0) # Build failed.

def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--out", help="Path of the generated ZIP file with the build data")
  parser.add_argument("--test", help="Name of the test to build")
  parser.add_argument("--bootclasspath", help="JAR files used for javac compilation")
  args = parser.parse_args()

  with tempfile.TemporaryDirectory(prefix="art-run-test-data-") as tmp:
    for mode in ["host", "target", "jvm"]:
      build(args, tmp, mode)

    # Create the final zip file which contains the content of the temporary directory.
    proc = subprocess.run([ZIP, "-o", args.out, "-C", tmp, "-D", tmp])
    assert(proc.returncode == 0) # Zip failed.

if __name__ == "__main__":
  main()
