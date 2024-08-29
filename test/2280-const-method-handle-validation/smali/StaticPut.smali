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


.class LStaticPut;
.super Ljava/lang/Object;

.field public static final staticFinalField:Ljava/lang/Object;
.field public instanceField:I
.field public final finalInstanceField:I

.method public static constructor <clinit>()V
    .registers 4
    const v0, 0x0
    sput-object v0, LStaticPut;->staticFinalField:Ljava/lang/Object;
    return-void
.end method

.method public constructor <init>()V
    .registers 2
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V
    const/4 v0, 0x1
    iput v0, p0, LStaticPut;->finalField:I
    return-void
.end method

.method private static forStaticFinalField()Ljava/lang/invoke/MethodHandle;
    .locals 1
    const-method-handle v0, static-put@LStaticPut;->staticFinalField:Ljava/lang/Object;
    return-object v0
.end method

.method private static forInstanceField()Ljava/lang/invoke/MethodHandle;
    .locals 1
    const-method-handle v0, static-put@LStaticPut;->instanceField:I
    return-object v0
.end method

.method private static forFinalInstanceField()Ljava/lang/invoke/MethodHandle;
    .locals 1
    const-method-handle v0, static-put@LStaticPut;->finalInstanceField:I
    return-object v0
.end method

.method public static runTests()V
    .locals 2
    :try_start_0
        invoke-static {}, LStaticPut;->forStaticFinalField()Ljava/lang/invoke/MethodHandle;
        const-string v1, "StaticPut.forStaticFinalField should throw"
        invoke-static {v1}, LMain;->fail(Ljava/lang/String;)V
    :try_end_0
    .catch Ljava/lang/IncompatibleClassChangeError; {:try_start_0 .. :try_end_0 } :catch_end_0
    :catch_end_0

    :try_start_1
        invoke-static {}, LStaticPut;->forInstanceField()Ljava/lang/invoke/MethodHandle;
        const-string v1, "StaticPut.forInstanceField should throw"
        invoke-static {v1}, LMain;->fail(Ljava/lang/String;)V
    :try_end_1
    .catch Ljava/lang/IncompatibleClassChangeError; {:try_start_1 .. :try_end_1 } :catch_end_1
    :catch_end_1

    :try_start_2
        invoke-static {}, LStaticPut;->forFinalInstanceField()Ljava/lang/invoke/MethodHandle;
        const-string v1, "StaticPut.forFinalInstanceField should throw"
        invoke-static {v1}, LMain;->fail(Ljava/lang/String;)V
    :try_end_2
    .catch Ljava/lang/IncompatibleClassChangeError; {:try_start_2 .. :try_end_2 } :catch_end_2
    :catch_end_2

    return-void
.end method
