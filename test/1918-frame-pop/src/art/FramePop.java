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

package art;

import java.lang.reflect.Method;

public class FramePop {

  public static native void enableFramePopEvent(Class klass, Method method, Thread thr)
      throws Exception;
  public static void notifyFramePop(Thread target, int depth) throws Exception {
    notifyFramePop(0, target, depth);
  }
  public static native void notifyFramePop(long env, Thread target, int depth) throws Exception;
  // TODO Rewrite all of this so it's more separated out.
  public static native long makeJvmtiEnvForFramePop();
}
