export type KolibriBridgeResult = {
  response: string;
  confidence: number;
  method: string;
  sources: number;
  duration_ms: number;
  thinking: string;
};

export class KolibriBridge {
  private module: any = null;
  private ready = false;

  static async load(): Promise<KolibriBridge> {
    const bridge = new KolibriBridge();
    return new Promise((resolve) => {
      const script = document.createElement('script');
      script.src = '/kolibri_engine.js';
      script.onload = async () => {
        try {
          // @ts-ignore
          const factory = window.createKolibriModule;
          bridge.module = await factory();
          bridge.module._kolibri_bridge_init();

          const response = await fetch('/knowledge.json');
          const knowledge = await response.json();
          for (const item of knowledge.slice(0, 1000)) {
            bridge.module.ccall('kolibri_mem_store', null, ['string', 'string', 'number'], [item.premise, item.conclusion, 1.0]);
          }

          bridge.ready = true;
          resolve(bridge);
        } catch (e) {
          console.error("Bridge Init Error:", e);
          resolve(bridge);
        }
      };
      document.head.appendChild(script);
    });
  }

  query(text: string): KolibriBridgeResult {
    if (!this.ready || !this.module) return { response: "Ядро загружается...", confidence: 0, method: "wait", sources: 0, duration_ms: 0, thinking: "" };
    try {
      const capacity = 16384;
      const qPtr = this.module._malloc(text.length * 4 + 1);
      this.module.stringToUTF8(text, qPtr, text.length * 4 + 1);
      const outPtr = this.module._malloc(capacity);

      this.module._kolibri_bridge_query_json(qPtr, outPtr, capacity);
      const res = JSON.parse(this.module.UTF8ToString(outPtr));

      this.module._free(qPtr);
      this.module._free(outPtr);
      return res;
    } catch (e) {
      return { response: "Ошибка ядра", confidence: 0, method: "error", sources: 0, duration_ms: 0, thinking: "" };
    }
  }
}
