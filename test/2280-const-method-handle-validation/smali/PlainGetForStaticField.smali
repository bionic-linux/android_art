.class LPlainGetForStaticField;
.super Ljava/lang/Object;

.field public static staticField:Ljava/lang/Object;

.method public static test()Ljava/lang/invoke/MethodHandle;
    .locals 1
    const-method-handle v0, instance-get@LPlainGetForStaticField;->staticField:Ljava/lang/Object;
    return-object v0
.end method