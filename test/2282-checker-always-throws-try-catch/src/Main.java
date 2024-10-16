/*
 * Copyright (C) 2024 The Android Open Source Project
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

public class Main {
    public static void main(String[] args) {
        $noinline$testAlwaysThrows();
    }

    private static void alwaysThrows() throws Exception {
        try {
            throw new Error();
        } catch (Error expected) {
            throw new Exception();
        }
    }

    /// CHECK-START: void Main.$noinline$testAlwaysThrows() inliner (after)
    /// CHECK: InvokeStaticOrDirect method_name:Main.alwaysThrows always_throws:true
    private static void $noinline$testAlwaysThrows() {
        try {
            alwaysThrows();
            System.out.println("alwaysThrows didn't throw");
        } catch (Exception expected) {
        }
    }
}
