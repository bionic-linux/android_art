def build(ctx):
  ctx.bash("./generate-sources")
  # To allow private interface methods.
  ctx.default_build(api_level="const-method-type",
                    javac_source_arg="17",
                    javac_target_arg="17")
