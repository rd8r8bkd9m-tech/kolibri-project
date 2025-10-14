/**
 * kolibri-bridge.ts
 *
 * Высокоуровневый мост между интерфейсом и ядром KolibriScript. Модуль
 * пытается загрузить WebAssembly-модуль `kolibri.wasm`, а при сбоях
 * gracefully деградирует до резервных реализаций (LLM или статическое
 * сообщение). Вся логика построения KolibriScript программ собрана здесь,
 * чтобы обеспечить прозрачность и детерминированность поведения фронтенда.
 */

import type { KnowledgeSnippet } from "../types/knowledge";
import { sendKnowledgeFeedback, teachKnowledge } from "./knowledge";
import { findModeLabel } from "./modes";

export interface KolibriBridge {
  readonly ready: Promise<void>;
  ask(prompt: string, mode?: string, context?: KnowledgeSnippet[]): Promise<string>;
  reset(): Promise<void>;
}

interface KolibriWasmExports {
  memory: WebAssembly.Memory;
  _malloc(size: number): number;
  _free(ptr: number): void;
  _kolibri_bridge_init(): number;
  _kolibri_bridge_reset(): number;
  _kolibri_bridge_execute(programPtr: number, outputPtr: number, outputCapacity: number): number;
}

const OUTPUT_CAPACITY = 8192;
const DEFAULT_MODE_LABEL = "Нейтральный";
const WASM_RESOURCE_URL = "/kolibri.wasm";
const WASM_INFO_URL = "/kolibri.wasm.txt";
const DEFAULT_API_BASE = "/api";
const RESPONSE_MODE = (import.meta.env.VITE_KOLIBRI_RESPONSE_MODE ?? "script").toLowerCase();
const RAW_API_BASE = import.meta.env.VITE_KOLIBRI_API_BASE ?? DEFAULT_API_BASE;

const API_BASE = (() => {
  const trimmed = RAW_API_BASE.trim();
  if (!trimmed) {
    return DEFAULT_API_BASE;
  }
  return trimmed.endsWith("/") ? trimmed.slice(0, -1) : trimmed;
})();

const LLM_INFERENCE_URL = `${API_BASE}/v1/infer`;
const SHOULD_USE_LLM = RESPONSE_MODE === "llm";

const COMMAND_PATTERN = /^(показать|обучить|спросить|тикнуть|сохранить)/i;
const PROGRAM_START_PATTERN = /начало\s*:/i;
const PROGRAM_END_PATTERN = /конец\./i;

const textDecoder = new TextDecoder("utf-8");
const textEncoder = new TextEncoder();

const WASI_ERRNO_SUCCESS = 0;
const WASI_ERRNO_BADF = 8;
const WASI_ERRNO_INVAL = 28;
const WASI_ERRNO_IO = 29;
const WASI_FILETYPE_CHARACTER_DEVICE = 2;

class WasiAdapter {
  private memory: WebAssembly.Memory | null = null;
  private view: DataView | null = null;

  constructor(
    private readonly stdout: (text: string) => void = (text) => console.debug("[kolibri-wasi]", text.trim()),
    private readonly stderr: (text: string) => void = (text) => console.warn("[kolibri-wasi][stderr]", text.trim()),
  ) {}

  attach(memory: WebAssembly.Memory): void {
    this.memory = memory;
    this.view = new DataView(memory.buffer);
  }

  private ensureView(): DataView {
    if (!this.memory) {
      throw new Error("WASI memory is not initialised");
    }
    if (!this.view || this.view.buffer !== this.memory.buffer) {
      this.view = new DataView(this.memory.buffer);
    }
    return this.view;
  }

  private ensureBytes(): Uint8Array {
    if (!this.memory) {
      throw new Error("WASI memory is not initialised");
    }
    return new Uint8Array(this.memory.buffer);
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
          for (let offset = 0; offset < 24; offset += 1) {
            view.setUint8(statPtr + offset, 0);
          }
          view.setUint8(statPtr, WASI_FILETYPE_CHARACTER_DEVICE);
          return WASI_ERRNO_SUCCESS;
        },
        fd_seek: () => WASI_ERRNO_IO,
        fd_write: (fd: number, iovsPtr: number, iovsLen: number, nwrittenPtr: number) => {
          if (!this.memory) {
            return WASI_ERRNO_INVAL;
          }
          const view = this.ensureView();
          let bytesWritten = 0;
          for (let index = 0; index < iovsLen; index += 1) {
            const ptr = view.getUint32(iovsPtr + index * 8, true);
            const len = view.getUint32(iovsPtr + index * 8 + 4, true);
            bytesWritten += len;
            if (fd === 1 || fd === 2) {
              const chunk = new Uint8Array(this.memory.buffer, ptr, len);
              const text = textDecoder.decode(chunk);
              if (fd === 1) {
                this.stdout(text);
              } else {
                this.stderr(text);
              }
            }
          }
          view.setUint32(nwrittenPtr, bytesWritten >>> 0, true);
          view.setUint32(nwrittenPtr + 4, Math.floor(bytesWritten / 2 ** 32) >>> 0, true);
          return WASI_ERRNO_SUCCESS;
        },
        proc_exit: (status: number) => {
          throw new Error(`WASI program exited with code ${status}`);
        },
        random_get: (ptr: number, len: number) => {
          if (!this.memory) {
            return WASI_ERRNO_INVAL;
          }
          const bytes = new Uint8Array(this.memory.buffer, ptr, len);
          if (typeof crypto !== "undefined" && typeof crypto.getRandomValues === "function") {
            crypto.getRandomValues(bytes);
          } else {
            for (let index = 0; index < len; index += 1) {
              bytes[index] = Math.floor(Math.random() * 256);
            }
          }
          return WASI_ERRNO_SUCCESS;
        },
      },
    };
  }
}

const resolveMemory = (exports: WebAssembly.Exports): WebAssembly.Memory => {
  const memory = (exports as Record<string, unknown>).memory;
  if (memory instanceof WebAssembly.Memory) {
    return memory;
  }
  throw new Error("WASM-модуль не экспортирует память WebAssembly");
};

const resolveFunction = (exports: WebAssembly.Exports, candidates: readonly string[]): (...args: number[]) => number => {
  const lookup = exports as Record<string, unknown>;
  for (const name of candidates) {
    const candidate = lookup[name];
    if (typeof candidate === "function") {
      return candidate as (...args: number[]) => number;
    }
  }
  throw new Error(`WASM-модуль не экспортирует функции ${candidates.join(" или ")}`);
};

const createKolibriWasmExports = (rawExports: WebAssembly.Exports, wasi: WasiAdapter): KolibriWasmExports => {
  const memory = resolveMemory(rawExports);
  wasi.attach(memory);
  return {
    memory,
    _malloc: resolveFunction(rawExports, ["_malloc", "malloc"]) as (size: number) => number,
    _free: resolveFunction(rawExports, ["_free", "free"]) as (ptr: number) => void,
    _kolibri_bridge_init: resolveFunction(rawExports, ["_kolibri_bridge_init", "kolibri_bridge_init"]) as () => number,
    _kolibri_bridge_reset: resolveFunction(rawExports, ["_kolibri_bridge_reset", "kolibri_bridge_reset"]) as () => number,
    _kolibri_bridge_execute: resolveFunction(rawExports, ["_kolibri_bridge_execute", "kolibri_bridge_execute"]) as (
      programPtr: number,
      outputPtr: number,
      outputCapacity: number,
    ) => number,
  };
};

const escapeScriptString = (value: string): string =>
  value
    .replace(/\\/g, "\\\\")
    .replace(/"/g, '\\"')
    .replace(/\r?\n/g, "\\n");

const normaliseLines = (input: string): string[] =>
  input
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line.length > 0);

export function buildScript(prompt: string, mode: string, context: KnowledgeSnippet[]): string {
  const trimmedPrompt = prompt.trim();
  const resolvedMode = mode ? findModeLabel(mode) : DEFAULT_MODE_LABEL;

  if (!trimmedPrompt) {
    return `начало:\n    показать "Пустой запрос"\nконец.\n`;
  }

  if (PROGRAM_START_PATTERN.test(trimmedPrompt) && PROGRAM_END_PATTERN.test(trimmedPrompt)) {
    return trimmedPrompt.endsWith("\n") ? trimmedPrompt : `${trimmedPrompt}\n`;
  }

  const lines: string[] = ["начало:"];

  if (resolvedMode && resolvedMode !== DEFAULT_MODE_LABEL) {
    lines.push(`    показать "Режим: ${escapeScriptString(resolvedMode)}"`);
  }

  lines.push(`    переменная вопрос = "${escapeScriptString(trimmedPrompt)}"`);
  lines.push(`    показать "Вопрос: ${escapeScriptString(trimmedPrompt)}"`);

  const uniqueAnswers = new Set<string>();
  context.forEach((snippet, index) => {
    const answer = snippet.content.trim();
    if (!answer) {
      return;
    }
    const normalised = answer.length > 400 ? `${answer.slice(0, 397)}…` : answer;
    if (uniqueAnswers.has(normalised)) {
      return;
    }
    uniqueAnswers.add(normalised);
    const sourceLabel = snippet.title || snippet.id;
    lines.push(`    переменная источник_${index + 1} = "${escapeScriptString(snippet.source ?? snippet.id)}"`);
    lines.push(`    обучить связь "${escapeScriptString(trimmedPrompt)}" -> "${escapeScriptString(normalised)}"`);
    lines.push(`    показать "Источник ${index + 1}: ${escapeScriptString(sourceLabel)}"`);
  });

  if (normaliseLines(trimmedPrompt).every((line) => COMMAND_PATTERN.test(line))) {
    lines.push("    вызвать эволюцию");
  }

  lines.push(`    создать формулу ответ из "ассоциация"`);
  lines.push("    вызвать эволюцию");
  lines.push(`    оценить ответ на задаче "${escapeScriptString(trimmedPrompt)}"`);
  lines.push("    показать итог");
  lines.push("конец.");

  return `${lines.join("\n")}\n`;
}

async function describeWasmFailure(error: unknown): Promise<string> {
  const baseReason = error instanceof Error && error.message ? error.message : String(error ?? "Неизвестная ошибка");

  try {
    const response = await fetch(WASM_INFO_URL, { cache: "no-store" });
    if (!response.ok) {
      return baseReason;
    }
    const infoText = (await response.text()).trim();
    return infoText ? `${baseReason}\n\n${infoText}` : baseReason;
  } catch {
    return baseReason;
  }
}

class KolibriWasmRuntime {
  private exports: KolibriWasmExports | null = null;
  private readonly wasi = new WasiAdapter();

  async initialise(): Promise<void> {
    const instance = await this.instantiate();
    const exports = createKolibriWasmExports(instance.exports, this.wasi);
    const initResult = exports._kolibri_bridge_init();
    if (initResult !== 0) {
      throw new Error(`Не удалось инициализировать KolibriScript (код ${initResult})`);
    }
    this.exports = exports;
  }

  private async instantiate(): Promise<WebAssembly.Instance> {
    const importObject = this.wasi.imports;

    if ("instantiateStreaming" in WebAssembly) {
      try {
        const streaming = await WebAssembly.instantiateStreaming(fetch(WASM_RESOURCE_URL), importObject);
        return streaming.instance;
      } catch (error) {
        console.warn("Kolibri WASM streaming instantiation failed, retrying with ArrayBuffer.", error);
      }
    }

    const response = await fetch(WASM_RESOURCE_URL);
    if (!response.ok) {
      throw new Error(`Не удалось загрузить kolibri.wasm: ${response.status} ${response.statusText}`);
    }
    const bytes = await response.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes, importObject);
    return instance;
  }

  async execute(prompt: string, mode: string, context: KnowledgeSnippet[]): Promise<string> {
    if (!this.exports) {
      throw new Error("Kolibri WASM мост не инициализирован");
    }

    const script = buildScript(prompt, mode ?? DEFAULT_MODE_LABEL, context);
    const scriptBytes = textEncoder.encode(script);
    const exports = this.exports;
    const programPtr = exports._malloc(scriptBytes.length + 1);
    const outputPtr = exports._malloc(OUTPUT_CAPACITY);

    if (!programPtr || !outputPtr) {
      if (programPtr) {
        exports._free(programPtr);
      }
      if (outputPtr) {
        exports._free(outputPtr);
      }
      throw new Error("Недостаточно памяти для выполнения KolibriScript");
    }

    try {
      const heap = new Uint8Array(exports.memory.buffer);
      heap.set(scriptBytes, programPtr);
      heap[programPtr + scriptBytes.length] = 0;

      const written = exports._kolibri_bridge_execute(programPtr, outputPtr, OUTPUT_CAPACITY);
      if (written < 0) {
        throw new Error(this.describeExecutionError(written));
      }

      const outputBytes = heap.subarray(outputPtr, outputPtr + written);
      const rawText = textDecoder.decode(outputBytes).trim();
      const answer = rawText.length === 0 ? "KolibriScript завершил работу без вывода." : rawText;

      void teachKnowledge(prompt, answer);
      void sendKnowledgeFeedback("good", prompt, answer);

      return answer;
    } finally {
      exports._free(programPtr);
      exports._free(outputPtr);
    }
  }

  async reset(): Promise<void> {
    if (!this.exports) {
      throw new Error("Kolibri WASM мост не инициализирован");
    }
    const result = this.exports._kolibri_bridge_reset();
    if (result !== 0) {
      throw new Error(`Не удалось сбросить KolibriScript (код ${result})`);
    }
  }

  private describeExecutionError(code: number): string {
    switch (code) {
      case -1:
        return "Не удалось инициализировать KolibriScript.";
      case -2:
        return "WASM-модуль не смог подготовить временный вывод.";
      case -3:
        return "KolibriScript сообщил об ошибке при разборе программы.";
      case -4:
        return "Во время выполнения KolibriScript произошла ошибка.";
      case -5:
        return "Некорректные аргументы вызова KolibriScript.";
      default:
        return `Неизвестная ошибка KolibriScript (код ${code}).`;
    }
  }
}

class KolibriScriptBridge implements KolibriBridge {
  readonly ready = Promise.resolve();

  constructor(private readonly runtime: KolibriWasmRuntime) {}

  async ask(prompt: string, mode: string = DEFAULT_MODE_LABEL, context: KnowledgeSnippet[] = []): Promise<string> {
    return this.runtime.execute(prompt, mode, context);
  }

  async reset(): Promise<void> {
    await this.runtime.reset();
  }
}

class KolibriFallbackBridge implements KolibriBridge {
  readonly ready = Promise.resolve();

  constructor(private readonly reason: string) {}

  async ask(): Promise<string> {
    return [
      "KolibriScript недоступен: kolibri.wasm не был загружен.",
      `Причина: ${this.reason}`,
      "Запустите scripts/build_wasm.sh или установите переменную KOLIBRI_ALLOW_WASM_STUB=1 для деградированного режима.",
    ].join("\n");
  }

  async reset(): Promise<void> {
    // Нет состояния для сброса.
  }
}

class KolibriLLMBridge implements KolibriBridge {
  readonly ready = Promise.resolve();

  constructor(private readonly endpoint: string, private readonly fallback: KolibriBridge) {}

  async ask(prompt: string, mode: string = DEFAULT_MODE_LABEL, context: KnowledgeSnippet[] = []): Promise<string> {
    const payload = { prompt, mode, context };
    try {
      const response = await fetch(this.endpoint, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      });

      if (!response.ok) {
        throw new Error(`LLM proxy responded with ${response.status} ${response.statusText}`);
      }

      const data = (await response.json()) as { response?: unknown };
      if (typeof data.response !== "string" || !data.response.trim()) {
        throw new Error("LLM proxy returned an unexpected payload.");
      }

      return data.response.trim();
    } catch (error) {
      console.warn("[kolibri-bridge] Ошибка при запросе к LLM, выполняем KolibriScript.", error);
      const fallbackResponse = await this.fallback.ask(prompt, mode, context);
      return `${fallbackResponse}\n\n(Ответ сгенерирован KolibriScript из-за ошибки LLM.)`;
    }
  }

  async reset(): Promise<void> {
    await this.fallback.reset();
  }
}

const createBridge = async (): Promise<KolibriBridge> => {
  const runtime = new KolibriWasmRuntime();

  try {
    await runtime.initialise();
  } catch (error) {
    const reason = await describeWasmFailure(error);
    console.warn("[kolibri-bridge] Переход в деградированный режим без WebAssembly.", reason);
    return new KolibriFallbackBridge(reason);
  }

  const scriptBridge = new KolibriScriptBridge(runtime);

  if (SHOULD_USE_LLM) {
    return new KolibriLLMBridge(LLM_INFERENCE_URL, scriptBridge);
  }

  return scriptBridge;
};

const bridgePromise: Promise<KolibriBridge> = createBridge();

const kolibriBridge: KolibriBridge = {
  ready: bridgePromise.then(() => undefined),
  async ask(prompt: string, mode?: string, context: KnowledgeSnippet[] = []): Promise<string> {
    const bridge = await bridgePromise;
    return bridge.ask(prompt, mode, context);
  },
  async reset(): Promise<void> {
    const bridge = await bridgePromise;
    await bridge.reset();
  },
};

export default kolibriBridge;
