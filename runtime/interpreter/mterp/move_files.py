#!/usr/bin/env python

import sys, re, os
from os import listdir
from collections import defaultdict

categories = [
  ("_float|_double|^fbinop|^funop|^fcmp|^fcvt|^fpcvt|^fpcmp|^sse", "floating_point.S"),
  ("^bincmp|^zcmp|_goto|_if|_switch|_return|_throw", "control_flow.S"),
  ("^field|_iget|_iput|_sget|_sput|_new_instance|_check_cast|_instance_of", "object.S"),
  ("_aget|_aput|_array", "array.S"),
  ("^invoke|_invoke", "invoke.S"),
  ("^const|^unused|_const|_monitor|_move|_unused|_nop", "other.S"),
  ("^header|^entry|^instruction|^alt_stub|^fallback|^footer|^close_cfi", "main.S"),
  ("^binop|^unop|^bindiv|^shop|^shiftWide|_int|_long", "arithmetic.S"),
  ("", None),
]

for arch in ["arm", "arm64", "mips", "mips64", "x86", "x86_64"]:
  output_files = defaultdict(dict)
  for file in listdir(arch):
    for category in categories:
      if re.search(category[0], file):
        output_files[category[1]][file] = True
        break
  for output_file, source_files in output_files.items():
    print(output_file)
    source_files = sorted(source_files)
    source_files = sorted(source_files, key=lambda src: src.startswith("op_"))
    source_files = sorted(source_files, key=lambda src: src != "entry.S")  # Move to top
    source_files = sorted(source_files, key=lambda src: src != "header.S")  # Move to top
    output = ""
    for source_file in source_files:
      print("  " + source_file)
      src = open(arch + "/" + source_file, "r")
      if len(output) != 0:
        output = output + '\n'
      output = output + src.read()
      os.remove(arch + "/" + source_file)
    out = open(arch + "/" + output_file, "w")
    out.write(output)
