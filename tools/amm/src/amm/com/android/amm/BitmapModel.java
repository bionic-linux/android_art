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

import android.graphics.Bitmap;
import java.io.ByteArrayOutputStream;

public class BitmapModel extends Model {

  public static class Instance extends ModelInstance {
    public final int width;
    public final int height;
    public final byte[] png;

    public Instance(Bitmap bitmap) {
      super(bitmap, bitmap.getAllocationByteCount());
      this.width = bitmap.getWidth();
      this.height = bitmap.getHeight();

      // TODO: Get a png for a scaled down version of the image rather than
      // the full sized image?
      ByteArrayOutputStream bos = new ByteArrayOutputStream();
      bitmap.compress(Bitmap.CompressFormat.PNG, 0, bos);
      this.png = bos.toByteArray();
    }

    @Override public String toString() {
      return String.format("%4d x %4d", width, height);
    }
  }

  public BitmapModel() {
    super("Bitmap");
  }

  @Override public ModelSnapshot sample() {
    Object[][] insts = ReflectiveGetInstances.getInstancesOfClasses(
        new Class[]{ Bitmap.class }, true);

    ModelSnapshot snapshot = new ModelSnapshot();
    snapshot.total = 0;
    snapshot.model = this;
    snapshot.instances = new ModelInstance[insts[0].length];
    for (int i = 0; i < snapshot.instances.length; ++i) {
      snapshot.instances[i] = new Instance((Bitmap)insts[0][i]);
      snapshot.total += snapshot.instances[i].size;
    }
    return snapshot;
  }
}
