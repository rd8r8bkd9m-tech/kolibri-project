import re

with open("core/logical_memory.c", "r") as f:
    content = f.read()

unpack_old = """        case LOGIC_CONSTANT:
            result = snprintf(buffer, predicted_size + 1, "%s", logic->data.constant.value);
            break;
        case LOGIC_VARIABLE:
            result = materialize_variable(logic, buffer, predicted_size + 1);
            break;"""

unpack_new = """        case LOGIC_CONSTANT:
            result = snprintf(buffer, predicted_size + 1, "%s", logic->data.constant.value);
            break;
        case LOGIC_L5_SUPER:
            /* Phase 3: JIT unpacking of 6-byte super formula */
            result = snprintf(buffer, predicted_size + 1, "[L5_JIT_UNPACK: TYPE=%d PAYLOAD=%u]", 
                              logic->data.l5_super.super_type, logic->data.l5_super.payload_hash);
            break;
        case LOGIC_VARIABLE:
            result = materialize_variable(logic, buffer, predicted_size + 1);
            break;"""

content = content.replace(unpack_old, unpack_new)

to_str_old = """        case LOGIC_CONSTANT:
            return snprintf(output, output_size, "const(\\"%s\\")", logic->data.constant.value);"""

to_str_new = """        case LOGIC_L5_SUPER:
            return snprintf(output, output_size, "l5_super(type=%d, payload=%u)", 
                            logic->data.l5_super.super_type, logic->data.l5_super.payload_hash);
        case LOGIC_CONSTANT:
            return snprintf(output, output_size, "const(\\"%s\\")", logic->data.constant.value);"""

content = content.replace(to_str_old, to_str_new)

with open("core/logical_memory.c", "w") as f:
    f.write(content)
