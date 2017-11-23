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

import android.graphics.Canvas;
import android.view.RenderNode;
import android.view.Surface;
import android.view.SurfaceView;
import android.view.TextureView;
import android.view.ThreadedRenderer;
import java.lang.reflect.Field;
import java.util.ArrayList;

public class GraphicsModel extends Model {

  public static class Instance extends ModelInstance {
    public final int width;
    public final int height;

    public Instance(Object obj, int width, int height, long size) {
      super(obj, size);
      this.width = width;
      this.height = height; 
    }

    @Override public String toString() {
      return String.format("%s %4d x %4d", object.getClass().getName(), width, height);
    }
  }

  public GraphicsModel() {
    super("Graphics");
  }

  @Override public ModelSnapshot sample() {
    Object[][] insts = ReflectiveGetInstances.getInstancesOfClasses(
        new Class[]{ RenderNode.class, ThreadedRenderer.class}, true);

    ArrayList<ModelInstance> minsts = new ArrayList<ModelInstance>();
    ModelSnapshot snapshot = new ModelSnapshot();
    snapshot.total = 0;
    snapshot.model = this;
    for (int i = 0; i < insts[0].length; ++i) {
      Instance inst = textureOrSurfaceViewInstance((RenderNode)insts[0][i]);
      if (inst != null) {
        snapshot.total += inst.size;
        minsts.add(inst);
      }
    }

    for (int i = 0; i < insts[1].length; ++i) {
      Instance inst = threadedRendererInstance((ThreadedRenderer)insts[1][i]);
      if (inst != null) {
        snapshot.total += inst.size;
        minsts.add(inst);
      }
    }

    snapshot.instances = minsts.toArray(new ModelInstance[minsts.size()]);
    return snapshot;
  }

  private Instance textureOrSurfaceViewInstance(RenderNode node) {
    try {
      Field fView = RenderNode.class.getDeclaredField("mOwningView");
      fView.setAccessible(true);

      Object view = fView.get(node);
      if (view instanceof TextureView) {
        TextureView tv = (TextureView)view;
        int width = tv.getWidth();
        int height = tv.getHeight();
        return new Instance(view, width, height, 2 * (4 * width * height));
      }

      if (view instanceof SurfaceView) {
        SurfaceView sv = (SurfaceView)view;
        Field fHwuiContext = Surface.class.getDeclaredField("mHwuiContext");
        fHwuiContext.setAccessible(true);
        Object hwuiContext = fHwuiContext.get(sv.getHolder().getSurface());
        if (hwuiContext != null) {
          int width = sv.getWidth();
          int height = sv.getHeight();
          return new Instance(hwuiContext, width, height, 2 * (4 * width * height));
        }
      }
      return null;
    } catch (ReflectiveOperationException roe) {
      throw new RuntimeException(roe);
      // roe.printStackTrace();
      // return null;
    }
  }

  private Instance threadedRendererInstance(ThreadedRenderer obj) {
    try {
      Field fWidth = ThreadedRenderer.class.getDeclaredField("mWidth");
      fWidth.setAccessible(true);

      Field fHeight = ThreadedRenderer.class.getDeclaredField("mHeight");
      fHeight.setAccessible(true);

      int width = fWidth.getInt(obj);
      int height = fHeight.getInt(obj);
      return new Instance(obj, width, height, 3 * (4 * width * height));
    } catch (ReflectiveOperationException roe) {
      throw new RuntimeException(roe);
      // roe.printStackTrace();
      // return null;
    }
  }
}
