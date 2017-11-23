/*
 * Copyright (C) 2017 The Android Open Source Project
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

package com.android.amm;

public class Snapshot {
  public long total;
  public ModelSnapshot[] models;

  private String dump(boolean detailed) {
    StringBuilder sb = new StringBuilder();
    sb.append(" AMM(KB)\n");
    sb.append("--------\n");
    for (ModelSnapshot ms : models) {
      sb.append(String.format("%8d  %s\n", ms.total / 1024, ms.model.name));
      if (detailed) {
        for (ModelInstance mi : ms.instances) {
          sb.append(String.format("%8s  %8d  %s\n", "", mi.size / 1024, mi.toString()));
        }
      }
    }
    sb.append(String.format("%8d  TOTAL\n", total / 1024));
    return sb.toString();
  }

  public String detail() {
    return dump(true);
  }

  public String summary() {
    return dump(false);
  }
}
