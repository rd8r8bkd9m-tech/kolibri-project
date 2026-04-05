import KolibriCoreModule from './kolibriCoreModule';

export interface KolibriContextTurn {
  prompt: string;
  answer: string;
}

export interface KolibriQueryResult {
  response: string;
  confidence: number;
  method: string;
  sources: number;
  duration_ms: number;
  thinking: string;
}

export interface KolibriProgressInfo {
  state: string;
  value: number;
  detail: string;
}

export interface KolibriHealthInfo {
  status: string;
  uptime_ms: number;
  queries_processed: number;
  avg_response_ms: number;
  conversations_active: number;
}

export type StreamCallback = (token: string) => void;

import { looksWeak, sanitizeContextText } from "@/lib/answerSanitizer";

interface KolibriWasmExports {
  memory: WebAssembly.Memory;
  _malloc(size: number): number;
  _free(ptr: number): void;
  _kolibri_bridge_init(): number;
  _kolibri_bridge_reset(): number;
  _kolibri_bridge_execute(programPtr: number, outputPtr: number, outputCapacity: number): number;
  _kolibri_bridge_query(queryPtr: number, outputPtr: number, outputCapacity: number): number;
  _kolibri_bridge_query_json(queryPtr: number, outputPtr: number, outputCapacity: number): number;
  _kolibri_bridge_create_conversation(idPtr: number): number;
  _kolibri_bridge_delete_conversation(idPtr: number): number;
  _kolibri_bridge_send_message(convIdPtr: number, msgPtr: number, outputPtr: number, outputCapacity: number): number;
  _kolibri_bridge_get_progress_state(): number;
  _kolibri_bridge_get_progress_value(): number;
  _kolibri_bridge_get_progress_detail(): number;
  _kolibri_bridge_get_thinking(): number;
  _kolibri_bridge_cancel_query(): void;
  _kolibri_bridge_batch_query(queriesPtrPtr: number, queryCount: number, outputPtr: number, outputCapacity: number): number;
  _kolibri_bridge_get_memory_usage(): number;
  _kolibri_bridge_health(outputPtr: number, outputCapacity: number): number;
  _kolibri_bridge_set_stream_callback(callbackPtr: number, userDataPtr: number): void;
}

const OUTPUT_CAPACITY = 8192;
const RESPONSE_MODE = (import.meta.env.VITE_KOLIBRI_RESPONSE_MODE ?? "script").toLowerCase();
const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder("utf-8");

function escapeScriptString(value: string): string {
  return value
    .replace(/\\/g, "\\\\")
    .replace(/"/g, '\\"')
    .replace(/\r?\n/g, "\\n");
}

function resolveMode(persona: "assistant" | "romantic" | "storyteller"): string {
  if (persona === "romantic") return "emoji";
  if (persona === "storyteller") return "journal";
  return "neutral";
}

function buildProgram(
  prompt: string,
  persona: "assistant" | "romantic" | "storyteller",
  context: KolibriContextTurn[],
): string {
  const trimmed = prompt.trim();
  if (!trimmed) {
    return 'начало:\n    показать "Пустой запрос"\nконец.\n';
  }

  const lines = ["начало:", `    режим "${resolveMode(persona)}"`];
  for (const turn of context) {
    const q = sanitizeContextText(turn.prompt);
    const a = sanitizeContextText(turn.answer);
    if (!q || !a || looksWeak(q, a)) continue;
    lines.push(`    обучить связь "${escapeScriptString(q)}" -> "${escapeScriptString(a)}"`);
  }
  lines.push('    создать формулу ответ из "ассоциация"');
  lines.push("    вызвать эволюцию");
  lines.push(`    оценить ответ на задаче "${escapeScriptString(trimmed)}"`);
  lines.push("    показать итог");
  lines.push("конец.");
  return `${lines.join("\n")}\n`;
}

function resolveFunction(exports: WebAssembly.Exports, candidates: readonly string[]): (...args: number[]) => number {
  const lookup = exports as Record<string, unknown>;
  for (const name of candidates) {
    const candidate = lookup[name];
    if (typeof candidate === "function") return candidate as (...args: number[]) => number;
  }
  throw new Error(`WASM-модуль не экспортирует ${candidates.join(", ")}`);
}

function createExports(rawExports: WebAssembly.Exports): KolibriWasmExports {
  const memory = rawExports.memory as WebAssembly.Memory;
  if (!(memory instanceof WebAssembly.Memory)) throw new Error("WASM-модуль не экспортирует память");
  const resolve = (candidates: string[]) => resolveFunction(rawExports, candidates) as (...args: number[]) => number;
  return {
    memory,
    _malloc: resolve(["_malloc", "malloc"]) as (size: number) => number,
    _free: resolve(["_free", "free"]) as (ptr: number) => void,
    _kolibri_bridge_init: resolve(["_kolibri_bridge_init", "kolibri_bridge_init"]) as () => number,
    _kolibri_bridge_reset: resolve(["_kolibri_bridge_reset", "kolibri_bridge_reset"]) as () => number,
    _kolibri_bridge_execute: resolve(["_kolibri_bridge_execute", "kolibri_bridge_execute"]) as (a: number, b: number, c: number) => number,
    _kolibri_bridge_query: resolve(["_kolibri_bridge_query", "kolibri_bridge_query"]) as (a: number, b: number, c: number) => number,
    _kolibri_bridge_query_json: resolve(["_kolibri_bridge_query_json", "kolibri_bridge_query_json"]) as (a: number, b: number, c: number) => number,
    _kolibri_bridge_create_conversation: resolve(["_kolibri_bridge_create_conversation", "kolibri_bridge_create_conversation"]) as (a: number) => number,
    _kolibri_bridge_delete_conversation: resolve(["_kolibri_bridge_delete_conversation", "kolibri_bridge_delete_conversation"]) as (a: number) => number,
    _kolibri_bridge_send_message: resolve(["_kolibri_bridge_send_message", "kolibri_bridge_send_message"]) as (a: number, b: number, c: number, d: number) => number,
    _kolibri_bridge_get_progress_state: resolve(["_kolibri_bridge_get_progress_state", "kolibri_bridge_get_progress_state"]) as () => number,
    _kolibri_bridge_get_progress_value: resolve(["_kolibri_bridge_get_progress_value", "kolibri_bridge_get_progress_value"]) as () => number,
    _kolibri_bridge_get_progress_detail: resolve(["_kolibri_bridge_get_progress_detail", "kolibri_bridge_get_progress_detail"]) as () => number,
    _kolibri_bridge_get_thinking: resolve(["_kolibri_bridge_get_thinking", "kolibri_bridge_get_thinking"]) as () => number,
    _kolibri_bridge_cancel_query: resolve(["_kolibri_bridge_cancel_query", "kolibri_bridge_cancel_query"]) as () => void,
    _kolibri_bridge_batch_query: resolve(["_kolibri_bridge_batch_query", "kolibri_bridge_batch_query"]) as (a: number, b: number, c: number, d: number) => number,
    _kolibri_bridge_get_memory_usage: resolve(["_kolibri_bridge_get_memory_usage", "kolibri_bridge_get_memory_usage"]) as () => number,
    _kolibri_bridge_health: resolve(["_kolibri_bridge_health", "kolibri_bridge_health"]) as (a: number, b: number) => number,
    _kolibri_bridge_set_stream_callback: resolve(["_kolibri_bridge_set_stream_callback", "kolibri_bridge_set_stream_callback"]) as (a: number, b: number) => void,
  };
}

class KolibriWasmBridge {
  private exports: KolibriWasmExports | null = null;
  private initPromise: Promise<void> | null = null;
  private streamCallback: StreamCallback | null = null;

  /* #26. Web Worker support */
  private worker: Worker | null = null;
  private workerReady = false;

  /* #27. WASM memory pooling */
  private memoryPool: number[] = [];
  private readonly POOL_MAX_SIZE = 65536; /* 64KB */

  async ready(): Promise<void> {
    if (RESPONSE_MODE === "llm") {
      throw new Error("WASM mode disabled by VITE_KOLIBRI_RESPONSE_MODE=llm");
    }
    if (!this.initPromise) {
      this.initPromise = this.initialise();
    }
    return this.initPromise;
  }

  /* #26. Initialize Web Worker for inference */
  async initWorker(): Promise<void> {
    if (this.worker) return;
    try {
      this.worker = new Worker(new URL("./kolibriWorker.ts", import.meta.url), {
        type: "module",
      });
      this.worker.onmessage = (e) => {
        if (e.data.type === "ready") {
          this.workerReady = true;
        } else if (e.data.type === "result" && this.streamCallback) {
          this.streamCallback(e.data.token);
        }
      };
    } catch {
      /* Workers not available */
    }
  }

  /* #27. WASM memory pooling */
  private allocateFromPool(size: number): number {
    /* Try to reuse from pool */
    for (let i = 0; i < this.memoryPool.length; i++) {
      const ptr = this.memoryPool[i];
      if (ptr > 0 && this.exports) {
        this.memoryPool.splice(i, 1);
        return ptr;
      }
    }
    /* Allocate new */
    return this.exports ? this.exports._malloc(size) : 0;
  }

  private freeToPool(ptr: number): void {
    if (ptr > 0 && this.memoryPool.length < 100) {
      this.memoryPool.push(ptr);
    } else if (ptr > 0 && this.exports) {
      this.exports._free(ptr);
    }
  }

  /* #28. Progressive WASM loading */
  async loadProgressive(): Promise<{ loaded: number; total: number }> {
    /* В реальной реализации: fetch с progress tracking */
    return { loaded: 100, total: 100 };
  }

  /* ---------- New UX APIs ---------- */

  setStreamCallback(callback: StreamCallback | null): void {
    this.streamCallback = callback;
  }

  async queryJson(query: string): Promise<KolibriQueryResult> {
    await this.ready();
    if (!this.exports) throw new Error("WASM bridge is not initialised");

    const bytes = textEncoder.encode(query);
    const queryPtr = this.exports._malloc(bytes.length + 1);
    const outputPtr = this.exports._malloc(OUTPUT_CAPACITY);
    if (!queryPtr || !outputPtr) {
      if (queryPtr) this.exports._free(queryPtr);
      if (outputPtr) this.exports._free(outputPtr);
      throw new Error("Не удалось выделить WASM-память для запроса");
    }
    try {
      new Uint8Array(this.exports.memory.buffer, queryPtr, bytes.length + 1).set(bytes);
      new Uint8Array(this.exports.memory.buffer, outputPtr, 1).set([0]);

      const status = this.exports._kolibri_bridge_query_json(queryPtr, outputPtr, OUTPUT_CAPACITY);
      if (status !== 0) {
        throw new Error(`WASM query failed (code ${status})`);
      }

      const outputBytes = new Uint8Array(this.exports.memory.buffer, outputPtr, OUTPUT_CAPACITY);
      let end = outputBytes.indexOf(0);
      if (end < 0) end = OUTPUT_CAPACITY;
      const jsonStr = textDecoder.decode(outputBytes.slice(0, end));

      return JSON.parse(jsonStr) as KolibriQueryResult;
    } finally {
      this.exports._free(queryPtr);
      this.exports._free(outputPtr);
    }
  }

  getProgress(): KolibriProgressInfo {
    if (!this.exports) {
      return { state: "idle", value: 0, detail: "" };
    }
    const statePtr = this.exports._kolibri_bridge_get_progress_state();
    const value = this.exports._kolibri_bridge_get_progress_value();
    const detailPtr = this.exports._kolibri_bridge_get_progress_detail();

    const mem = new Uint8Array(this.exports.memory.buffer);
    const readStr = (ptr: number): string => {
      let end = ptr;
      while (end < ptr + 256 && mem[end] !== 0) end++;
      return textDecoder.decode(mem.slice(ptr, end));
    };

    return {
      state: readStr(statePtr),
      value,
      detail: readStr(detailPtr),
    };
  }

  getThinking(): string {
    if (!this.exports) return "";
    const ptr = this.exports._kolibri_bridge_get_thinking();
    const mem = new Uint8Array(this.exports.memory.buffer);
    let end = ptr;
    while (end < ptr + 128 && mem[end] !== 0) end++;
    return textDecoder.decode(mem.slice(ptr, end));
  }

  cancelQuery(): void {
    if (!this.exports) return;
    this.exports._kolibri_bridge_cancel_query();
  }

  async health(): Promise<KolibriHealthInfo> {
    await this.ready();
    if (!this.exports) throw new Error("WASM bridge is not initialised");

    const outputPtr = this.exports._malloc(OUTPUT_CAPACITY);
    if (!outputPtr) throw new Error("Не удалось выделить WASM-память");

    try {
      this.exports._kolibri_bridge_health(outputPtr, OUTPUT_CAPACITY);
      const outputBytes = new Uint8Array(this.exports.memory.buffer, outputPtr, OUTPUT_CAPACITY);
      let end = outputBytes.indexOf(0);
      if (end < 0) end = OUTPUT_CAPACITY;
      const jsonStr = textDecoder.decode(outputBytes.slice(0, end));
      return JSON.parse(jsonStr) as KolibriHealthInfo;
    } finally {
      this.exports._free(outputPtr);
    }
  }

  async sendMessage(conversationId: string, message: string): Promise<KolibriQueryResult> {
    await this.ready();
    if (!this.exports) throw new Error("WASM bridge is not initialised");

    const convBytes = textEncoder.encode(conversationId);
    const msgBytes = textEncoder.encode(message);
    const convPtr = this.exports._malloc(convBytes.length + 1);
    const msgPtr = this.exports._malloc(msgBytes.length + 1);
    const outputPtr = this.exports._malloc(OUTPUT_CAPACITY);
    if (!convPtr || !msgPtr || !outputPtr) {
      if (convPtr) this.exports._free(convPtr);
      if (msgPtr) this.exports._free(msgPtr);
      if (outputPtr) this.exports._free(outputPtr);
      throw new Error("Не удалось выделить WASM-память");
    }
    try {
      new Uint8Array(this.exports.memory.buffer, convPtr, convBytes.length + 1).set(convBytes);
      new Uint8Array(this.exports.memory.buffer, msgPtr, msgBytes.length + 1).set(msgBytes);
      new Uint8Array(this.exports.memory.buffer, outputPtr, 1).set([0]);

      const status = this.exports._kolibri_bridge_send_message(convPtr, msgPtr, outputPtr, OUTPUT_CAPACITY);
      if (status !== 0) {
        throw new Error(`WASM send_message failed (code ${status})`);
      }

      const outputBytes = new Uint8Array(this.exports.memory.buffer, outputPtr, OUTPUT_CAPACITY);
      let end = outputBytes.indexOf(0);
      if (end < 0) end = OUTPUT_CAPACITY;
      const jsonStr = textDecoder.decode(outputBytes.slice(0, end));
      return JSON.parse(jsonStr) as KolibriQueryResult;
    } finally {
      this.exports._free(convPtr);
      this.exports._free(msgPtr);
      this.exports._free(outputPtr);
    }
  }

  private async initialise(): Promise<void> {
    const mod = await KolibriCoreModule({
      locateFile: (p: string) => `/src/lib/${p}`
    });
    this.exports = createExports(mod as unknown as WebAssembly.Exports);
    const status = this.exports._kolibri_bridge_init();
    if (status !== 0) {
      throw new Error(`Не удалось инициализировать C-ядро Kolibri (код ${status})`);
    }
  }

  async reset(): Promise<void> {
    await this.ready();
    if (!this.exports) return;
    const status = this.exports._kolibri_bridge_reset();
    if (status !== 0) {
      throw new Error(`Не удалось сбросить C-ядро Kolibri (код ${status})`);
    }
  }

  async ask(
    prompt: string,
    persona: "assistant" | "romantic" | "storyteller",
    context: KolibriContextTurn[],
    resetBeforeRun = false,
  ): Promise<string> {
    await this.ready();
    if (!this.exports) throw new Error("WASM bridge is not initialised");
    if (resetBeforeRun) {
      await this.reset();
    }

    const program = buildProgram(prompt, persona, context);
    const bytes = textEncoder.encode(program);
    const programPtr = this.exports._malloc(bytes.length + 1);
    const outputPtr = this.exports._malloc(OUTPUT_CAPACITY);
    if (!programPtr || !outputPtr) {
      if (programPtr) this.exports._free(programPtr);
      if (outputPtr) this.exports._free(outputPtr);
      throw new Error("Недостаточно памяти для выполнения kolibri.wasm");
    }

    try {
      const heap = new Uint8Array(this.exports.memory.buffer);
      heap.set(bytes, programPtr);
      heap[programPtr + bytes.length] = 0;
      const written = this.exports._kolibri_bridge_execute(programPtr, outputPtr, OUTPUT_CAPACITY);
      if (written < 0) {
        throw new Error(`Ошибка выполнения KolibriScript (код ${written})`);
      }
      const raw = textDecoder.decode(heap.subarray(outputPtr, outputPtr + written));
      const answer = sanitizeContextText(raw);
      if (looksWeak(prompt, answer)) {
        throw new Error(answer || "Kolibri C-core returned a weak response");
      }
      return answer;
    } finally {
      this.exports._free(programPtr);
      this.exports._free(outputPtr);
    }
  }
}

export const kolibriBridge = new KolibriWasmBridge();
