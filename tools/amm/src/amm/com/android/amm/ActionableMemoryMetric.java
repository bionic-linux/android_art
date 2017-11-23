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

import android.util.Log;
import dalvik.system.VMDebug;

public class ActionableMemoryMetric {
  private final Model[] models;

  private static ActionableMemoryMetric STANDARD_METRIC
    = new ActionableMemoryMetric(
        new JavaModel(),
        new BitmapModel(),
        new GraphicsModel(),
        new DexCodeModel(),
        new SoCodeModel());

  public static ActionableMemoryMetric getStandardMetric() {
    return STANDARD_METRIC;
  }

  public ActionableMemoryMetric(Model... models) {
    this.models = models;
  }

  public Snapshot sample() {
    Snapshot snapshot = new Snapshot();
    snapshot.total = 0;
    snapshot.models = new ModelSnapshot[models.length];

    Log.i("AMM", "Sampling actionable memory metric...");
    for (int i = 0; i < models.length; ++i) {
      snapshot.models[i] = models[i].sample();
      snapshot.total += snapshot.models[i].total;
    }
    Log.i("AMM", "Sampled actionable memory metric value: " + snapshot.total + " bytes");
    return snapshot;
  }
}
