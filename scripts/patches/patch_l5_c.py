import re

with open("core/logical_memory.c", "r") as f:
    content = f.read()

ser_old = """        case LOGIC_CONDITIONAL:
            serialize_logic(f, logic->data.conditional.condition);
            serialize_logic(f, logic->data.conditional.then_expr);
            serialize_logic(f, logic->data.conditional.else_expr);
            break;
        default:
            break;
    }
}"""

ser_new = """        case LOGIC_CONDITIONAL:
            serialize_logic(f, logic->data.conditional.condition);
            serialize_logic(f, logic->data.conditional.then_expr);
            serialize_logic(f, logic->data.conditional.else_expr);
            break;
        case LOGIC_L5_SUPER:
            fwrite(&logic->data.l5_super.super_type, 1, 1, f);
            fwrite(&logic->data.l5_super.payload_hash, 4, 1, f);
            fwrite(&logic->data.l5_super.checksum, 1, 1, f);
            break;
        default:
            break;
    }
}"""
content = content.replace(ser_old, ser_new)


deser_old = """        case LOGIC_CONDITIONAL:
            logic->data.conditional.condition = deserialize_logic(f);
            logic->data.conditional.then_expr = deserialize_logic(f);
            logic->data.conditional.else_expr = deserialize_logic(f);
            break;
        default:
            break;
    }

    return logic;
}"""

deser_new = """        case LOGIC_CONDITIONAL:
            logic->data.conditional.condition = deserialize_logic(f);
            logic->data.conditional.then_expr = deserialize_logic(f);
            logic->data.conditional.else_expr = deserialize_logic(f);
            break;
        case LOGIC_L5_SUPER:
            fread(&logic->data.l5_super.super_type, 1, 1, f);
            fread(&logic->data.l5_super.payload_hash, 4, 1, f);
            fread(&logic->data.l5_super.checksum, 1, 1, f);
            break;
        default:
            break;
    }

    return logic;
}"""
content = content.replace(deser_old, deser_new)

l5_func = """
/* ========== L5 GENERATIVE ENCODING ========== */

LogicExpression* lm_logic_l5_super(uint8_t type, uint32_t payload) {
    LogicExpression *expr = calloc(1, sizeof(LogicExpression));
    if (!expr) return NULL;

    expr->type = LOGIC_L5_SUPER;
    expr->data.l5_super.super_type = type;
    expr->data.l5_super.payload_hash = payload;
    /* Basic checksum: type XOR bytes of payload */
    expr->data.l5_super.checksum = type ^ (payload & 0xFF) ^ ((payload >> 8) & 0xFF) ^ ((payload >> 16) & 0xFF) ^ ((payload >> 24) & 0xFF);
    
    expr->complexity = 1.0; /* Expensive to unpack */
    expr->materialized_size = 1024; /* Estimated size of JIT unpacked data */
    expr->creation_time = (uint64_t)time(NULL);

    return expr;
}
"""

content += "\n" + l5_func

with open("core/logical_memory.c", "w") as f:
    f.write(content)
