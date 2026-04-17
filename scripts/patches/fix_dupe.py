import re
with open("core/logical_memory.c", "r") as f:
    content = f.read()

# remove duplicate definitions of lm_logic_l5_super
blocks = content.split("/* ========== L5 GENERATIVE ENCODING ========== */")
content = blocks[0] + "/* ========== L5 GENERATIVE ENCODING ========== */" + blocks[1]

with open("core/logical_memory.c", "w") as f:
    f.write(content)
