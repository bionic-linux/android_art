/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <stdlib.h>

#include "android/set_abort_message.h"

void AHeapProfileEnableCallbackInfo_getSamplingInterval() {
  android_set_abort_message("Fake!");
  abort();
}

void AHeapProfile_reportSample() {
  android_set_abort_message("Fake!");
  abort();
}

void AHeapProfile_reportFree() {
  android_set_abort_message("Fake!");
  abort();
}

void AHeapProfile_reportAllocation() {
  android_set_abort_message("Fake!");
  abort();
}

void AHeapProfile_registerHeap() {
  android_set_abort_message("Fake!");
  abort();
}

void AHeapInfo_setEnabledCallback() {
  android_set_abort_message("Fake!");
  abort();
}

void AHeapInfo_setDisabledCallback() {
  android_set_abort_message("Fake!");
  abort();
}

void AHeapInfo_create() {
  android_set_abort_message("Fake!");
  abort();
}
