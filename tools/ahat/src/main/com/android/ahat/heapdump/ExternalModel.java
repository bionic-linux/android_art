/*
 * Copyright (C) 2018 The Android Open Source Project
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

package com.android.ahat.heapdump;

/**
 * A model for the external size of an ahat instance.
 * The model is derived from an actionable memory metric snapshot if present
 * in the heap dump, otherwise from use of native allocation registry.
 */
public class ExternalModel {
  /**
   * The size of external memory use being modeled.
   */
  public final long size;

  /**
   * The source of information about the external model.
   * For actionable memory metric, this will be the ModelInstance. For
   * registered native allocation this is the relevant Cleaner instance.
   */
  public final AhatInstance source;

  /**
   * Constructs a new ExternalModel.
   *
   * @param size the modeled size
   * @param source the source of the external model information
   */
  ExternalModel(long size, AhatInstance source) {
    this.size = size;
    this.source = source;
  }

  /**
   * Describes the source of the "Modeled External Size" information.
   */
  public static enum Source {
    /**
     * No source of modeled external size information was found.
     */
    NONE,

    /**
     * Registered native size is used as the source of the modeled external
     * size.
     */
    REGISTERED_NATIVE,

    /**
     * An actionable memory metric snapshot is used as the source of the
     * modeled external size.
     */
    ACTIONABLE_MEMORY_METRIC;
  }
}
