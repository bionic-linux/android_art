.class public LClassWithStaticConst;

.super Ljava/lang/Object;

.method static constructor <clinit>()V
    .registers 1
    const-class v0, [LClassWithMissingInterface;
    sput-object v0, LClassWithStaticConst;->field:Ljava/lang/Class;
    return-void
.end method

.field public static field:Ljava/lang/Class;
