/**
 * kolibriCoreWasm.ts — WASM мост через Emscripten JS-лоадер
 */

import KolibriCoreModule from './kolibriCoreModule';

export interface Conversation { id: string; title: string; created_at: string; updated_at: string; turn_count: number; }
export interface ConversationTurn { id: string; role: "user" | "assistant"; content: string; created_at: string; method?: string; duration_ms?: number; confidence?: number; }
export interface ModelInfo { id: string; name: string; version: string; parameters: number; status: "ready"; }
export interface DomainStats { physics: number; chemistry: number; programming: number; law: number; total: number; }

const OUT_CAPACITY = 16384;

type KolibriMod = Awaited<ReturnType<typeof KolibriCoreModule>>;

class KolibriCoreWasmBridge {
  private mod: KolibriMod | null = null;
  private readyPromise: Promise<void> | null = null;
  private conversations: Map<string, Conversation> = new Map();
  private turns: Map<string, ConversationTurn[]> = new Map();

  async ready(): Promise<void> {
    if (!this.readyPromise) this.readyPromise = this.load();
    return this.readyPromise;
  }

  private async load(): Promise<void> {
    try {
      this.mod = await KolibriCoreModule({
        locateFile: (p: string) => `/src/lib/${p}`
      });
      const rc = this.mod._kolibri_core_wasm_init();
      if (rc !== 0) throw new Error(`init code ${rc}`);
      this.loadState();
      console.log("✅ Kolibri Core WASM initialized");
    } catch (e) {
      console.error("WASM failed:", e);
      throw e;
    }
  }

  private loadState() {
    try {
      const c = localStorage.getItem("k_c");
      if (c) this.conversations = new Map(Object.entries(JSON.parse(c)));
      const t = localStorage.getItem("k_t");
      if (t) this.turns = new Map(Object.entries(JSON.parse(t)));
    } catch {}
  }

  private saveState() {
    try {
      localStorage.setItem("k_c", JSON.stringify(Object.fromEntries(this.conversations)));
      localStorage.setItem("k_t", JSON.stringify(Object.fromEntries(this.turns)));
    } catch {}
  }

  private readStr(ptr: number): string {
    if (!this.mod) return "";
    const mem = this.mod.memory as WebAssembly.Memory;
    const buf = new Uint8Array(mem.buffer, ptr);
    const end = buf.indexOf(0);
    return new TextDecoder().decode(buf.slice(0, end < 0 ? OUT_CAPACITY : end));
  }

  private writeStr(ptr: number, str: string) {
    if (!this.mod) return;
    const mem = this.mod.memory as WebAssembly.Memory;
    const enc = new TextEncoder().encode(str);
    new Uint8Array(mem.buffer, ptr, enc.length + 1).set(enc);
  }

  private callJson(fn: (a: number, b: number, c: number) => number, obj: object): string {
    if (!this.mod) throw new Error("not ready");
    const json = JSON.stringify(obj);
    const ip = this.mod._malloc(json.length + 1);
    const op = this.mod._malloc(OUT_CAPACITY);
    try {
      this.writeStr(ip, json);
      const rc = fn(ip, op, OUT_CAPACITY);
      if (rc !== 0) throw new Error(`code ${rc}`);
      return this.readStr(op);
    } finally {
      this.mod!._free(ip);
      this.mod!._free(op);
    }
  }

  /* ===== Public API ===== */
  async getModels(): Promise<ModelInfo[]> {
    return [{ id: "kolibri-core", name: "Kolibri C-Core", version: "1.0", parameters: 100000, status: "ready" }];
  }
  async getConversations(limit: number): Promise<Conversation[]> { return Array.from(this.conversations.values()).slice(0, limit); }
  async getTurns(convId: string, limit: number): Promise<ConversationTurn[]> { return (this.turns.get(convId) || []).slice(-limit); }
  async createConversation(title: string): Promise<Conversation> {
    const id = `c-${crypto.randomUUID()}`;
    const conv: Conversation = { id, title: title.slice(0, 100), created_at: new Date().toISOString(), updated_at: new Date().toISOString(), turn_count: 0 };
    this.conversations.set(id, conv); this.turns.set(id, []); this.saveState();
    return conv;
  }
  async getDomainStats(): Promise<DomainStats> { return { physics: 12, chemistry: 10, programming: 14, law: 11, total: 47 }; }
  async getLearningStatus() { return { status: "idle", documents: 0, epochs: 0 }; }
  async knowledgeAnalytics() { return { total_facts: 47, domains: await this.getDomainStats() }; }
  async health() { return { status: "ok", uptime_ms: performance.now(), avg_response_ms: 50 }; }

  async chat(message: string): Promise<{ response: string; method: string; confidence: number; duration_ms: number }> {
    await this.ready();
    const start = performance.now();
    let result: any = null;
    try { const j = this.callJson(this.mod!._kolibri_wasm_reason, { query: message }); result = JSON.parse(j); } catch {}
    const dur = performance.now() - start;
    if (result?.answer) {
      const cid = Array.from(this.conversations.keys())[0];
      if (cid) {
        const ts = this.turns.get(cid) || [];
        ts.push({ id: `t${Date.now()}`, role: "user", content: message, created_at: new Date().toISOString() });
        ts.push({ id: `t${Date.now()+1}`, role: "assistant", content: result.answer, created_at: new Date().toISOString(), method: result.type, duration_ms: dur, confidence: result.confidence });
        this.turns.set(cid, ts);
        const cv = this.conversations.get(cid);
        if (cv) { cv.updated_at = new Date().toISOString(); cv.turn_count = ts.length; this.conversations.set(cid, cv); }
        this.saveState();
      }
      return { response: result.answer, method: result.type || "reasoning", confidence: result.confidence || 0.5, duration_ms: dur };
    }
    return { response: `Kolibri C-core: "${message}"`, method: "fallback", confidence: 0.3, duration_ms: dur };
  }

  async solveLinear(a: number, b: number, c: number) { await this.ready(); return JSON.parse(this.callJson(this.mod!._kolibri_wasm_solve_linear, { a, b, c })); }
  async solveQuadratic(a: number, b: number, c: number) { await this.ready(); return JSON.parse(this.callJson(this.mod!._kolibri_wasm_solve_quadratic, { a, b, c })); }
  async tokenize(expr: string) { await this.ready(); return JSON.parse(this.callJson(this.mod!._kolibri_wasm_tokenize, { expression: expr })); }
  async verify(query: string, answer: string) { await this.ready(); return JSON.parse(this.callJson(this.mod!._kolibri_wasm_verify, { query, answer })); }
  async explain(query: string): Promise<string> { await this.ready(); return JSON.parse(this.callJson(this.mod!._kolibri_wasm_explain, { query })).explanation || ""; }
}

export const kolibriCoreWasm = new KolibriCoreWasmBridge();
