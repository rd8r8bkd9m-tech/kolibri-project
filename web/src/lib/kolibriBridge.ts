// // import { WASI } from "@wasmer/wasi/shim";

export interface KolibriBridgeResult {
  response: string;
  confidence: number;
  method: string;
  sources: number;
  duration_ms: number;
  thinking: string;
  conversation_id?: string;
  message_count?: number;
}

export interface KolibriHealth {
  status: string;
  uptime_ms: number;
  queries_processed: number;
  avg_response_ms: number;
  conversations_active: number;
}

export class KolibriBridge {
  private ready = false;
  private instance: WebAssembly.Instance | null = null;
  private memory: WebAssembly.Memory | null = null;

  constructor(instance: WebAssembly.Instance) {
    this.instance = instance;
    this.memory = (instance.exports.memory as WebAssembly.Memory) || null;
    this.ready = true;
  }

  static async load(url = "/kolibri.wasm"): Promise<KolibriBridge> {
    const response = await fetch(url);
    if (!response.ok) {
      throw new Error(`Не удалось загрузить WASM по адресу ${url}: ${response.statusText}`);
    }

    // Minimal WASI polyfill for browser
    const wasiImports = {
      wasi_snapshot_preview1: {
        proc_exit: () => {},
        fd_write: () => {},
        fd_read: () => {},
        fd_close: () => {},
        fd_seek: () => {},
        clock_time_get: () => {},
        random_get: () => {},
        environ_sizes_get: () => {},
        environ_get: () => {},
        args_sizes_get: () => {},
        args_get: () => {},
        path_open: () => {},
        path_create_directory: () => {},
        path_remove_directory: () => {},
        path_unlink_file: () => {},
        path_rename: () => {},
        path_filestat_get: () => {},
        path_filestat_set_times: () => {},
        fd_filestat_get: () => {},
        fd_filestat_set_size: () => {},
        fd_filestat_set_times: () => {},
        fd_pread: () => {},
        fd_pwrite: () => {},
        fd_readdir: () => {},
        poll_oneoff: () => {},
        sock_accept: () => {},
        sock_recv: () => {},
        sock_send: () => {},
        sock_shutdown: () => {},
      }
    };

    const importObject = {
      ...wasiImports,
      env: {
        abort: () => {
          throw new Error("Kolibri WASM abort");
        },
      },
    };

    const wasmModule = await WebAssembly.instantiateStreaming(response, importObject);
    const bridge = new KolibriBridge(wasmModule.instance);
    bridge.init();
    return bridge;
  }

  private get exports() {
    return this.instance?.exports as any;
  }

  private allocString(str: string): number {
    const encoder = new TextEncoder();
    const bytes = encoder.encode(str);
    const ptr = this.exports.malloc(bytes.length + 1);
    const mem = new Uint8Array(this.memory!.buffer);
    mem.set(bytes, ptr);
    mem[ptr + bytes.length] = 0;
    return ptr;
  }

  private freeString(ptr: number) {
    this.exports.free(ptr);
  }

  private readString(ptr: number): string {
    const mem = new Uint8Array(this.memory!.buffer);
    let end = ptr;
    while (mem[end] !== 0) end++;
    const decoder = new TextDecoder();
    return decoder.decode(mem.subarray(ptr, end));
  }

  isReady(): boolean {
    return this.ready;
  }

  init(): number {
    return this.exports.kolibri_bridge_init();
  }

  reset(): number {
    return this.exports.kolibri_bridge_reset();
  }

  health(): KolibriHealth {
    const capacity = 1024;
    const ptr = this.exports.malloc(capacity);
    this.exports.kolibri_bridge_health(ptr, capacity);
    const json = this.readString(ptr);
    this.freeString(ptr);
    return JSON.parse(json);
  }

  queryJson(query: string): KolibriBridgeResult {
    const qPtr = this.allocString(query);
    const capacity = 8192;
    const outPtr = this.exports.malloc(capacity);
    
    this.exports.kolibri_bridge_query_json(qPtr, outPtr, capacity);
    
    const json = this.readString(outPtr);
    this.freeString(qPtr);
    this.freeString(outPtr);
    
    return JSON.parse(json);
  }

  sendMessage(conversationId: string, message: string): KolibriBridgeResult {
    const idPtr = this.allocString(conversationId);
    const mPtr = this.allocString(message);
    const capacity = 8192;
    const outPtr = this.exports.malloc(capacity);
    
    this.exports.kolibri_bridge_send_message(idPtr, mPtr, outPtr, capacity);
    
    const json = this.readString(outPtr);
    this.freeString(idPtr);
    this.freeString(mPtr);
    this.freeString(outPtr);
    
    return JSON.parse(json);
  }

  cancelQuery(): void {
    this.exports.kolibri_bridge_cancel_query();
  }

  getProgress(): { state: string; value: number; detail: string } {
    return {
      state: this.readString(this.exports.kolibri_bridge_get_progress_state()),
      value: this.exports.kolibri_bridge_get_progress_value(),
      detail: this.readString(this.exports.kolibri_bridge_get_progress_detail()),
    };
  }

  getThinking(): string {
    return this.readString(this.exports.kolibri_bridge_get_thinking());
  }
}
