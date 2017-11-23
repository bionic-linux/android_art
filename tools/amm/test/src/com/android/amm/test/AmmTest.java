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

package com.android.amm.test;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.graphics.BitmapFactory;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.support.test.InstrumentationRegistry;
import android.view.TextureView;
import com.android.amm.ActionableMemoryMetric;
import com.android.amm.BitmapModel;
import com.android.amm.DexCodeModel;
import com.android.amm.GraphicsModel;
import com.android.amm.ModelInstance;
import com.android.amm.ModelSnapshot;
import com.android.amm.Snapshot;
import com.android.amm.SoCodeModel;
import org.junit.Rule;
import org.junit.Test;
import java.io.IOException;
import java.net.URL;
import java.util.Enumeration;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

public class AmmTest {

  private ModelSnapshot getModelSnapshot(Snapshot snapshot, String name) {
    ModelSnapshot model = null;
    for (ModelSnapshot ms : snapshot.models) {
      if (name.equals(ms.model.name)) {
        // There should only be one model snapshot present.
        assertNull(model);
        model = ms;
      }
    }
    assertNotNull(model);
    return model;
  }

  @Test
  public void basic() {
    // Verify sampling the snapshot does not crash.
    Snapshot snapshot = ActionableMemoryMetric.getStandardMetric().sample();
    assertNotNull(snapshot);
  }

  @Test
  public void bitmap() {
    // Allocate a bitmap and check that a model for it shows up in the
    // standard metric.
    int r = Color.RED;
    int g = Color.GREEN;
    int b = Color.BLUE;
    int w = Color.WHITE;
    int[] colors = new int[]{
      r, r, r, r, g, g, g, g,
      r, r, r, r, g, g, g, g,
      r, r, r, r, g, g, g, g,
      w, w, w, w, b, b, b, b,
      w, w, w, w, b, b, b, b,
      w, w, w, w, b, b, b, b
    };
    Bitmap bitmap = Bitmap.createBitmap(colors, 8, 6, Bitmap.Config.ARGB_8888);
    assertNotNull(bitmap);

    Snapshot snapshot = ActionableMemoryMetric.getStandardMetric().sample();
    ModelSnapshot bitmapModelSnapshot = getModelSnapshot(snapshot, "Bitmap");

    // Find the matching instance.
    BitmapModel.Instance bitmapModelInstance = null;
    for (ModelInstance mi : bitmapModelSnapshot.instances) {
      if (mi.object == bitmap) {
        // There should only be one model instance for the bitmap
        assertNull(bitmapModelInstance);
        bitmapModelInstance = (BitmapModel.Instance)mi;
      }
    }
    assertNotNull(bitmapModelInstance);
    assertEquals(8, bitmapModelInstance.width);
    assertEquals(6, bitmapModelInstance.height);
    assertEquals(4 * 8 * 6, bitmapModelInstance.size);

    byte[] png = bitmapModelInstance.png;
    assertEquals('P', png[1]);
    assertEquals('N', png[2]);
    assertEquals('G', png[3]);

    Bitmap decoded = BitmapFactory.decodeByteArray(png, 0, png.length);
    assertEquals(8, decoded.getWidth());
    assertEquals(6, decoded.getHeight());
    assertEquals(r, decoded.getPixel(1, 1));
    assertEquals(w, decoded.getPixel(1, 5));
    assertEquals(g, decoded.getPixel(6, 1));
    assertEquals(b, decoded.getPixel(6, 5));
  }

  @Test
  public void dexcode() throws IOException {
    // Determine the size of the amm-test.jar dex code, and verify that shows
    // up in the standard metric.
    Snapshot snapshot = ActionableMemoryMetric.getStandardMetric().sample();
    ModelSnapshot dexcodeModelSnapshot = getModelSnapshot(snapshot, "DexCode");

    // Find the matching instance.
    DexCodeModel.Instance dexcodeModelInstance = null;
    for (ModelInstance mi : dexcodeModelSnapshot.instances) {
      DexCodeModel.Instance dmi = (DexCodeModel.Instance)mi;
      if (dmi.name.endsWith("base.apk")) {
        dexcodeModelInstance = dmi;
      }
    }
    assertNotNull(dexcodeModelInstance);

    // Check the size is as we expect.
    URL classes_dex = null;
    ClassLoader loader = AmmTest.class.getClassLoader();
    for (Enumeration<URL> e = loader.getResources("classes.dex"); e.hasMoreElements();) {
      URL url = e.nextElement();
      if (url.getPath().endsWith("base.apk!/classes.dex")) {
        classes_dex = url;
        break;
      }
    }
    assertNotNull(classes_dex);
    long size = classes_dex.openConnection().getContentLength();

    // Be a little bit lenient in case we have to use an approximation.
    // As long as we are in the right ballpark, things should be okay.
    assertTrue(dexcodeModelInstance.size >= size);
    assertTrue(dexcodeModelInstance.size < size * 1.2);
  }

  @Test
  public void socode() {
    // Run the native library to ensure it is loaded.
    assertEquals(42, NativeLib.aNativeFunction());

    Snapshot snapshot = ActionableMemoryMetric.getStandardMetric().sample();
    ModelSnapshot soModelSnapshot = getModelSnapshot(snapshot, "SoCode");

    assertEquals(1, soModelSnapshot.instances.length);
    SoCodeModel.Instance soModelInstance = (SoCodeModel.Instance)soModelSnapshot.instances[0];
    assertNotNull(soModelInstance.object);
    SoCodeModel.Entry ammentry = null;
    for (SoCodeModel.Entry entry : soModelInstance.entries) {
      if (entry.name.endsWith("libammtestjni.so")) {
        assertNull(ammentry);
        ammentry = entry;
      }
    }
    assertNotNull(ammentry);

    // Expected size:
    //  1 page for text and misc.
    //  1 page for cinit.
    //  2 pages for init.
    //  3 pages for uninit.
    //  1 page for additional data.
    //  TODO: 1 extra page for something else?
    assertEquals(9 * 4096, ammentry.size);
  }

  @Test
  public void graphics() {
    // Launch the MainActivity to set up the expected use of graphics.
    Instrumentation instrumentation = InstrumentationRegistry.getInstrumentation();
    Intent intent = new Intent(instrumentation.getTargetContext(), MainActivity.class);
    Activity activity = instrumentation.startActivitySync(intent);

    Snapshot snapshot = ActionableMemoryMetric.getStandardMetric().sample();
    ModelSnapshot ms = getModelSnapshot(snapshot, "Graphics");

    boolean textureViewModeled = false;
    boolean addedViewModeled = false;
    boolean surfaceViewModeled = false;
    for (ModelInstance mi : ms.instances) {
      GraphicsModel.Instance gmi = (GraphicsModel.Instance)mi;
      if (gmi.object instanceof TextureView) {
        // This is the 256 x 512 texture view.
        assertEquals(256, gmi.width);
        assertEquals(512, gmi.height);
        assertEquals(2 * (4 * 256 * 512), gmi.size);
        assertFalse(textureViewModeled);
        textureViewModeled = true;
      }

      // This is the 128 x 128 WindowManager added text view.
      if (gmi.width == 128 && gmi.height == 128) {
        assertEquals(3 * (4 * 128 * 128), gmi.size);
        assertFalse(addedViewModeled);
        addedViewModeled = true;
      }

      if (gmi.width == 64 && gmi.height == 128) {
        // This is the 64 x 128 SurfaceView.
        assertEquals(2 * (4 * 64 * 128), gmi.size);
        assertFalse(surfaceViewModeled);
        surfaceViewModeled = true;
      }
    }
    assertTrue(textureViewModeled);
    assertTrue(addedViewModeled);   // TODO: only works if screen on.
    assertTrue(surfaceViewModeled); // TODO: only works if screen on.

    activity.finish();
  }

  // TODO:
  //  * What if bitmap pixel data is in Java heap, should model return 0
  //    for size then?
  //  * What if dex file is extracted in memory, or the oat file contains it
  //    instead of a vdex file?
  //  * Can we avoid repeatedly sampling the metric for each test? Should we?
  //  * Come up with a better way of identifying the Graphics model instances.
}
