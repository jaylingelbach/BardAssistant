Import("env")

env.Tool("compilation_db")
db = env.CompilationDatabase("$PROJECT_DIR/compile_commands.json")
env.Alias("compiledb", db)
env.Default(db)
