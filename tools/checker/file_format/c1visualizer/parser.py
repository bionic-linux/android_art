# Copyright (C) 2014 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from common.logger                   import Logger
from file_format.common              import SplitStream
from file_format.c1visualizer.struct import C1visualizerFile, C1visualizerPass

import re

def ParseC1visualizerSeekableStream(fileName, seekableStream):
  c1File = C1visualizerFile(fileName)
  AddISAFeaturesFromStream(c1File, seekableStream)

  # AddISAFeaturesFromStream read the first 5 lines of the file and moved the stream pointer to the
  # the 6th line.
  # The first optimisation pass will always be after the ISA features metadata block (if present),
  # or at the beginning of the file (if the ISA features metadata block is not present).
  # If those 5 lines contained the ISA features metadata block, the stream pointer is already
  # pointing to the first optimisation pass, which will be read by AddPassesFromStream.
  # If they didn't contain the ISA features, we need to jump back to the beginning of the file.
  if not c1File.instructionSetFeatures:
    seekableStream.seek(0)

  AddPassesFromStream(fileName, c1File, seekableStream)
  return c1File

# AddISAFeaturesFromStream parses the first 5 lines of the stream and stores the ISA features (when
# present) in the c1File.
def AddISAFeaturesFromStream(c1File, stream):
  # If the .cfg includes the list of ISA features, we expect to find them at the beginning of the
  # file as a fake compilation block in the following form:
  # begin_compilation
  #   name "isa_features:feature1,-feature2"
  #   method "isa_features:feature1,-feature2"
  #   date 1580721972
  # end_compilation

  # Read the file header, which is composed of 5 lines.
  # Line at idx 1 is potentially containing the ISA features.
  lines = ReadLines(stream, 5)
  if not lines:
    return
  featuresLine = lines[1]

  # If the header contains the ISA features, store them in c1File.
  if re.match('name "isa_features:[\w,-]+"', featuresLine):
    methodName = featuresLine.split("\"")[1]
    rawFeatures = methodName.split("isa_features:")[1].split(",")

    # Create a map of features in the form {featureName: isEnabled}.
    features = {}
    for rf in rawFeatures:
      featureName = rf
      isEnabled = True
      # A '-' in front of the feature name indicates that the feature wasn't enabled at compile
      # time.
      if rf[0] == '-':
        featureName = rf[1:]
        isEnabled = False
      features[featureName] = isEnabled

    c1File.setISAFeatures(features)

# ReadLines reads n non-empty lines from the current cursor position, strips them and returns
# them as a list. Empty lines will be skipped.
def ReadLines(stream, n):
  if n == 0:
    return []
  lines = []
  readLines = 0
  for line in stream:
    line = line.strip()
    if not line:
      continue
    lines.append(line)
    readLines += 1
    if readLines == n:
      break
  return lines

class C1ParserState:
  OutsideBlock, InsideCompilationBlock, StartingCfgBlock, InsideCfgBlock = range(4)

  def __init__(self):
    self.currentState = C1ParserState.OutsideBlock
    self.lastMethodName = None

def __parseC1Line(line, lineNo, state, fileName):
  """ This function is invoked on each line of the output file after the initial
      chunk that contains the list of ISA features (if exists) and returns
      a triplet which instructs the parser how the line should be handled. If the
      line is to be included in the current group, it is returned in the first
      value. If the line starts a new output group, the name of the group is
      returned in the second value. The third value is only here to make the
      function prototype compatible with `SplitStream` and is always set to
      `None` here.
  """
  if state.currentState == C1ParserState.StartingCfgBlock:
    # Previous line started a new 'cfg' block which means that this one must
    # contain the name of the pass (this is enforced by C1visualizer).
    if re.match("name\s+\"[^\"]+\"", line):
      # Extract the pass name, prepend it with the name of the method and
      # return as the beginning of a new group.
      state.currentState = C1ParserState.InsideCfgBlock
      return (None, state.lastMethodName + " " + line.split("\"")[1], None)
    else:
      Logger.fail("Expected output group name", fileName, lineNo)

  elif state.currentState == C1ParserState.InsideCfgBlock:
    if line == "end_cfg":
      state.currentState = C1ParserState.OutsideBlock
      return (None, None, None)
    else:
      return (line, None, None)

  elif state.currentState == C1ParserState.InsideCompilationBlock:
    # Search for the method's name. Format: method "<name>"
    if re.match("method\s+\"[^\"]*\"", line):
      methodName = line.split("\"")[1].strip()
      if not methodName:
        Logger.fail("Empty method name in output", fileName, lineNo)
      state.lastMethodName = methodName
    elif line == "end_compilation":
      state.currentState = C1ParserState.OutsideBlock
    return (None, None, None)

  else:
    assert state.currentState == C1ParserState.OutsideBlock
    if line == "begin_cfg":
      # The line starts a new group but we'll wait until the next line from
      # which we can extract the name of the pass.
      if state.lastMethodName is None:
        Logger.fail("Expected method header", fileName, lineNo)
      state.currentState = C1ParserState.StartingCfgBlock
      return (None, None, None)
    elif line == "begin_compilation":
      state.currentState = C1ParserState.InsideCompilationBlock
      return (None, None, None)
    else:
      Logger.fail("C1visualizer line not inside a group", fileName, lineNo)

def AddPassesFromStream(fileName, c1File, stream):
  state = C1ParserState()
  fnProcessLine = lambda line, lineNo: __parseC1Line(line, lineNo, state, fileName)
  fnLineOutsideChunk = lambda line, lineNo: \
      Logger.fail("C1visualizer line not inside a group", fileName, lineNo)
  for passName, passLines, startLineNo, testArch in \
      SplitStream(stream, fnProcessLine, fnLineOutsideChunk):
    C1visualizerPass(c1File, passName, passLines, startLineNo + 1)