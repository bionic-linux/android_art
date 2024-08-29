.class LMain;
.super Ljava/lang/Object;

.method private static fail(Ljava/lang/String;)V
    .locals 2
    new-instance v1, Ljava/lang/AssertionError;
    invoke-direct {v1, p0}, Ljava/lang/AssertionError;-><init>(Ljava/lang/Object;)V
    throw v1
.end method

.method public static main([Ljava/lang/String;)V
    .locals 3
    :try_start_1
        invoke-static {}, LPlainGetForStaticField;->test()Ljava/lang/invoke/MethodHandle;
        const-string v1, "PlainGetForStaticField.test should throw"
        invoke-static {v1}, LMain;->fail(Ljava/lang/String;)V
    :try_end_1
    .catch Ljava/lang/IncompatibleClassChangeError; {:try_start_1 .. :try_end_1} :catch_end_1
    :catch_end_1
    return-void
.end method