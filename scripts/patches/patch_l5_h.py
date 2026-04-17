import re

with open("core/kolibri/logical_memory.h", "r") as f:
    content = f.read()

func_dec = """/* Создать логическое выражение: if(condition, then, else) */
LogicExpression* lm_logic_conditional(
    LogicExpression *condition,
    LogicExpression *then_expr,
    LogicExpression *else_expr
);"""

func_dec_new = """/* Создать логическое выражение: if(condition, then, else) */
LogicExpression* lm_logic_conditional(
    LogicExpression *condition,
    LogicExpression *then_expr,
    LogicExpression *else_expr
);

/* Phase 3: L5 Generative Encoding */
LogicExpression* lm_logic_l5_super(uint8_t type, uint32_t payload);"""

content = content.replace(func_dec, func_dec_new)

with open("core/kolibri/logical_memory.h", "w") as f:
    f.write(content)
