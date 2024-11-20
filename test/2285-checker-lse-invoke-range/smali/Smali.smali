#
# Copyright (C) 2024 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

.class public LSmali;
.super Ljava/lang/Object;

# static fields
.field public static mF:F
.field public static mI:I

# Copy of testAllocationEliminationWithLoops from 530-checker-lse, but in smali

## CHECK-START: float Smali.$noinline$testAllocationEliminationWithLoops_Smali() load_store_elimination (before)
## CHECK: NewInstance
## CHECK: NewInstance
## CHECK: NewInstance

## CHECK-START: float Smali.$noinline$testAllocationEliminationWithLoops_Smali() load_store_elimination (after)
## CHECK-NOT: NewInstance
.method public static $noinline$testAllocationEliminationWithLoops_Smali()F
    .registers 8

    const/4 v0, 0x0
    :loop_0
    const/4 v1, 0x5
    if-ge v0, v1, :cond_0
    const/4 v2, 0x0
    :loop_1
    if-ge v2, v1, :cond_1
    const/4 v3, 0x0
    :loop_2
    if-ge v3, v1, :cond_2
    new-instance v4, Ljava/lang/Integer;
    new-instance v5, Ljava/lang/Integer;
    sget v6, LSmali;->mI:I
    invoke-direct {v5, v6}, Ljava/lang/Integer;-><init>(I)V
    invoke-virtual {v5}, Ljava/lang/Integer;->intValue()I
    move-result v5
    invoke-direct {v4, v5}, Ljava/lang/Integer;-><init>(I)V
    invoke-virtual {v4}, Ljava/lang/Integer;->intValue()I
    move-result v4
    new-instance v5, Ljava/lang/Boolean;
    const/4 v6, 0x0
    invoke-direct {v5, v6}, Ljava/lang/Boolean;-><init>(Z)V
    invoke-virtual {v5}, Ljava/lang/Boolean;->booleanValue()Z
    move-result v5
    if-eqz v5, :cond_3
    const/16 v5, 0x23f # 575
    :loop_3
    if-ltz v5, :cond_3
    sget v6, LSmali;->mF:F
    const v7, 0x4e68ee36
    sub-float/2addr v6, v7
    sput v6, LSmali;->mF:F
    add-int/lit8 v5, v5, -1
    goto :loop_3

    :cond_3
    add-int/lit8 v3, v3, 1
    goto :loop_2

    :cond_2
    add-int/lit8 v2, v2, 1
    goto :loop_1

    :cond_1
    add-int/lit8 v0, v0, 1
    goto :loop_0

    :cond_0
    const/high16 v0, 0x3f800000 # 1.0f
    return v0

.end method

# Copy of testAllocationEliminationWithLoops from 530-checker-lse, but in smali
# and using invoke-virtual/range instead of invoke-virtual

## CHECK-START: float Smali.$noinline$testAllocationEliminationWithLoops_Smali_Range() load_store_elimination (before)
## CHECK: NewInstance
## CHECK: NewInstance
## CHECK: NewInstance

## CHECK-START: float Smali.$noinline$testAllocationEliminationWithLoops_Smali_Range() load_store_elimination (after)
## CHECK-NOT: NewInstance
.method public static $noinline$testAllocationEliminationWithLoops_Smali_Range()F
    .registers 8

    const/4 v0, 0x0
    :loop_0
    const/4 v1, 0x5
    if-ge v0, v1, :cond_0
    const/4 v2, 0x0
    :loop_1
    if-ge v2, v1, :cond_1
    const/4 v3, 0x0
    :loop_2
    if-ge v3, v1, :cond_2
    new-instance v4, Ljava/lang/Integer;
    new-instance v5, Ljava/lang/Integer;
    sget v6, LSmali;->mI:I
    invoke-direct {v5, v6}, Ljava/lang/Integer;-><init>(I)V
    invoke-virtual/range {v5}, Ljava/lang/Integer;->intValue()I
    move-result v5
    invoke-direct {v4, v5}, Ljava/lang/Integer;-><init>(I)V
    invoke-virtual/range {v4}, Ljava/lang/Integer;->intValue()I
    move-result v4
    new-instance v5, Ljava/lang/Boolean;
    const/4 v6, 0x0
    invoke-direct {v5, v6}, Ljava/lang/Boolean;-><init>(Z)V
    invoke-virtual/range {v5}, Ljava/lang/Boolean;->booleanValue()Z
    move-result v5
    if-eqz v5, :cond_3
    const/16 v5, 0x23f # 575
    :loop_3
    if-ltz v5, :cond_3
    sget v6, LSmali;->mF:F
    const v7, 0x4e68ee36
    sub-float/2addr v6, v7
    sput v6, LSmali;->mF:F
    add-int/lit8 v5, v5, -1
    goto :loop_3

    :cond_3
    add-int/lit8 v3, v3, 1
    goto :loop_2

    :cond_2
    add-int/lit8 v2, v2, 1
    goto :loop_1

    :cond_1
    add-int/lit8 v0, v0, 1
    goto :loop_0

    :cond_0
    const/high16 v0, 0x3f800000 # 1.0f
    return v0

.end method