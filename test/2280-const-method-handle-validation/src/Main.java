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

    public static void main(String[] args) throws Throwable {
        StringBuilder msg = new StringBuilder();

        // In every try-catch block below The RI throws an exception: either CFE or ICCE.
        // Unless anything said, ART does not throw.
        try {
            // const-method-type created there breaks R8: https://paste.googleplex.com/5366765195624448?raw
            // RI throws CFE.
            new FieldAsVirtual();
            msg.append("FieldAsVirtual was created successfully\n");
        } catch (Throwable t) {
            msg.append("FieldAsVirtual threw " + t.getClass()).append('\n');
        }

        try {
            new InterfaceAsVirtual();
            msg.append("InterfaceAsVirtual was created successfully\n");
        } catch (Throwable t) {
            msg.append("InterfaceAsVirtual threw " + t.getClass()).append('\n');
        }

        try {
            new VirtualAsInterface();
            msg.append("VirtualAsInterface was created successfully\n");
        } catch (Throwable t) {
            msg.append("VirtualAsInterface threw " + t.getClass()).append('\n');
        }

        try {
            new VirtualAsStatic();
            msg.append("VirtualAsStatic was created successfully\n");
        } catch (Throwable t) {
            msg.append("VirtualAsStatic threw " + t.getClass()).append('\n');
        }

        try {
            // E dex2oatd64: oat_writer.cc:3286 Failed to verify X: Failure to verify dex file 'X': Invalid type descriptor: '()I'
            // The RI throws ClassFormatError.
            // new VirtualAsStaticGet();
            msg.append("VirtualAsStaticGet was created successfully\n");
        } catch (Throwable t) {
            msg.append("VirtualAsStaticGet threw " + t.getClass()).append('\n');
        }

        System.out.println(msg.toString());
    }
}