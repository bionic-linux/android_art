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
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.PixelFormat;
import android.graphics.SurfaceTexture;
import android.os.Bundle;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.TextureView;
import android.view.View;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.TextView;

public class MainActivity extends Activity {

  Bitmap mBitmap;
  TextureView mTextureView;
  SurfaceView mSurfaceView;
  TextView mTextView;
  int mTextViewClicks = 0;

  @Override
  public void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);

    // For Bitmap Model Test:
    ClassLoader loader = MainActivity.class.getClassLoader();
    mBitmap = BitmapFactory.decodeStream(loader.getResourceAsStream("aahat.png"), null, null);

    // For So Model Test:
    Log.i("AmmTest", "onCreate, the answer is: " + NativeLib.aNativeFunction());

    // For Graphics Model Test:
    mTextureView = new TextureView(this);
    mSurfaceView = new SurfaceView(this);

    mTextureView.setSurfaceTextureListener(new TextureView.SurfaceTextureListener() {
      public void onSurfaceTextureAvailable(SurfaceTexture surface, int width, int height) {
        Log.i("AmmTest", "onSurfaceTextureAvailable");

        updateTextureView();
      }

      public void onSurfaceTextureSizeChanged(SurfaceTexture surface, int width, int height) {
        Log.i("AmmTest", "onSurfaceTextureSizeChanged to " + width + " x " + height);
      }

      public boolean onSurfaceTextureDestroyed(SurfaceTexture surface) {
        Log.i("AmmTest", "onSurfaceTextureDestroyed");
        return true;
      }

      public void onSurfaceTextureUpdated(SurfaceTexture surface) {
        Log.i("AmmTest", "onSurfaceTextureUpdated");
        if (mTextViewClicks < 10) {
          // Force full graphics buffering.
          updateSurfaceView();
          updateTextureView();
          mTextViewClicks++;
          mTextView.setText("hello " + mTextViewClicks);
        }
      }
    });

    SurfaceHolder holder = mSurfaceView.getHolder();
    holder.setFormat(PixelFormat.RGBA_4444);
    holder.addCallback(new SurfaceHolder.Callback() {
      public void surfaceCreated(SurfaceHolder holder) {
        Log.i("AmmTest", "surfaceCreated");

        updateSurfaceView();
      }

      public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        Log.i("AmmTest", "surfaceChanged");
      }

      public void surfaceDestroyed(SurfaceHolder holder) {
        Log.i("AmmTest", "surfaceDestroyed");
      }
    });

    WindowManager wm = getSystemService(WindowManager.class);
    mTextView = new TextView(this);
    mTextView.setText("hello 0");
    mTextView.setBackgroundColor(0xffff0000);
    mTextView.setOnClickListener(new View.OnClickListener() {
      public void onClick(View v) {
        mTextViewClicks++;
        mTextView.setText("hello " + mTextViewClicks);
      }
    });

    LinearLayout ll = new LinearLayout(this);
    ll.addView(mTextureView, 200, 500);
    ll.addView(mSurfaceView, 240, 250);
    setContentView(ll);

    WindowManager.LayoutParams layout = new WindowManager.LayoutParams();
    layout.width = 122;
    layout.height = 152;
    // layout.format = PixelFormat.RGBA_4444;
    wm.addView(mTextView, layout);
  }

  public void clickTextureView(View v) {
    updateTextureView();
  }

  public void clickSurfaceView(View v) {
    updateSurfaceView();
  }

  private int tr = 255;
  private int tg = 255;
  private int tb = 0;

  private void updateTextureView() {
    Log.i("AmmTest", "updateTextureView");
    Canvas canvas = mTextureView.lockCanvas();
    if (canvas != null) {
      canvas.drawRGB(tr, tg, tb);
      int tmp = tr;
      mTextureView.unlockCanvasAndPost(canvas);
      tr = tg;
      tg = tb;
      tb = tmp;
    }
  }

  private int sr = 255;
  private int sg = 0;
  private int sb = 255;

  private void updateSurfaceView() {
    Log.i("AmmTest", "updateSurfaceView: isHardwareAccelerated="
        + mSurfaceView.isHardwareAccelerated());
    SurfaceHolder holder = mSurfaceView.getHolder();
    // Canvas canvas = holder.lockCanvas();
    Canvas canvas = holder.lockHardwareCanvas();
    if (canvas != null) {
      canvas.drawRGB(sr, sg, sb);
      int tmp = sr;
      holder.unlockCanvasAndPost(canvas);
      sr = sg;
      sg = sb;
      sb = tmp;
    }
  }
}

