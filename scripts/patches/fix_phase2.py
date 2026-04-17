import re

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

content = content.replace(fitness_code, "")

insert_pos = content.find("static void *bg_learn_loop(void *arg) {")
content = content[:insert_pos] + fitness_code + "\n" + content[insert_pos:]

with open("core/kolibri_http_server.c", "w") as f:
    f.write(content)
