export interface KolibriCoreModuleExports extends WebAssembly.Exports {
  _malloc(size: number): number;
  _free(ptr: number): void;
  _kolibri_core_wasm_init(): number;
  _kolibri_wasm_reason(a: number, b: number, c: number): number;
  _kolibri_wasm_solve_linear(a: number, b: number, c: number): number;
  _kolibri_wasm_solve_quadratic(a: number, b: number, c: number): number;
  _kolibri_wasm_tokenize(a: number, b: number, c: number): number;
  _kolibri_wasm_verify(a: number, b: number, c: number): number;
  _kolibri_wasm_explain(a: number, b: number, c: number): number;
  _kolibri_wasm_domain_stats(a: number, b: number): number;
  _kolibri_wasm_free(): void;
  _kolibri_bridge_init(): number;
  _kolibri_bridge_reset(): number;
  _kolibri_bridge_execute(a: number, b: number, c: number): number;
  _kolibri_bridge_query(a: number, b: number, c: number): number;
  _kolibri_bridge_query_json(a: number, b: number, c: number): number;
  _kolibri_bridge_create_conversation(a: number): number;
  _kolibri_bridge_delete_conversation(a: number): number;
  _kolibri_bridge_send_message(a: number, b: number, c: number, d: number): number;
  _kolibri_bridge_get_progress_state(): number;
  _kolibri_bridge_get_progress_value(): number;
  _kolibri_bridge_get_progress_detail(): number;
  _kolibri_bridge_get_thinking(): number;
  _kolibri_bridge_cancel_query(): void;
  _kolibri_bridge_batch_query(a: number, b: number, c: number, d: number): number;
  _kolibri_bridge_get_memory_usage(): number;
  _kolibri_bridge_health(a: number, b: number): number;
  _kolibri_bridge_set_stream_callback(a: number, b: number): void;
}

export interface KolibriCoreModuleConfig {
  locateFile?: (path: string) => string;
}

export default function KolibriCoreModule(config?: KolibriCoreModuleConfig): Promise<KolibriCoreModuleExports>;
