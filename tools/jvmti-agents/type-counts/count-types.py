#!/usr/bin/env python
#
# Copyright (C) 2019 The Android Open Source Project
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

"""
Retrieves the counts of how many objects have a particular field filled with what on all running
processes.

Prints a json map from pid -> (log-tag, field-name, field-type, count, total-size).
"""


import adb
import argparse
import concurrent.futures
import itertools
import json
import logging
import os
import os.path
import signal
import subprocess
import time

def main():
  parser = argparse.ArgumentParser(description="Get counts of types from a device.")
  parser.add_argument("-S", "--serial", metavar="SERIAL", type=str,
                      required=False,
                      default=os.environ.get("ANDROID_SERIAL", None),
                      help="Android serial to use. Defaults to ANDROID_SERIAL")
  parser.add_argument("pid", default=[], action="append",
                      help="Specific pids to check. By default checks all running dalvik processes")
  has_out = "OUT" in os.environ
  def_32 = os.path.join(os.environ.get("OUT", ""), "system", "lib", "libtypecounts.so")
  def_64 = os.path.join(os.environ.get("OUT", ""), "system", "lib64", "libtypecounts.so")
  has_32 = has_out and os.path.exists(def_32)
  has_64 = has_out and os.path.exists(def_64)
  def pushable_lib(name):
    if os.path.isfile(name):
      return name
    else:
      raise argparse.ArgumentTypeError(name + " is not a file!")
  parser.add_argument('--lib32', type=pushable_lib,
                      required=not has_32,
                      action='store',
                      default=def_32,
                      help="Location of 32 bit agent to push")
  parser.add_argument('--lib64', type=pushable_lib,
                      required=not has_64,
                      action='store',
                      default=def_64 if has_64 else None,
                      help="Location of 64 bit agent to push")

  out = parser.parse_args()

  device = adb.device.get_device(out.serial)
  print("getting root")
  device.root()

  print("Disabling selinux")
  device.shell("setenforce 0".split())

  print("Pushing libraries")
  lib32 = device.shell("mktemp".split())[0].strip()
  lib64 = device.shell("mktemp".split())[0].strip()

  print(out.lib32 + " -> " + lib32)
  device.push(out.lib32, lib32)

  print(out.lib64 + " -> " + lib64)
  device.push(out.lib64, lib64)

  mkcmd = lambda lib: "'{}'".format(lib)

  print("Clearing logcat")
  device.shell("logcat -c".split())
  final = {}
  print("Getting info from every process dumped to logcat")
  for p in out.pid:
    res = check_single_process(p, device, mkcmd, lib32, lib64);
    if res is not None:
      final[p] = res
  device.shell('rm {}'.format(lib32).split())
  device.shell('rm {}'.format(lib64).split())
  print(json.dumps(final, indent=2))

def kill_it(p):
  time.sleep(1)
  os.kill(p, signal.SIGINT)

def check_single_process(pid, device, mkcmd, bit32, bit64):
  try:
    # Link agent into the /data/data/<app>/code_cache directory
    name = device.shell('cat /proc/{}/cmdline'.format(pid).split())[0].strip('\0')
    targetdir = str('/data/data/{}/code_cache'.format(str(name).strip()))
    print("Will place agents in {}".format(targetdir))
    target32 = device.shell('mktemp -p {}'.format(targetdir).split())[0].strip()
    print("{} -> {}".format(bit32, target32))
    target64 = device.shell('mktemp -p {}'.format(targetdir).split())[0].strip()
    print("{} -> {}".format(bit64, target64))
    try:
      device.shell('cp {} {}'.format(bit32, target32).split())
      device.shell('cp {} {}'.format(bit64, target64).split())
      device.shell('chmod 555 {}'.format(target32).split())
      device.shell('chmod 555 {}'.format(target64).split())
      # Just try attaching both 32 and 64 bit. Wrong one will fail silently.
      device.shell(['am', 'attach-agent', str(pid), mkcmd(target32)])
      device.shell(['am', 'attach-agent', str(pid), mkcmd(target64)])
      time.sleep(0.5)
      device.shell('kill -3 {}'.format(pid).split())
      time.sleep(0.5)
    finally:
      print("Removing agent copies at {}, {}".format(target32, target64))
      device.shell(['rm', '-f', target32])
      device.shell(['rm', '-f', target64])
    out = []
    all_fields = []
    lc_cmd = "logcat -d -b main --pid={} -e '^TYPECOUNT'".format(pid).split(' ')
    for l in device.shell(lc_cmd)[0].strip().split('\n'):
      out.append(l.split("\t")[1:])
    if len(out) != 0:
      return out
    else:
      return None
  except adb.device.ShellError as e:
    print("failed on pid " + repr(pid) + " because " + repr(e))
    return None

if __name__ == '__main__':
  main()
