/// <reference types="vite/client" />

declare interface ImportMetaEnv {
  readonly VITE_KOLIBRI_RESPONSE_MODE?: string;
  readonly VITE_KOLIBRI_API_BASE?: string;
}

declare interface ImportMeta {
  readonly env: ImportMetaEnv;
}
