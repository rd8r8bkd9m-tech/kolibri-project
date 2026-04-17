import re

with open("web/src/lib/kolibriBridge.ts", "r") as f:
    content = f.read()

old_env = """      env: {
        abort: () => {
          throw new Error("Kolibri WASM abort");
        },
      },"""

new_env = """      env: {
        abort: () => {
          throw new Error("Kolibri WASM abort");
        },
        emscripten_notify_memory_growth: (index: number) => {
          // memory grew
        },
      },"""

if old_env in content:
    content = content.replace(old_env, new_env)
    with open("web/src/lib/kolibriBridge.ts", "w") as f:
        f.write(content)
else:
    print("Could not find old_env in kolibriBridge.ts")
