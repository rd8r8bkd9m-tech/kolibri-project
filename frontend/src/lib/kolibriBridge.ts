export interface KolibriContextTurn {
  prompt: string;
  answer: string;
}

import { looksWeak, sanitizeContextText } from "@/lib/answerSanitizer";

interface KolibriWasmExports {
  memory: WebAssembly.Memory;
  _malloc(size: number): number;
  _free(ptr: number): void;
  _kolibri_bridge_init(): number;
  _kolibri_bridge_reset(): number;
  _kolibri_bridge_execute(programPtr: number, outputPtr: number, outputCapacity: number): number;
}

const OUTPUT_CAPACITY = 8192;
const WASM_RESOURCE_URL = "/kolibri.wasm";
const RESPONSE_MODE = (import.meta.env.VITE_KOLIBRI_RESPONSE_MODE ?? "script").toLowerCase();
const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder("utf-8");

const WASI_ERRNO_SUCCESS = 0;
const WASI_ERRNO_INVAL = 28;
const WASI_FILETYPE_CHARACTER_DEVICE = 2;

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

class WasiAdapter {
  private memory: WebAssembly.Memory | null = null;
  private view: DataView | null = null;

  attach(memory: WebAssembly.Memory): void {
    this.memory = memory;
    this.view = new DataView(memory.buffer);
  }

  private ensureView(): DataView {
    if (!this.memory) throw new Error("WASI memory is not initialised");
    if (!this.view || this.view.buffer !== this.memory.buffer) {
      this.view = new DataView(this.memory.buffer);
    }
    return this.view;
  }

  get imports(): Record<string, Record<string, WebAssembly.ImportValue>> {
    return {
      wasi_snapshot_preview1: {
        args_get: () => WASI_ERRNO_SUCCESS,
        args_sizes_get: (argcPtr: number, argvBufSizePtr: number) => {
          const view = this.ensureView();
          view.setUint32(argcPtr, 0, true);
          view.setUint32(argvBufSizePtr, 0, true);
          return WASI_ERRNO_SUCCESS;
        },
        environ_get: () => WASI_ERRNO_SUCCESS,
        environ_sizes_get: (countPtr: number, sizePtr: number) => {
          const view = this.ensureView();
          view.setUint32(countPtr, 0, true);
          view.setUint32(sizePtr, 0, true);
          return WASI_ERRNO_SUCCESS;
        },
        fd_close: () => WASI_ERRNO_SUCCESS,
        fd_fdstat_get: (_fd: number, statPtr: number) => {
          const view = this.ensureView();
          for (let offset = 0; offset < 24; offset += 1) view.setUint8(statPtr + offset, 0);
          view.setUint8(statPtr, WASI_FILETYPE_CHARACTER_DEVICE);
          return WASI_ERRNO_SUCCESS;
        },
        fd_seek: () => WASI_ERRNO_INVAL,
        fd_write: (_fd: number, iovsPtr: number, iovsLen: number, nwrittenPtr: number) => {
          const view = this.ensureView();
          let bytesWritten = 0;
          for (let index = 0; index < iovsLen; index += 1) {
            const len = view.getUint32(iovsPtr + index * 8 + 4, true);
            bytesWritten += len;
          }
          view.setUint32(nwrittenPtr, bytesWritten >>> 0, true);
          view.setUint32(nwrittenPtr + 4, Math.floor(bytesWritten / 2 ** 32) >>> 0, true);
          return WASI_ERRNO_SUCCESS;
        },
        proc_exit: (status: number) => {
          throw new Error(`WASI program exited with code ${status}`);
        },
        random_get: (ptr: number, len: number) => {
          if (!this.memory) return WASI_ERRNO_INVAL;
          const bytes = new Uint8Array(this.memory.buffer, ptr, len);
          if (typeof crypto !== "undefined" && typeof crypto.getRandomValues === "function") {
            crypto.getRandomValues(bytes);
          } else {
            for (let i = 0; i < len; i += 1) bytes[i] = Math.floor(Math.random() * 256);
          }
          return WASI_ERRNO_SUCCESS;
        },
        clock_time_get: (_clockId: number, _precision: number, timePtr: number) => {
          const view = this.ensureView();
          const nowMs =
            typeof performance !== "undefined" && typeof performance.now === "function"
              ? performance.timeOrigin + performance.now()
              : Date.now();
          const nowNs = BigInt(Math.floor(nowMs * 1_000_000));
          if (typeof view.setBigUint64 === "function") {
            view.setBigUint64(timePtr, nowNs, true);
          } else {
            const low = Number(nowNs & BigInt(0xffffffff));
            const high = Number((nowNs >> BigInt(32)) & BigInt(0xffffffff));
            view.setUint32(timePtr, low >>> 0, true);
            view.setUint32(timePtr + 4, high >>> 0, true);
          }
          return WASI_ERRNO_SUCCESS;
        },
      },
    };
  }
}

function resolveMemory(exports: WebAssembly.Exports): WebAssembly.Memory {
  const memory = (exports as Record<string, unknown>).memory;
  if (memory instanceof WebAssembly.Memory) return memory;
  throw new Error("WASM-модуль не экспортирует память");
}

function resolveFunction(exports: WebAssembly.Exports, candidates: readonly string[]): (...args: number[]) => number {
  const lookup = exports as Record<string, unknown>;
  for (const name of candidates) {
    const candidate = lookup[name];
    if (typeof candidate === "function") {
      return candidate as (...args: number[]) => number;
    }
  }
  throw new Error(`WASM-модуль не экспортирует ${candidates.join(", ")}`);
}

function createExports(rawExports: WebAssembly.Exports, wasi: WasiAdapter): KolibriWasmExports {
  const memory = resolveMemory(rawExports);
  wasi.attach(memory);
  return {
    memory,
    _malloc: resolveFunction(rawExports, ["_malloc", "malloc"]) as (size: number) => number,
    _free: resolveFunction(rawExports, ["_free", "free"]) as (ptr: number) => void,
    _kolibri_bridge_init: resolveFunction(rawExports, ["_kolibri_bridge_init", "kolibri_bridge_init"]) as () => number,
    _kolibri_bridge_reset: resolveFunction(rawExports, ["_kolibri_bridge_reset", "kolibri_bridge_reset"]) as () => number,
    _kolibri_bridge_execute: resolveFunction(
      rawExports,
      ["_kolibri_bridge_execute", "kolibri_bridge_execute"],
    ) as (programPtr: number, outputPtr: number, outputCapacity: number) => number,
  };
}

class KolibriWasmBridge {
  private exports: KolibriWasmExports | null = null;
  private readonly wasi = new WasiAdapter();
  private initPromise: Promise<void> | null = null;

  async ready(): Promise<void> {
    if (RESPONSE_MODE === "llm") {
      throw new Error("WASM mode disabled by VITE_KOLIBRI_RESPONSE_MODE=llm");
    }
    if (!this.initPromise) {
      this.initPromise = this.initialise();
    }
    return this.initPromise;
  }

  private async initialise(): Promise<void> {
    const imports = this.wasi.imports;
    let instance: WebAssembly.Instance;
    if ("instantiateStreaming" in WebAssembly) {
      try {
        const streaming = await WebAssembly.instantiateStreaming(fetch(WASM_RESOURCE_URL), imports);
        instance = streaming.instance;
      } catch {
        const response = await fetch(WASM_RESOURCE_URL);
        if (!response.ok) {
          throw new Error(`Не удалось загрузить kolibri.wasm: ${response.status}`);
        }
        const bytes = await response.arrayBuffer();
        const loaded = await WebAssembly.instantiate(bytes, imports);
        instance = loaded.instance;
      }
    } else {
      const response = await fetch(WASM_RESOURCE_URL);
      if (!response.ok) {
        throw new Error(`Не удалось загрузить kolibri.wasm: ${response.status}`);
      }
      const bytes = await response.arrayBuffer();
      const loaded = await WebAssembly.instantiate(bytes, imports);
      instance = loaded.instance;
    }
    this.exports = createExports(instance.exports, this.wasi);
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
