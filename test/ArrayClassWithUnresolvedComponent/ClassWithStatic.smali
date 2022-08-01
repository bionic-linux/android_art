.class public LClassWithStatic;

.super Ljava/lang/Object;

.method static constructor <clinit>()V
    .registers 1
    const-string v0, "[LClassWithMissingInterface;"
    invoke-static {v0}, Ljava/lang/Class;->forName(Ljava/lang/String;)Ljava/lang/Class;
    move-result-object v0
    sput-object v0, LClassWithStatic;->field:Ljava/lang/Class;
    return-void
.end method

.field public static field:Ljava/lang/Class;
