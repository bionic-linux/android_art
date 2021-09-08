# Copyright (C) 2021 The Android Open Source Project
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

.class public LRemoveSuspendCheck;
.super Ljava/lang/Object;
.field public str:Ljava/lang/String;

.method public constructor <init>()V
.registers 1
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V
    return-void
.end method

## CHECK-START: java.lang.String RemoveSuspendCheck.leaf(RemoveSuspendCheck) register (after)
## CHECK-NOT: SuspendCheck
.method public leaf(LRemoveSuspendCheck;)Ljava/lang/String;
.registers 2
    iget-object v0, p0, LRemoveSuspendCheck;->str:Ljava/lang/String;
    return-object v0
.end method

.method public static removeSuspendCheck()V
    .registers 1
    new-instance v0, LRemoveSuspendCheck;
    invoke-direct {v0}, LRemoveSuspendCheck;-><init>()V
    invoke-virtual {v0, v0}, LRemoveSuspendCheck;->leaf(LRemoveSuspendCheck;)Ljava/lang/String;
    return-void
.end method