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

 import annotations.ConstantMethodType;

import java.lang.invoke.MethodHandle;

import annotations.ConstantMethodHandle;


public class FieldAsVirtual {


    static {
        // Commenting out due to R8 breakage: https://paste.googleplex.com/5366765195624448?raw
        // MethodHandle mh = methodHandle();
    }

    @ConstantMethodHandle(
        kind = ConstantMethodHandle.INVOKE_VIRTUAL,
        owner = "FieldAsVirtual",
        fieldOrMethodName = "field",
        descriptor = "I")
    private static MethodHandle methodHandle() {
        unreachable();
        return null;
    }

    public int field;

    private static void unreachable() {
        throw new Error("unreachable!");
    }

}
