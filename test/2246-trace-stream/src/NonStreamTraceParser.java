/*
 * Copyright (C) 2023 The Android Open Source Project
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

public class NonStreamTraceParser extends BaseTraceParser {

    public void CheckTraceFileFormat(File file, int expected_version) throws Exception {
        InitializeParser(file);

        // On non-streaming formats, the file starts with information about options and threads and
        // method information.
        // Read version string and version.
        String line = readLine();
        if (!line.equals("*version")) {
            throw new Exception("Trace doesn't start with version. Starts with: " + line);
        }
        int version = Integer.decode(readLine());
        if (version != expected_version) {
            throw new Exception("Unexpected version: " + version);
        }

        // Record num_entries and ignore next few options that provides some metadata.
        line = readLine();
        int num_entries = 0;
        while (!line.startsWith(START_SECTION_ID)) {
            if (line.startsWith("num-method-calls")) {
                String[] tokens = line.split("=");
                num_entries = Integer.decode(tokens[1]);
            }
            line = readLine();
        }

        // This should be threads.
        if (!line.equals(THREADS_SECTION_ID)) {
            throw new Exception("Missing information about threads " + line);
        }

        line = readLine();
        while (!line.startsWith(START_SECTION_ID)) {
            String[] thread_info = line.split("\t");
            thread_id_map.put(Integer.decode(thread_info[0]), thread_info[1]);
            line = readLine();
        }

        // Parse methods
        if (!line.equals(METHODS_SECTION_ID)) {
            throw new Exception("Missing information about methods " + line);
        }

        line = readLine();
        while (!line.startsWith(START_SECTION_ID)) {
            String[] method_info = line.split("\t", 2);
            method_id_map.put(Integer.decode(method_info[0]), method_info[1]);
            line = readLine();
        }

        // This should be end
        if (!line.equals(END_SECTION_ID)) {
            throw new Exception("Missing end after methods " + line);
        }

        // Validate the actual data.
        validateTraceHeader(expected_version);
        boolean has_entries = true;
        boolean seen_stop_tracing_method = false;
        for (int i = 0; i < num_entries; i++) {
            int thread_id = GetEntryHeader();
            String event_string = ProcessEventEntry(thread_id);
            // Ignore daemons (ex: heap task daemon, reference queue daemon) because they may not
            // be deterministic.
            if (ShouldIgnoreThread(thread_id)) {
                continue;
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
        closeFile();
    }
}
