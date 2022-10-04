/*
 * Copyright (C) 2022 The Android Open Source Project
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

interface Itf {
  public default String bar() {
    return "Itf";
  }
}

interface OtherItf {
  // javac will complain if OtherItf and Itf are compiled together. What will be loaded at runtime
  // will actually be src2/OtherItf
  // public default String bar() {
  //   return "OtherItf";
  // }
}

// The method bar will be a default conflict for this class. That used to make
// the class linker re-allocate its ArtMethod array, and calling CopyFrom.
// The bug was that CopyFrom was overwriting the imt index of interace methods,
// and for this example `foo`.
interface SubItf extends Itf, OtherItf {
  public String foo();
}

class Impl implements SubItf {
  public String foo() {
    return "Impl";
  }
}

public class Main {
  static SubItf itf = new Impl();
  public static void main(String[] args) throws Exception {
    // Loop enough to trigger the native OOME.
    for (int i = 0; i < 50000; ++i) {
      // Because the imt index was overwritten to 0, this call ended up
      // in the conflict trampoline which wrongly updated the 0th entry
      // of the imt table. This lead to this call always calling the
      // conflict trampoline.
      itf.foo();
    }
  }
}
