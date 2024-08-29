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

import java.lang.invoke.MethodHandle;

import annotations.ConstantMethodHandle;

public class InvokeInterface {

    private int instanceField;

    private static void unreachable() {
        throw new AssertionError("unreachable!");
    }

    @ConstantMethodHandle(
        kind = ConstantMethodHandle.INVOKE_INTERFACE,
        owner = "InvokeInterface",
        fieldOrMethodName = "unreachable",
        descriptor = "()V")
    private static MethodHandle forStaticMethod() {
        unreachable();
        return null;
    }

    @ConstantMethodHandle(
        kind = ConstantMethodHandle.INVOKE_INTERFACE,
        owner = "java/lang/Object",
        fieldOrMethodName = "hashCode",
        descriptor = "()I")
    private static MethodHandle forInstanceMethod() {
        unreachable();
        return null;
    }

    @ConstantMethodHandle(
        kind = ConstantMethodHandle.INVOKE_INTERFACE,
        owner = "Main$I",
        fieldOrMethodName = "method",
        descriptor = "()V")
    private static MethodHandle inaccessibleInterfaceMethod() {
        unreachable();
        return null;
    }

    @ConstantMethodHandle(
        kind = ConstantMethodHandle.INVOKE_INTERFACE,
        owner = "Main$I",
        fieldOrMethodName = "defaultMethod",
        descriptor = "()V")
    private static MethodHandle inaccessibleDefaultInterfaceMethod() {
        unreachable();
        return null;
    }

    @ConstantMethodHandle(
        kind = ConstantMethodHandle.INVOKE_INTERFACE,
        owner = "Main$I",
        fieldOrMethodName = "privateMethod",
        descriptor = "()V")
    private static MethodHandle inaccessiblePrivateInterfaceMethod() {
        unreachable();
        return null;
    }

    public static void runTests() {
        try {
            InvokeInterfaceForStaticMethod.runTests();
            unreachable();
        } catch (IncompatibleClassChangeError | ClassFormatError expected) {}

        try {
            InvokeInterfaceForInstanceMethod.runTests();
            unreachable();
        } catch (IncompatibleClassChangeError | ClassFormatError expected) {}

        try {
            InvokeInterfaceForConstructor.runTests();
            unreachable();
        } catch (IncompatibleClassChangeError | ClassFormatError expected) {}

        try {
            InvokeInterfaceForClassInitializer.runTests();
            unreachable();
        } catch (IncompatibleClassChangeError | ClassFormatError expected) {}

        // TODO(b/297147201): the RI considers these instructions as invalid and commenting out any
        // of them will lead to CFE during InvokeInterface initialization. ART creates MH object
        // instances ignoring the fact that Main$I is inner private interface.
        /*
        try {
            inaccessibleInterfaceMethod();
            unreachable();
        } catch (IncompatibleClassChangeError expected) {}

        try {
            inaccessibleDefaultInterfaceMethod();
            unreachable();
        } catch (IncompatibleClassChangeError expected) {}

        try {
            inaccessiblePrivateInterfaceMethod();
            unreachable();
        } catch (IncompatibleClassChangeError expected) {}*/
    }

}
