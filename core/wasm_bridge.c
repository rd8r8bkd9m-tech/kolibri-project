#define _GNU_SOURCE
#include <emscripten.h>
#include "kolibri/formula.h"
#include "kolibri/script.h"
#include "kolibri/inference.h"
#include "kolibri/knowledge.h"
#include "kolibri/fractal_memory.h"
#include "kolibri/reasoning.h"
#include "kolibri/reasoning_engine.h"
#include "kolibri/knowledge_base.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

static KolibriInferenceContext *g_inf_ctx = NULL;
static int g_bridge_ready = 0;

EMSCRIPTEN_KEEPALIVE
int kolibri_bridge_init(void) {
    if (g_inf_ctx) {
        kolibri_inference_destroy(g_inf_ctx);
    }
    g_inf_ctx = kolibri_inference_create();
    if (!g_inf_ctx) return -1;
    kolibri_inference_set_strategy(g_inf_ctx, KOLIBRI_INF_HYBRID);
    kolibri_reasoning_init();
    kolibri_kb_init(NULL);
    g_bridge_ready = 1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int kolibri_bridge_reset(void) { return kolibri_bridge_init(); }

EMSCRIPTEN_KEEPALIVE
int kolibri_bridge_reasoning_query(const char *query, char *out_json, size_t capacity) {
    return kolibri_reasoning_query_json(query, out_json, capacity);
}

EMSCRIPTEN_KEEPALIVE
int kolibri_bridge_kb_search(const char *query, char *out_json, size_t capacity) {
    return kolibri_kb_search(query, out_json, capacity);
}

EMSCRIPTEN_KEEPALIVE
int kolibri_bridge_kb_add_fact(const char *fact, double confidence, const char *source) {
    return kolibri_kb_add_fact(fact, confidence, source);
}

EMSCRIPTEN_KEEPALIVE
int kolibri_bridge_kb_add_rule(const char *premise, const char *conclusion, int op, double strength, const char *domain) {
    return kolibri_re_add_rule(NULL, premise, conclusion, (KolibriLogicalOp)op, strength, domain);
}

EMSCRIPTEN_KEEPALIVE
int kolibri_bridge_health(char *out, size_t cap) {
    snprintf(out, cap, "{\"status\":\"ok\",\"engine\":\"Kolibri AGI v66\",\"runtime\":\"WASM\"}");
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int kolibri_bridge_query_json(const char *q, char *b, size_t c) { if(b && c>0) b[0]='\0'; return 0; }
EMSCRIPTEN_KEEPALIVE
int kolibri_bridge_send_message(const char *i, const char *m, char *o, size_t c) { if(o && c>0) o[0]='\0'; return 0; }
EMSCRIPTEN_KEEPALIVE
const char *kolibri_bridge_get_progress_state(void) { return "idle"; }
EMSCRIPTEN_KEEPALIVE
double kolibri_bridge_get_progress_value(void) { return 1.0; }
EMSCRIPTEN_KEEPALIVE
const char *kolibri_bridge_get_progress_detail(void) { return "ready"; }
EMSCRIPTEN_KEEPALIVE
const char *kolibri_bridge_get_thinking(void) { return "WASM Core Active"; }
EMSCRIPTEN_KEEPALIVE
void kolibri_bridge_cancel_query(void) {}
EMSCRIPTEN_KEEPALIVE
int kolibri_bridge_is_cancelled(void) { return 0; }

EMSCRIPTEN_KEEPALIVE
int kolibri_bridge_execute(const char *p, char *b, size_t c) { if(b && c>0) b[0]='\0'; return 0; }
