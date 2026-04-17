import re

with open("core/kolibri/logical_memory.h", "r") as f:
    content = f.read()

old_enum = """    LOGIC_CONDITIONAL,   /* Условие: if(cond, then, else) */
    LOGIC_COMPOSITION,   /* Композиция: compose(f1, f2, ...) */
    LOGIC_RELATION       /* Отношение: relates(A, B, type) */
} LogicType;"""

new_enum = """    LOGIC_CONDITIONAL,   /* Условие: if(cond, then, else) */
    LOGIC_COMPOSITION,   /* Композиция: compose(f1, f2, ...) */
    LOGIC_RELATION,      /* Отношение: relates(A, B, type) */
    LOGIC_L5_SUPER       /* L5 Generative: 6-byte super formula */
} LogicType;"""

content = content.replace(old_enum, new_enum)

old_union = """        /* LOGIC_RELATION */
        struct {
            struct LogicExpression *left;
            struct LogicExpression *right;
            char relation_type[16];  /* "derives_from", "part_of", "equivalent" */
        } relation;
    } data;"""

new_union = """        /* LOGIC_RELATION */
        struct {
            struct LogicExpression *left;
            struct LogicExpression *right;
            char relation_type[16];  /* "derives_from", "part_of", "equivalent" */
        } relation;
        
        /* LOGIC_L5_SUPER */
        struct {
            uint8_t super_type;
            uint32_t payload_hash;
            uint8_t checksum;
        } l5_super;
    } data;"""

content = content.replace(old_union, new_union)

with open("core/kolibri/logical_memory.h", "w") as f:
    f.write(content)
