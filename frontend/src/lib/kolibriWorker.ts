/// <reference lib="webworker" />

import KolibriCoreModule from "./kolibriCoreModule";

declare const self: DedicatedWorkerGlobalScope;

type WorkerMessage =
  | { type: "query"; id: string; query: string }
  | { type: "health"; id: string };

type KolibriWorkerModule = Awaited<ReturnType<typeof KolibriCoreModule>>;

const OUTPUT_CAPACITY = 8192;
const encoder = new TextEncoder();
const decoder = new TextDecoder("utf-8");

let modulePromise: Promise<KolibriWorkerModule> | null = null;

function locateWasmAsset(path: string): string {
  return new URL(path, import.meta.url).href;
}

async function getModule(): Promise<KolibriWorkerModule> {
  if (!modulePromise) {
    modulePromise = KolibriCoreModule({
      locateFile: locateWasmAsset,
    }).then((mod) => {
      if (typeof mod._kolibri_core_wasm_init === "function") {
        const rc = mod._kolibri_core_wasm_init();
        if (rc !== 0) {
          throw new Error(`kolibri_core_wasm_init failed with code ${rc}`);
        }
      }
      return mod;
    });
  }
  return modulePromise;
}

function writeCString(memory: WebAssembly.Memory, ptr: number, value: string): void {
  const bytes = encoder.encode(value);
  const view = new Uint8Array(memory.buffer, ptr, bytes.length + 1);
  view.set(bytes);
  view[bytes.length] = 0;
}

function readCString(memory: WebAssembly.Memory, ptr: number, capacity: number): string {
  const view = new Uint8Array(memory.buffer, ptr, capacity);
  let end = view.indexOf(0);
  if (end < 0) end = capacity;
  return decoder.decode(view.subarray(0, end));
}

async function runJsonCall(
  invoke: (mod: KolibriWorkerModule, inputPtr: number, outputPtr: number, capacity: number) => number,
  payload: object,
): Promise<string> {
  const mod = await getModule();
  const json = JSON.stringify(payload);
  const inputPtr = mod._malloc(json.length + 1);
  const outputPtr = mod._malloc(OUTPUT_CAPACITY);
  if (!inputPtr || !outputPtr) {
    if (inputPtr) mod._free(inputPtr);
    if (outputPtr) mod._free(outputPtr);
    throw new Error("worker failed to allocate WASM memory");
  }
  try {
    writeCString(mod.memory as WebAssembly.Memory, inputPtr, json);
    const rc = invoke(mod, inputPtr, outputPtr, OUTPUT_CAPACITY);
    if (rc !== 0) {
      throw new Error(`worker call failed with code ${rc}`);
    }
    return readCString(mod.memory as WebAssembly.Memory, outputPtr, OUTPUT_CAPACITY);
  } finally {
    mod._free(inputPtr);
    mod._free(outputPtr);
  }
}

async function handleQuery(message: Extract<WorkerMessage, { type: "query" }>): Promise<void> {
  const result = await runJsonCall(
    (mod, inputPtr, outputPtr, capacity) => mod._kolibri_wasm_reason(inputPtr, outputPtr, capacity),
    { query: message.query },
  );
  self.postMessage({ type: "result", id: message.id, result });
}

async function handleHealth(message: Extract<WorkerMessage, { type: "health" }>): Promise<void> {
  const mod = await getModule();
  self.postMessage({
    type: "health",
    id: message.id,
    status: "ok",
    memory_bytes: (mod.memory as WebAssembly.Memory).buffer.byteLength,
  });
}

self.addEventListener("message", (event: MessageEvent<WorkerMessage>) => {
  const message = event.data;
  const run = async (): Promise<void> => {
    if (message.type === "query") {
      await handleQuery(message);
      return;
    }
    if (message.type === "health") {
      await handleHealth(message);
    }
  };
  void run().catch((error: unknown) => {
    const errorMessage = error instanceof Error ? error.message : String(error);
    self.postMessage({ type: "error", id: "id" in message ? message.id : undefined, error: errorMessage });
  });
});

void getModule()
  .then(() => {
    self.postMessage({ type: "ready" });
  })
  .catch((error: unknown) => {
    const errorMessage = error instanceof Error ? error.message : String(error);
    self.postMessage({ type: "error", error: errorMessage });
  });

export {};
