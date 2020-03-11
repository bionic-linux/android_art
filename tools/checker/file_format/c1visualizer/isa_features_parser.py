# Copyright (C) 2020 The Android Open Source Project
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

import re

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