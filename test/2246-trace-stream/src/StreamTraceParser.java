/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import java.io.File;
import java.io.IOException;

public class StreamTraceParser extends BaseTraceParser {

    public void CheckTraceFileFormat(File file, int expected_version) throws Exception {
        InitializeParser(file);

        validateTraceHeader(expected_version);
        boolean has_entries = true;
        boolean seen_stop_tracing_method = false;
        while (has_entries) {
            int header_type = GetEntryHeader();
            switch (header_type) {
                case 1:
                    ProcessMethodInfoEntry();
                    break;
                case 2:
                    ProcessThreadInfoEntry();
                    break;
                case 3:
                    // TODO(mythria): Add test to also check format of trace summary.
                    has_entries = false;
                    break;
                default:
                    String event_string = ProcessEventEntry(header_type);
                    if (ShouldIgnoreThread(header_type)) {
                        break;
                    }
                    // Ignore events after method tracing was stopped. The code that is executed
                    // later could be non-deterministic.
                    if (!seen_stop_tracing_method) {
                        System.out.println(event_string);
                    }
                    if (event_string.contains("Main$VMDebug $noinline$stopMethodTracing")) {
                        seen_stop_tracing_method = true;
                    }
            }
        }
        closeFile();
    }
}
