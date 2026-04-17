import re

with open("CMakeLists.txt", "r") as f:
    content = f.read()

if "core/k_alloc.c" not in content:
    content = content.replace("core/decimal.c", "core/decimal.c\n    core/k_alloc.c")
    with open("CMakeLists.txt", "w") as f:
        f.write(content)

