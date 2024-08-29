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


.class LPlainGet;
.super Ljava/lang/Object;

.field public static staticField:Ljava/lang/Object;

.method private static forStaticField()Ljava/lang/invoke/MethodHandle;
    .locals 1
    const-method-handle v0, instance-get@LPlainGet;->staticField:Ljava/lang/Object;
    return-object v0
.end method

.method private static inaccessible()Ljava/lang/invoke/MethodHandle;
    .locals 1
    const-method-handle v0, instance-get@LMain;->privateField:I
    return-object v0
.end method

.method public static runTests()V
    .locals 2
    :try_start_0
        invoke-static {}, LPlainGet;->forStaticField()Ljava/lang/invoke/MethodHandle;
        const-string v1, "PlainGet.forStaticField should throw"
        invoke-static {v1}, LMain;->fail(Ljava/lang/String;)V
    :try_end_0
    .catch Ljava/lang/IncompatibleClassChangeError; {:try_start_0 .. :try_end_0} :catch_end_0
    :catch_end_0

    :try_start_1
        invoke-static {}, LPlainGet;->inaccessible()Ljava/lang/invoke/MethodHandle;
        const-string v1, "PlainGet.inaccessible should throw"
        invoke-static {v1}, LMain;->fail(Ljava/lang/String;)V
    :try_end_1
    .catch Ljava/lang/IncompatibleClassChangeError; {:try_start_1 .. :try_end_1} :catch_end_1
    :catch_end_1

    return-void
.end method
