# /*
#  * Copyright 2022 The Android Open Source Project
#  *
#  * Licensed under the Apache License, Version 2.0 (the "License");
#  * you may not use this file except in compliance with the License.
#  * You may obtain a copy of the License at
#  *
#  *      http://www.apache.org/licenses/LICENSE-2.0
#  *
#  * Unless required by applicable law or agreed to in writing, software
#  * distributed under the License is distributed on an "AS IS" BASIS,
#  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  * See the License for the specific language governing permissions and
#  * limitations under the License.
#  */

.class public LImpl;
.super Ljava/lang/Object;
.implements LSubItf;

.method constructor <init>()V
    .locals 1
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V
    return-void
.end method

.method public static test(LSubItf;)Ljava/lang/String;
    .locals 1

    # Because the imt index was overwritten to 0, this call ended up
    # in the conflict trampoline which wrongly updated the 0th entry
    # of the imt table. This lead to this call always calling the
    # conflict trampoline.
    invoke-interface {p0}, LSubItf;->foo()Ljava/lang/String;
    move-result-object v0
    return-object v0
.end method

.method public foo()Ljava/lang/String;
    .locals 2

    const-string v1, "Impl"
    return-object v1
.end method
