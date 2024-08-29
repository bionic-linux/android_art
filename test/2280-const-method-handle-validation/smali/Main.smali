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


.class LMain;
.super Ljava/lang/Object;

.field private privateField:I
.field private static staticPrivateField:I

.method private method()V
    .locals 1
    return-void
.end method

.method private static fail(Ljava/lang/String;)V
    .locals 2
    new-instance v1, Ljava/lang/AssertionError;
    invoke-direct {v1, p0}, Ljava/lang/AssertionError;-><init>(Ljava/lang/Object;)V
    throw v1
.end method

.method public static main([Ljava/lang/String;)V
    .registers 3

    invoke-static {}, LPlainGet;->runTests()V

    invoke-static {}, LPlainPut;->runTests()V

    invoke-static {}, LStaticGet;->runTests()V

    invoke-static {}, LStaticPut;->runTests()V

    invoke-static {}, LInvokeVirtual;->runTests()V

    invoke-static {}, LInvokeInterface;->runTests()V

    invoke-static {}, LInvokeStatic;->runTests()V

    invoke-static {}, LInvokeDirect;->runTests()V

    invoke-static {}, LInvokeConstructor;->runTests()V

    return-void
.end method