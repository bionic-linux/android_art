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
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
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
import java.io.IOException;
import java.net.URL;
import java.util.Enumeration;
import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

public class AmmTest {

  // Cache the snapshot for performance reasons.
  // Access the snapshot using the getSnapshot() method.
  private static Snapshot snapshot = null;

  private static Snapshot getSnapshot() {
    if (snapshot == null) {
      Instrumentation instrumentation = InstrumentationRegistry.getInstrumentation();
      Intent intent = new Intent(instrumentation.getTargetContext(), MainActivity.class);
      Activity activity = instrumentation.startActivitySync(intent);
      snapshot = ActionableMemoryMetric.getStandardMetric().sample();
      activity.finish();
    }
    return snapshot;
  }

  private static ModelSnapshot getModelSnapshot(String name) {
    ModelSnapshot model = null;
    for (ModelSnapshot ms : getSnapshot().models) {
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
    assertNotNull(getSnapshot());
  }

  @Test
  public void bitmap() {
    ModelSnapshot bitmapModelSnapshot = getModelSnapshot("Bitmap");

    // Find the matching instance.
    BitmapModel.Instance bitmapModelInstance = null;
    for (ModelInstance mi : bitmapModelSnapshot.instances) {
      BitmapModel.Instance bmi = (BitmapModel.Instance)mi;
      if (bmi.width == 132 && bmi.height == 154) {
        // There should only be one model instance for the bitmap
        assertNull(bitmapModelInstance);
        bitmapModelInstance = bmi;
      }
    }
    assertNotNull(bitmapModelInstance);
    assertEquals(4 * 132 * 154, bitmapModelInstance.size);

    byte[] png = bitmapModelInstance.png;
    assertEquals('P', png[1]);
    assertEquals('N', png[2]);
    assertEquals('G', png[3]);

    Bitmap decoded = BitmapFactory.decodeByteArray(png, 0, png.length);
    assertEquals(132, decoded.getWidth());
    assertEquals(154, decoded.getHeight());
    assertEquals(0xFFFFFF, 0xFFFFFF & decoded.getPixel(20, 20));

    // TODO: Why are the colors of these pixels slightly off?
    // assertEquals(0x4285F4, 0xFFFFFF & decoded.getPixel(64, 20));
    // assertEquals(0xA4CA39, 0xFFFFFF & decoded.getPixel(64, 90));
    assertEquals(0x4086F5, 0xFFFFFF & decoded.getPixel(64, 20));
    assertEquals(0xA5CB37, 0xFFFFFF & decoded.getPixel(64, 90));
  }

  @Test
  public void dexcode() throws IOException {
    // Determine the size of the amm-test.jar dex code, and verify that shows
    // up in the standard metric.
    ModelSnapshot dexcodeModelSnapshot = getModelSnapshot("DexCode");

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
    URL classesDex = null;
    ClassLoader loader = AmmTest.class.getClassLoader();
    for (Enumeration<URL> e = loader.getResources("classes.dex"); e.hasMoreElements();) {
      URL url = e.nextElement();
      if (url.getPath().endsWith("base.apk!/classes.dex")) {
        classesDex = url;
        break;
      }
    }
    assertNotNull(classesDex);
    long size = classesDex.openConnection().getContentLength();

    // Be a little bit lenient in case we have to use an approximation.
    // As long as we are in the right ballpark, things should be okay.
    assertTrue(dexcodeModelInstance.size >= size);
    assertTrue(dexcodeModelInstance.size < size * 1.2);
  }

  @Test
  public void socode() {
    ModelSnapshot soModelSnapshot = getModelSnapshot("SoCode");

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
    ModelSnapshot ms = getModelSnapshot("Graphics");

    boolean textureViewModeled = false;
    boolean addedViewModeled = false;
    boolean surfaceViewModeled = false;
    for (ModelInstance mi : ms.instances) {
      GraphicsModel.Instance gmi = (GraphicsModel.Instance)mi;
      if (gmi.width == 200 && gmi.height == 500) {
        // This is the 200 x 500 texture view.
        assertTrue(gmi.object instanceof TextureView);
        assertEquals(2 * (4 * 200 * 500), gmi.size);
        assertFalse(textureViewModeled);
        textureViewModeled = true;
      }

      // This is the 122 x 152 WindowManager added text view.
      if (gmi.width == 122 && gmi.height == 152) {
        assertEquals(3 * (4 * 122 * 152), gmi.size);
        assertFalse(addedViewModeled);
        addedViewModeled = true;
      }

      if (gmi.width == 240 && gmi.height == 250) {
        // This is the 64 x 128 SurfaceView.
        assertEquals(3 * (4 * 240 * 250), gmi.size);
        assertFalse(surfaceViewModeled);
        surfaceViewModeled = true;
      }
    }
    assertTrue(textureViewModeled);
    assertTrue(addedViewModeled);   // TODO: only works if screen on.
    assertTrue(surfaceViewModeled); // TODO: only works if screen on.
  }

  // TODO:
  //  * What if bitmap pixel data is in Java heap, should model return 0
  //    for size then?
  //  * What if dex file is extracted in memory, or the oat file contains it
  //    instead of a vdex file?
}
