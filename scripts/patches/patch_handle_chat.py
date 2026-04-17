import re

with open("core/kolibri_http_server.c", "r") as f:
    content = f.read()

# Make sure to include k_alloc.h
if "kolibri/k_alloc.h" not in content:
    content = content.replace("#include \"kolibri/decimal.h\"", "#include \"kolibri/decimal.h\"\n#include \"kolibri/k_alloc.h\"")

old_start = """    char message[65536] = {0}, conversation_id[256] = {0}, profile[64] = {0};
    if (json_get_str(body, "message", message, sizeof(message)) != 0) {
        send_json(fd, 400, "Bad Request", "{\\"error\\":\\"missing message\\"}");
        return;
    }
    json_get_str(body, "conversation_id", conversation_id, sizeof(conversation_id));
    json_get_str(body, "profile", profile, sizeof(profile));

    double t0 = now_ms();"""

new_start = """    char message[65536] = {0}, conversation_id[256] = {0}, profile[64] = {0};
    if (json_get_str(body, "message", message, sizeof(message)) != 0) {
        send_json(fd, 400, "Bad Request", "{\\"error\\":\\"missing message\\"}");
        return;
    }
    json_get_str(body, "conversation_id", conversation_id, sizeof(conversation_id));
    json_get_str(body, "profile", profile, sizeof(profile));

    /* === Phase 1.1: Decimal Cognition On-the-fly === */
    k_mem_block *decimal_input = k_alloc_decimal_block(message);

    double t0 = now_ms();"""

if old_start in content:
    content = content.replace(old_start, new_start)
    with open("core/kolibri_http_server.c", "w") as f:
        f.write(content)
else:
    print("Failed to find old_start")
