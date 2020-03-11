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

from file_format.c1visualizer.isa_features_parser  import AddISAFeaturesFromStream
from file_format.c1visualizer.passes_parser        import AddPassesFromStream
from file_format.c1visualizer.struct               import C1visualizerFile

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