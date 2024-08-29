#
# Copyright (C) 2024 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


.class LInvokeConstructor;
.super Ljava/lang/Object;

.method public static constructor <clinit>()V
    .locals 1
    return-void
.end method

.method public virtualMethod()V
    .locals 1
    return-void
.end method

.method private static forStaticMethod()Ljava/lang/invoke/MethodHandle;
    .locals 1
    const-method-handle v0, invoke-constructor@LInvokeConstructor;->forStaticMethod()Ljava/lang/invoke/MethodHandle;
    return-object v0
.end method

.method private static forInterfaceMethod()Ljava/lang/invoke/MethodHandle;
    .locals 1
    const-method-handle v0, invoke-constructor@LInvokeInterface;->interfaceMethod()V
    return-object v0
.end method

.method private static forClassInitializer()Ljava/lang/invoke/MethodHandle;
    .locals 1
    const-method-handle v0, invoke-constructor@LInvokeConstructor;-><clinit>()V
    return-object v0
.end method

.method private static inaccessible()Ljava/lang/invoke/MethodHandle;
    .locals 1
    const-method-handle v0, invoke-constructor@LInvokeDirect;-><init>()V
    return-object v0
.end method

.method public static runTests()V
    .locals 4
    :try_start_0
        invoke-static {}, LInvokeConstructor;->forVirtualMethod()Ljava/lang/invoke/MethodHandle;
        const-string v1, "InvokeConstructor.forVirtualMethod should throw"
        invoke-static {v1}, LMain;->fail(Ljava/lang/String;)V
    :try_end_0
    .catch Ljava/lang/IncompatibleClassChangeError; {:try_start_0 .. :try_end_0 } :catch_end_0
    :catch_end_0

    :try_start_1
        invoke-static {}, LInvokeConstructor;->forStaticMethod()Ljava/lang/invoke/MethodHandle;
        const-string v1, "InvokeConstructor.forStaticMethod should throw"
        invoke-static {v1}, LMain;->fail(Ljava/lang/String;)V
    :try_end_1
    .catch Ljava/lang/IncompatibleClassChangeError; {:try_start_1 .. :try_end_1 } :catch_end_1
    :catch_end_1

    :try_start_3
        invoke-static {}, LInvokeConstructor;->forClassInitializer()Ljava/lang/invoke/MethodHandle;
        const-string v1, "InvokeConstructor.forClassInitializer should throw"
        invoke-static {v1}, LMain;->fail(Ljava/lang/String;)V
    :try_end_3
    .catch Ljava/lang/IncompatibleClassChangeError; {:try_start_3 .. :try_end_3 } :catch_end_3
    :catch_end_3

    :try_start_4
        invoke-static {}, LInvokeConstructor;->inaccessible()Ljava/lang/invoke/MethodHandle;
        const-string v1, "InvokeConstructor.inaccessible should throw"
        invoke-static {v1}, LMain;->fail(Ljava/lang/String;)V
    :try_end_4
    .catch Ljava/lang/IncompatibleClassChangeError; {:try_start_4 .. :try_end_4 } :catch_end_4
    :catch_end_4

    return-void
.end method