import re

# 1. Update kolibri_swarm_node.c
with open("kolibri_swarm_node.c", "r") as f:
    content = f.read()

old_session = """typedef struct {
    char conversation_id[256];
    char context_digits[4096]; /* Phase 1.1: Decimal Cognition Context */
    int active;
} SwarmSession;"""

new_session = """typedef struct {
    char conversation_id[256];
    char context_digits[4096]; /* Phase 1.1: Decimal Cognition Context */
    int active;
    double voting_channels[10]; /* Phase 2: Numeric Voting Channels */
} SwarmSession;"""

if old_session in content:
    content = content.replace(old_session, new_session)
    with open("kolibri_swarm_node.c", "w") as f:
        f.write(content)

# 2. Update core/kolibri_http_server.c
with open("core/kolibri_http_server.c", "r") as f:
    content = f.read()

fitness_code = """
/* === Phase 2: Formula Evolution & Numeric Voting === */
static double kolibri_phase2_fitness_eval(const uint8_t *digits, size_t length, void *data) {
    if (length == 0) return -1000.0;
    
    double score = 0.0;
    int zeros = 0;
    for (size_t i = 0; i < length; i++) {
        if (digits[i] == 0) zeros++;
        score += (double)digits[i];
    }
    
    /* Penalize inefficient/sparse formulas (too many zeros or too short) */
    if (zeros > length / 2) score -= 50.0;
    if (length < 10) score -= 20.0;
    
    return score;
}
"""

if "Phase 2: Formula Evolution & Numeric Voting" not in content:
    # Insert fitness_code before bg_learn_thread
    insert_pos = content.find("static void *bg_learn_thread(void *arg) {")
    content = content[:insert_pos] + fitness_code + "\n" + content[insert_pos:]
    
    old_evo = """        if (g_evo_ready && g_evo_trainer) {
            evo_cycle_counter++;
            if (evo_cycle_counter >= 1200) {
                kolibri_evo_step(g_evo_trainer);
                printf("  🧬 Evo step: gen=%d best=%.4f avg=%.4f\\n", g_evo_trainer->current_generation,"""

    new_evo = """        if (g_evo_ready && g_evo_trainer) {
            evo_cycle_counter++;
            if (evo_cycle_counter >= 1200) {
                /* Phase 2: Evaluate fitness and cull inefficient formulas before step */
                kolibri_evo_evaluate_fitness(g_evo_trainer, kolibri_phase2_fitness_eval, NULL);
                kolibri_evo_step(g_evo_trainer);
                printf("  🧬 Evo step: gen=%d best=%.4f avg=%.4f\\n", g_evo_trainer->current_generation,"""
    
    content = content.replace(old_evo, new_evo)
    
    with open("core/kolibri_http_server.c", "w") as f:
        f.write(content)

