import re

with open("kolibri_swarm_node.c", "r") as f:
    content = f.read()

role_code = """
/* Phase 4: Swarm Roles & Quorum */
typedef enum {
    SWARM_ROLE_LEARNER,
    SWARM_ROLE_ANCHOR,
    SWARM_ROLE_VALIDATOR
} SwarmRole;

static SwarmRole g_node_role = SWARM_ROLE_LEARNER;
static int g_quorum_required = 6; /* 6/10 quorum requirement for validation */
"""

if "Phase 4: Swarm Roles" not in content:
    insert_pos = content.find("static int server_fd = -1;")
    if insert_pos == -1:
        insert_pos = content.find("int main")
    
    content = content[:insert_pos] + role_code + "\n" + content[insert_pos:]

with open("kolibri_swarm_node.c", "w") as f:
    f.write(content)
