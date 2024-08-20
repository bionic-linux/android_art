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

void AStatsEvent_obtain() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_build() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_write() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_release() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_setAtomId() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_writeInt32() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_writeInt64() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_writeFloat() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_writeBool() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_writeByteArray() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_writeString() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_writeAttributionChain() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_writeInt32Array() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_writeInt64Array() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_writeFloatArray() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_writeBoolArray() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_writeStringArray() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_addBoolAnnotation() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEvent_addInt32Annotation() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsSocket_close() {
  android_set_abort_message("Fake!");
  abort();
}
