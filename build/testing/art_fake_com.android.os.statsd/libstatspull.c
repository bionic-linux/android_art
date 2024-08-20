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

void AStatsManager_PullAtomMetadata_obtain() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsManager_PullAtomMetadata_release() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsManager_PullAtomMetadata_setCoolDownMillis() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsManager_PullAtomMetadata_getCoolDownMillis() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsManager_PullAtomMetadata_setTimeoutMillis() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsManager_PullAtomMetadata_getTimeoutMillis() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsManager_PullAtomMetadata_setAdditiveFields() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsManager_PullAtomMetadata_getNumAdditiveFields() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsManager_PullAtomMetadata_getAdditiveFields() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsEventList_addStatsEvent() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsManager_setPullAtomCallback() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsManager_clearPullAtomCallback() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsManager_addSubscription() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsManager_removeSubscription() {
  android_set_abort_message("Fake!");
  abort();
}

void AStatsManager_flushSubscription() {
  android_set_abort_message("Fake!");
  abort();
}
