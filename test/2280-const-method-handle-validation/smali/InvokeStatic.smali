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


.class LInvokeStatic;
.super Ljava/lang/Object;

.method public instanceMethod()V
    .registers 1
    return-void
.end method

.method public static constructor <clinit>()V
    .locals 1
    return-void
.end method

.method private static forVirtualMethod()Ljava/lang/invoke/MethodHandle;
    .locals 1
    const-method-handle v0, invoke-static@LInvokeStatic;->instanceMethod()V
    return-object v0
.end method

.method private static forConstructor()Ljava/lang/invoke/MethodHandle;
    .locals 1
    const-method-handle v0, invoke-static@LInvokeStatic;-><init>()V
    return-object v0
.end method

.method private static forClassInitializer()Ljava/lang/invoke/MethodHandle;
    .locals 1
    const-method-handle v0, invoke-static@LInvokeStatic;-><clinit>()V
    return-object v0
.end method

.method private static inaccessible()Ljava/lang/invoke/MethodHandle;
    .locals 1
    const-method-handle v0, invoke-static@LMain;->fail(Ljava/lang/String;)V
    return-object v0
.end method

.method public static runTests()V
    .locals 2
    :try_start_0
        invoke-static {}, LInvokeStatic;->forVirtualMethod()Ljava/lang/invoke/MethodHandle;
        const-string v1, "InvokeVirtual.forVirtualMethod should throw"
        invoke-static {v1}, LMain;->fail(Ljava/lang/String;)V
    :try_end_0
    .catch Ljava/lang/IncompatibleClassChangeError; {:try_start_0 .. :try_end_0 } :catch_end_0
    :catch_end_0

    :try_start_1
        invoke-static {}, LInvokeStatic;->forConstructor()Ljava/lang/invoke/MethodHandle;
        const-string v1, "InvokeStatic.forConstructor should throw"
        invoke-static {v1}, LMain;->fail(Ljava/lang/String;)V
    :try_end_1
    .catch Ljava/lang/IncompatibleClassChangeError; {:try_start_1 .. :try_end_1 } :catch_end_1
    :catch_end_1

    :try_start_2
        invoke-static {}, LInvokeStatic;->forClassInitializer()Ljava/lang/invoke/MethodHandle;
        const-string v1, "InvokeStatic.forClassInitializer should throw"
        invoke-static {v1}, LMain;->fail(Ljava/lang/String;)V
    :try_end_2
    .catch Ljava/lang/ClassFormatError; {:try_start_2 .. :try_end_2 } :catch_end_2
    :catch_end_2

    :try_start_3
        invoke-static {}, LInvokeStatic;->inaccessible()Ljava/lang/invoke/MethodHandle;
        const-string v1, "InvokeStatic.inaccessible should throw"
        invoke-static {v1}, LMain;->fail(Ljava/lang/String;)V
    :try_end_3
    .catch Ljava/lang/IncompatibleClassChangeError; {:try_start_3 .. :try_end_3 } :catch_end_3
    :catch_end_3

    return-void
.end method
