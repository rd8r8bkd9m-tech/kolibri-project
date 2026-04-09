import type {
  AccountPreferencesResponse,
  AccountProfileResponse,
  AuthStatusResponse,
  ChatApiResponse,
  ConversationListResponse,
  ConversationSummary,
  ConversationTurnsResponse,
  ImagineRequest,
  ImagineResponse,
  LearnTextDemoResponse,
  ModelOption,
  ModelStatus,
  QualityBenchmarkHistoryResponse,
  SwarmKpackExportResponse,
  SwarmRuntimeStatusResponse,
} from "@/types";
import { kolibriBridge, type KolibriContextTurn, type KolibriQueryResult, type KolibriProgressInfo, type KolibriHealthInfo } from "@/lib/kolibriBridge";
import { analyzeImageLocally } from "@/lib/localImageAnalysis";
import { useChatStore } from "@/store/useChatStore";

export interface StreamRequest {
  prompt: string;
  model: ModelOption;
  sessionId: string;
  persona: "assistant" | "romantic" | "storyteller";
  memoryEnabled: boolean;
  context: KolibriContextTurn[];
}

export interface StreamChatResult {
  conversationId: string | null;
  method?: string;
  durationMs?: number;
  knowledgeHits?: number;
  confidence?: number;
  sources?: number;
  thinking?: string;
  /* Estimator / project metadata */
  productMode?: string;
  projectActive?: boolean;
  domainMode?: string;
  estimateStage?: string;
  projectKind?: string;
  projectAreaM2?: number;
}

/* #22. Markdown rendering support */
export interface MarkdownRenderOptions {
  codeHighlight?: boolean;
  mathRendering?: boolean;
}

/* #23. Keyboard shortcuts */
export const KEYBOARD_SHORTCUTS = {
  SEND: "Ctrl+Enter",
  CLEAR: "Escape",
  LAST_MESSAGE: "ArrowUp",
  SEARCH: "Ctrl+K",
} as const;

/* #24. Response diff view */
export interface ResponseDiff {
  oldResponse: string;
  newResponse: string;
  changes: Array<{ type: "added" | "removed"; text: string }>;
}

export function computeResponseDiff(oldResponse: string, newResponse: string): ResponseDiff {
  const changes: Array<{ type: "added" | "removed"; text: string }> = [];
  /* Simple diff: сравниваем по строкам */
  const oldLines = oldResponse.split("\n");
  const newLines = newResponse.split("\n");

  for (const line of newLines) {
    if (!oldLines.includes(line)) {
      changes.push({ type: "added", text: line });
    }
  }
  for (const line of oldLines) {
    if (!newLines.includes(line)) {
      changes.push({ type: "removed", text: line });
    }
  }

  return { oldResponse, newResponse, changes };
}

/* #25. Offline PWA caching */
const PWA_CACHE_NAME = "kolibri-responses-v1";
const PWA_MAX_CACHED = 50;

export async function cacheResponse(key: string, response: StreamChatResult): Promise<void> {
  if (typeof caches === "undefined") return;
  try {
    const cache = await caches.open(PWA_CACHE_NAME);
    const request = new Request(`/api/cache/${encodeURIComponent(key)}`);
    const cacheResponse = new Response(JSON.stringify(response), {
      headers: { "Content-Type": "application/json" },
    });
    await cache.put(request, cacheResponse);

    /* Удаляем старые записи если > PWA_MAX_CACHED */
    const keys = await cache.keys();
    if (keys.length > PWA_MAX_CACHED) {
      await cache.delete(keys[0]);
    }
  } catch {
    /* Cache unavailable */
  }
}

export async function getCachedResponse(key: string): Promise<StreamChatResult | null> {
  if (typeof caches === "undefined") return null;
  try {
    const cache = await caches.open(PWA_CACHE_NAME);
    const request = new Request(`/api/cache/${encodeURIComponent(key)}`);
    const response = await cache.match(request);
    if (response) {
      return response.json() as Promise<StreamChatResult>;
    }
  } catch {
    /* Cache unavailable */
  }
  return null;
}

export interface VisionAnalyzeResponse {
  response: string;
  provider: string;
  model: string;
  duration_ms: number;
  mime_type: string;
  width?: number | null;
  height?: number | null;
}

export interface LearnTextDemoRequest {
  text: string;
  question: string;
  title?: string;
  source?: string;
  category?: string;
  conversation_id?: string;
  client_id?: string;
  temperature?: number;
  profile?: "fast" | "balanced" | "deep";
  time_budget_ms?: number;
  persona?: "assistant" | "romantic" | "storyteller";
  memory_enabled?: boolean;
  model?: string;
  refresh_timeout_sec?: number;
}

const KOLIBRI_CLIENT_ID_KEY = "kolibri_client_id";
const JSON_HEADERS = { "Content-Type": "application/json" } as const;

function getAuthToken(): string {
  return useChatStore.getState().apiToken?.trim() || "";
}

function buildApiHeaders(headers?: HeadersInit): Headers {
  const next = new Headers(headers);
  const token = getAuthToken();
  if (token && !next.has("Authorization")) {
    next.set("Authorization", `Bearer ${token}`);
  }
  if (typeof window !== "undefined") {
    next.set("X-Kolibri-Client-Id", getStableClientId());
  }
  return next;
}

async function apiFetch(input: string, init: RequestInit = {}): Promise<Response> {
  const next: RequestInit = {
    ...init,
    headers: buildApiHeaders(init.headers),
  };
  return fetch(input, next);
}

function isExplicitWasmMode(): boolean {
  return (import.meta.env.VITE_KOLIBRI_RESPONSE_MODE ?? "backend").toLowerCase() === "wasm";
}

export function getStableClientId(): string {
  try {
    const existing = window.localStorage.getItem(KOLIBRI_CLIENT_ID_KEY);
    if (existing && existing.trim().length >= 8) return existing.trim();
    const next =
      (typeof crypto !== "undefined" && "randomUUID" in crypto
        ? crypto.randomUUID()
        : `client-${Date.now()}-${Math.random().toString(36).slice(2, 10)}`);
    window.localStorage.setItem(KOLIBRI_CLIENT_ID_KEY, next);
    return next;
  } catch {
    return `client-ephemeral-${Math.random().toString(36).slice(2, 10)}`;
  }
}

export async function fetchModelStatus(): Promise<ModelStatus> {
  try {
    const primary = await apiFetch("/api/v1/ai/models");
    if (primary.ok) {
      return (await primary.json()) as ModelStatus;
    }
  } catch {
    // fallback below
  }

  const fallback = await apiFetch("/api/v1/model/stats");
  if (!fallback.ok) throw new Error(`Не удалось загрузить модели: ${fallback.status}`);
  const data = (await fallback.json()) as { exists: boolean; path: string; size_mb: number; patterns: number; edges: number };
  const modelName = data.path?.split("/").pop() ?? "unknown";
  return {
    primary_model: modelName,
    model_available: data.exists,
    c_trainer_available: false,
    model_path: data.path ?? "",
    model_size_mb: data.size_mb ?? 0,
    patterns: data.patterns ?? 0,
    edges: data.edges ?? 0,
    documents: 0,
    epoch: 0,
    formula_generation: 0,
    embedding_vocab_size: 0,
    sentence_store_size: 0,
  };
}

export async function streamChat(
  payload: StreamRequest,
  onToken: (token: string) => void,
  signal?: AbortSignal,
): Promise<StreamChatResult> {
  const effectivePrompt = payload.prompt.trim();
  const profile =
    payload.model === "Колибри 4 • Тяжёлая"
      ? "deep"
      : payload.model === "Колибри 5 • Превью"
      ? "balanced"
      : "fast";
  const timeBudgetMs = profile === "deep" ? 22000 : profile === "balanced" ? 14000 : 9000;
  const stableClientId = getStableClientId();
  const clientId = payload.memoryEnabled
    ? stableClientId
    : `ephemeral:${stableClientId}:${payload.sessionId}`;
  const chatBody = {
    message: effectivePrompt,
    conversation_id: payload.sessionId,
    client_id: clientId,
    temperature: 0.7,
    profile,
    time_budget_ms: timeBudgetMs,
    persona: payload.persona,
    memory_enabled: payload.memoryEnabled,
    model: payload.model,
  };

  const requestFallback = async (): Promise<ChatApiResponse> => {
    const fallback = await apiFetch("/api/v1/ai/chat", {
      method: "POST",
      headers: JSON_HEADERS,
      body: JSON.stringify(chatBody),
      signal,
    });
    if (!fallback.ok) throw new Error(`Ошибка ответа модели: ${fallback.status}`);
    return (await fallback.json()) as ChatApiResponse;
  };

  const emitChars = (value: string) => {
    for (const char of value) onToken(char);
  };

  const fallbackWithSuffix = async (alreadyEmitted: string): Promise<StreamChatResult> => {
    const full = await requestFallback();
    if (!alreadyEmitted) {
      emitChars(full.response ?? "");
      return {
        conversationId: full.conversation_id ?? null,
        method: full.method,
        durationMs: full.duration_ms,
        knowledgeHits: full.knowledge_hits,
      };
    }
    if ((full.response ?? "").startsWith(alreadyEmitted)) {
      emitChars((full.response ?? "").slice(alreadyEmitted.length));
    }
    return {
      conversationId: full.conversation_id ?? null,
      method: full.method,
      durationMs: full.duration_ms,
      knowledgeHits: full.knowledge_hits,
    };
  };

  if (isExplicitWasmMode()) {
    try {
      const localAnswer = await kolibriBridge.ask(
        effectivePrompt,
        payload.persona,
        payload.context,
        !payload.memoryEnabled,
      );
      emitChars(localAnswer);
      return { conversationId: payload.sessionId };
    } catch {
      // fallback to backend stream below
    }
  }

  let response: Response;
  try {
    response = await apiFetch("/api/v1/ai/chat/stream", {
      method: "POST",
      headers: JSON_HEADERS,
      body: JSON.stringify(chatBody),
      signal,
    });
  } catch (error) {
    if (error instanceof DOMException && error.name === "AbortError") {
      throw error;
    }
    return await fallbackWithSuffix("");
  }

  if (!response.ok || !response.body) {
    return await fallbackWithSuffix("");
  }

  const reader = response.body.getReader();
  const decoder = new TextDecoder("utf-8");
  let buffer = "";
  let emitted = "";
  let finalResult: StreamChatResult = { conversationId: payload.sessionId };

  while (true) {
    let done = false;
    let value: Uint8Array | undefined;
    try {
      const readResult = await reader.read();
      done = readResult.done;
      value = readResult.value;
    } catch (error) {
      if (error instanceof DOMException && error.name === "AbortError") {
        throw error;
      }
      return await fallbackWithSuffix(emitted);
    }
    if (done) break;
    if (value) buffer += decoder.decode(value, { stream: true });

    const events = buffer.split("\n\n");
    buffer = events.pop() ?? "";

    for (const evt of events) {
      const lines = evt.split("\n");
      const eventLine = lines.find((line) => line.startsWith("event:"));
      const dataLine = lines.find((line) => line.startsWith("data:"));
      if (!eventLine || !dataLine) continue;

      const eventType = eventLine.replace("event:", "").trim();
      const raw = dataLine.replace("data:", "").trim();

      if (eventType === "error") {
        return await fallbackWithSuffix(emitted);
      }
      try {
        if (eventType === "done") {
          const parsed = JSON.parse(raw) as {
            conversation_id?: string;
            method?: string;
            duration_ms?: number;
            knowledge_hits?: number;
          };
          finalResult = {
            conversationId: parsed.conversation_id ?? finalResult.conversationId,
            method: parsed.method,
            durationMs: parsed.duration_ms,
            knowledgeHits: parsed.knowledge_hits,
          };
          continue;
        }
        if (eventType !== "token") continue;
        const parsed = JSON.parse(raw) as { text?: string };
        if (parsed.text) {
          onToken(parsed.text);
          emitted += parsed.text;
        }
      } catch {
        // noop: ignore malformed chunk
      }
    }
  }

  if (!emitted.length) {
    return await fallbackWithSuffix("");
  }
  return finalResult;
}

export async function imagineImage(payload: ImagineRequest): Promise<ImagineResponse> {
  const response = await apiFetch("/api/v1/ai/imagine", {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify(payload),
  });

  const body = await response.json().catch(() => ({} as { detail?: string }));
  if (!response.ok) {
    const detail = typeof body?.detail === "string" ? body.detail : `Ошибка генерации: ${response.status}`;
    throw new Error(detail);
  }

  return body as ImagineResponse;
}

export async function analyzeImageAttachment(file: File, prompt = "Опиши изображение и выдели главное по-русски."): Promise<VisionAnalyzeResponse> {
  try {
    return await analyzeImageLocally(file, prompt);
  } catch {
    // fallback to server-side analysis below
  }

  const formData = new FormData();
  formData.append("file", file);
  formData.append("prompt", prompt);

  const response = await apiFetch("/api/v1/ai/vision/analyze", {
    method: "POST",
    body: formData,
  });

  const body = await response.json().catch(() => ({} as { detail?: string }));
  if (!response.ok) {
    const detail = typeof body?.detail === "string" ? body.detail : `Ошибка анализа изображения: ${response.status}`;
    throw new Error(detail);
  }

  return body as VisionAnalyzeResponse;
}

export async function learnTextWithSwarmDemo(payload: LearnTextDemoRequest): Promise<LearnTextDemoResponse> {
  const response = await apiFetch("/api/v1/ai/demo/learn/text", {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify(payload),
  });

  const body = await response.json().catch(() => ({} as { detail?: string }));
  if (!response.ok) {
    const detail = typeof body?.detail === "string" ? body.detail : `Не удалось обучить рой: ${response.status}`;
    throw new Error(detail);
  }

  return body as LearnTextDemoResponse;
}

export async function fetchQualityBenchmarkHistory(limit = 20): Promise<QualityBenchmarkHistoryResponse> {
  const safeLimit = Math.max(1, Math.min(200, Math.floor(limit)));
  const response = await apiFetch(`/api/v1/ai/quality/benchmark/history?limit=${safeLimit}`);
  if (!response.ok) {
    throw new Error(`Не удалось загрузить историю качества: ${response.status}`);
  }
  return (await response.json()) as QualityBenchmarkHistoryResponse;
}

export async function fetchSwarmRuntimeStatus(): Promise<SwarmRuntimeStatusResponse> {
  const response = await apiFetch("/api/v1/swarm/runtime/status");
  if (!response.ok) {
    throw new Error(`Не удалось загрузить статус роя: ${response.status}`);
  }
  return (await response.json()) as SwarmRuntimeStatusResponse;
}

export async function startSwarmRuntime(): Promise<SwarmRuntimeStatusResponse> {
  const response = await apiFetch("/api/v1/swarm/runtime/start", { method: "POST" });
  if (!response.ok) {
    throw new Error(`Не удалось запустить рой: ${response.status}`);
  }
  return (await response.json()) as SwarmRuntimeStatusResponse;
}

export async function runSwarmComparison(): Promise<SwarmRuntimeStatusResponse> {
  const response = await apiFetch("/api/v1/swarm/runtime/refresh", { method: "POST" });
  if (!response.ok) {
    throw new Error(`Не удалось принудительно пересчитать рой: ${response.status}`);
  }
  return (await response.json()) as SwarmRuntimeStatusResponse;
}

export async function runSwarmComparisonIfIdle(): Promise<SwarmRuntimeStatusResponse> {
  const response = await apiFetch("/api/v1/swarm/runtime/run", { method: "POST" });
  if (!response.ok) {
    throw new Error(`Не удалось пересчитать сравнение роя: ${response.status}`);
  }
  return (await response.json()) as SwarmRuntimeStatusResponse;
}

export async function ingestSwarmText(payload: {
  text: string;
  title?: string;
  source?: string;
  category?: string;
}): Promise<SwarmRuntimeStatusResponse> {
  const response = await apiFetch("/api/v1/swarm/runtime/ingest/text", {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify(payload),
  });
  if (!response.ok) {
    const body = await response.json().catch(() => ({} as { detail?: string }));
    throw new Error(typeof body.detail === "string" ? body.detail : `Не удалось добавить текст: ${response.status}`);
  }
  return (await response.json()) as SwarmRuntimeStatusResponse;
}

export async function ingestSwarmUrl(payload: {
  url?: string;
  urls?: string[];
  crawl?: boolean;
  depth?: number;
  max_pages?: number;
  delay_sec?: number;
}): Promise<SwarmRuntimeStatusResponse> {
  const response = await apiFetch("/api/v1/swarm/runtime/ingest/url", {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify(payload),
  });
  if (!response.ok) {
    const body = await response.json().catch(() => ({} as { detail?: string }));
    throw new Error(typeof body.detail === "string" ? body.detail : `Не удалось добавить URL: ${response.status}`);
  }
  return (await response.json()) as SwarmRuntimeStatusResponse;
}

export async function exportSwarmKpack(payload: {
  package_id: string;
  title: string;
  language?: string;
  domains?: string[];
  description?: string;
  default_query?: string;
}): Promise<SwarmKpackExportResponse> {
  const response = await apiFetch("/api/v1/swarm/runtime/kpack/export", {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify(payload),
  });
  if (!response.ok) {
    const body = await response.json().catch(() => ({} as { detail?: string }));
    throw new Error(typeof body.detail === "string" ? body.detail : `Не удалось экспортировать .kpack: ${response.status}`);
  }
  return (await response.json()) as SwarmKpackExportResponse;
}

export async function downloadSwarmKpack(payload: Pick<SwarmKpackExportResponse, "download_url" | "filename">): Promise<void> {
  const response = await apiFetch(payload.download_url);
  if (!response.ok) {
    throw new Error(`Не удалось скачать .kpack: ${response.status}`);
  }
  const blob = await response.blob();
  const url = URL.createObjectURL(blob);
  try {
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = payload.filename;
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
  } finally {
    URL.revokeObjectURL(url);
  }
}

export async function importSwarmKpack(
  file: File,
  options?: { refresh?: boolean; refresh_timeout_sec?: number },
): Promise<SwarmRuntimeStatusResponse> {
  const form = new FormData();
  form.append("file", file);
  form.append("refresh", String(options?.refresh ?? true));
  form.append("refresh_timeout_sec", String(options?.refresh_timeout_sec ?? 180));

  const response = await apiFetch("/api/v1/swarm/runtime/kpack/import", {
    method: "POST",
    body: form,
  });
  if (!response.ok) {
    const body = await response.json().catch(() => ({} as { detail?: string }));
    throw new Error(typeof body.detail === "string" ? body.detail : `Не удалось импортировать .kpack: ${response.status}`);
  }
  return (await response.json()) as SwarmRuntimeStatusResponse;
}

export async function fetchAuthStatus(): Promise<AuthStatusResponse> {
  const response = await apiFetch("/api/v1/auth/status");
  if (!response.ok) {
    throw new Error(`Не удалось загрузить auth status: ${response.status}`);
  }
  return (await response.json()) as AuthStatusResponse;
}

export async function loginAccount(payload: { username: string; password: string }): Promise<{ access_token: string; role: string }> {
  const response = await apiFetch("/api/v1/auth/login", {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify(payload),
  });
  const body = await response.json().catch(() => ({} as { detail?: string; access_token?: string; role?: string }));
  if (!response.ok) {
    throw new Error(typeof body.detail === "string" ? body.detail : `Не удалось войти: ${response.status}`);
  }
  return {
    access_token: String(body.access_token || ""),
    role: String(body.role || "user"),
  };
}

export async function logoutAccount(): Promise<void> {
  await apiFetch("/api/v1/auth/logout", { method: "POST" }).catch(() => undefined);
}

export async function fetchAccountProfile(): Promise<AccountProfileResponse> {
  const response = await apiFetch(`/api/v1/account/profile?client_id=${encodeURIComponent(getStableClientId())}`);
  if (!response.ok) {
    throw new Error(`Не удалось загрузить профиль: ${response.status}`);
  }
  return (await response.json()) as AccountProfileResponse;
}

export async function updateAccountProfile(payload: {
  name?: string;
  facts?: string[];
}): Promise<AccountProfileResponse> {
  const response = await apiFetch(`/api/v1/account/profile?client_id=${encodeURIComponent(getStableClientId())}`, {
    method: "PUT",
    headers: JSON_HEADERS,
    body: JSON.stringify(payload),
  });
  const body = await response.json().catch(() => ({} as { detail?: string }));
  if (!response.ok) {
    throw new Error(typeof body.detail === "string" ? body.detail : `Не удалось обновить профиль: ${response.status}`);
  }
  return body as AccountProfileResponse;
}

export async function fetchAccountPreferences(): Promise<AccountPreferencesResponse> {
  const response = await apiFetch(`/api/v1/account/preferences?client_id=${encodeURIComponent(getStableClientId())}`);
  if (!response.ok) {
    throw new Error(`Не удалось загрузить настройки: ${response.status}`);
  }
  return (await response.json()) as AccountPreferencesResponse;
}

export async function updateAccountPreferences(payload: {
  theme?: "system" | "light" | "dark";
  persona?: "assistant" | "romantic" | "storyteller";
  memory_enabled?: boolean;
  model?: string;
}): Promise<AccountPreferencesResponse> {
  const response = await apiFetch(`/api/v1/account/preferences?client_id=${encodeURIComponent(getStableClientId())}`, {
    method: "PUT",
    headers: JSON_HEADERS,
    body: JSON.stringify(payload),
  });
  const body = await response.json().catch(() => ({} as { detail?: string }));
  if (!response.ok) {
    throw new Error(typeof body.detail === "string" ? body.detail : `Не удалось обновить настройки: ${response.status}`);
  }
  return body as AccountPreferencesResponse;
}

export async function fetchConversationSessions(limit = 100): Promise<ConversationListResponse> {
  const safeLimit = Math.max(1, Math.min(500, Math.floor(limit)));
  const response = await apiFetch(
    `/api/v1/ai/conversations?client_id=${encodeURIComponent(getStableClientId())}&limit=${safeLimit}`,
  );
  if (!response.ok) {
    throw new Error(`Не удалось загрузить диалоги: ${response.status}`);
  }
  return (await response.json()) as ConversationListResponse;
}

export async function fetchConversationTurns(conversationId: string, limit = 120): Promise<ConversationTurnsResponse> {
  const safeLimit = Math.max(1, Math.min(400, Math.floor(limit)));
  const response = await apiFetch(
    `/api/v1/ai/conversations/${encodeURIComponent(conversationId)}/turns?client_id=${encodeURIComponent(getStableClientId())}&limit=${safeLimit}`,
  );
  if (!response.ok) {
    throw new Error(`Не удалось загрузить историю чата: ${response.status}`);
  }
  return (await response.json()) as ConversationTurnsResponse;
}

export async function syncConversationSession(payload: {
  conversation_id: string;
  title?: string;
  pinned?: boolean;
}): Promise<ConversationSummary> {
  const response = await apiFetch(`/api/v1/ai/conversations?client_id=${encodeURIComponent(getStableClientId())}`, {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify(payload),
  });
  const body = await response.json().catch(() => ({} as { detail?: string }));
  if (!response.ok) {
    throw new Error(typeof body.detail === "string" ? body.detail : `Не удалось синхронизировать диалог: ${response.status}`);
  }
  return body as ConversationSummary;
}

export async function patchConversationSession(
  conversationId: string,
  payload: { title?: string; pinned?: boolean },
): Promise<ConversationSummary> {
  const response = await apiFetch(
    `/api/v1/ai/conversations/${encodeURIComponent(conversationId)}?client_id=${encodeURIComponent(getStableClientId())}`,
    {
      method: "PATCH",
      headers: JSON_HEADERS,
      body: JSON.stringify(payload),
    },
  );
  const body = await response.json().catch(() => ({} as { detail?: string }));
  if (!response.ok) {
    throw new Error(typeof body.detail === "string" ? body.detail : `Не удалось обновить диалог: ${response.status}`);
  }
  return body as ConversationSummary;
}

export async function deleteConversationSession(conversationId: string): Promise<void> {
  const response = await apiFetch(
    `/api/v1/ai/conversations/${encodeURIComponent(conversationId)}?client_id=${encodeURIComponent(getStableClientId())}`,
    { method: "DELETE" },
  );
  if (!response.ok && response.status !== 404) {
    throw new Error(`Не удалось удалить диалог: ${response.status}`);
  }
}

// ============================================================================
// Unified Knowledge Hub API
// ============================================================================

export interface KnowledgeGraphNode {
  id: string;
  label: string;
  frequency: number;
  fitness: number;
  domain: string;
}

export interface KnowledgeGraphEdge {
  source: string;
  target: string;
  weight: number;
}

export interface KnowledgeGraphData {
  nodes: KnowledgeGraphNode[];
  links: KnowledgeGraphEdge[];
}

export interface KnowledgeQueryRequest {
  query: string;
  top_k: number;
}

export interface KnowledgeQueryResponse {
  query: string;
  sources: Array<{
    name: string;
    score: number;
    content: string;
    metadata: Record<string, any>;
  }>;
  best_answer: string;
  confidence: number;
  total_sources: number;
  fusion_method: string;
  duration_ms: number;
}

export interface KnowledgeAnalyticsResponse {
  knowledge_graph: {
    patterns: number;
    edges: number;
    documents: number;
    tokens: number;
  };
  formula_pool: {
    size: number;
    generation: number;
    best_fitness: number;
  };
  embeddings: {
    vocab_size: number;
    trained_pairs: number;
    epochs: number;
  };
}

export async function queryKnowledgeHub(req: KnowledgeQueryRequest): Promise<KnowledgeQueryResponse> {
  const response = await apiFetch("/api/v1/ai/knowledge/query", {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify(req),
  });
  const body = await response.json().catch(() => ({} as { detail?: string }));
  if (!response.ok) {
    throw new Error(typeof body.detail === "string" ? body.detail : `Ошибка запроса к Knowledge Hub: ${response.status}`);
  }
  return body as KnowledgeQueryResponse;
}

export async function getKnowledgeAnalytics(): Promise<KnowledgeAnalyticsResponse> {
  const response = await apiFetch("/api/v1/ai/knowledge/analytics");
  if (!response.ok) {
    throw new Error(`Ошибка получения аналитики: ${response.status}`);
  }
  return (await response.json()) as KnowledgeAnalyticsResponse;
}

export async function getKnowledgeGraphData(domain?: string): Promise<KnowledgeGraphData> {
  // Получаем аналитику и конвертируем в формат графа
  const analytics = await getKnowledgeAnalytics();
  
  // Получаем данные графа из knowledge API
  const response = await apiFetch("/api/v1/ai/knowledge/analytics");
  if (!response.ok) {
    throw new Error(`Ошибка получения данных графа: ${response.status}`);
  }
  
  // Пока возвращаем заглушку на основе аналитики
  // В будущем бэкенд должен отдавать реальные узлы и рёбра
  const nodes: KnowledgeGraphNode[] = [];
  const links: KnowledgeGraphEdge[] = [];
  
  return { nodes, links };
}

// ============================================================================
// Continuous Learning API
// ============================================================================

export interface LearningStatusResponse {
  enabled: boolean;
  running: boolean;
  cycle_interval_sec: number;
  curriculum_level: number;
  tasks_registered: number;
  tasks: Array<{
    name: string;
    priority: string;
    run_count: number;
    total_time: number;
    error_count: number;
    last_error: string;
  }>;
  metrics: {
    total_cycles: number;
    total_tasks_executed: number;
    total_errors: number;
    total_uptime: number;
    corpus: {
      patterns: number;
      edges: number;
      documents: number;
      tokens: number;
    };
    formulas: {
      pool_size: number;
      best_fitness: number;
      evolution_count: number;
    };
    world_model: {
      loss: number;
      concepts: number;
      surprise: number;
    };
    embeddings: {
      vocab_size: number;
      loss: number;
    };
    dialogue: {
      processed: number;
      facts_extracted: number;
      knowledge_created: number;
    };
    curriculum: {
      level: number;
      source: string;
    };
  };
}

export async function getLearningStatus(): Promise<LearningStatusResponse> {
  const response = await apiFetch("/api/v1/learning/status");
  if (!response.ok) {
    throw new Error(`Ошибка получения статуса обучения: ${response.status}`);
  }
  return (await response.json()) as LearningStatusResponse;
}

export async function startLearning(): Promise<{ status: string; metrics: any }> {
  const response = await apiFetch("/api/v1/learning/start", { method: "POST" });
  if (!response.ok) {
    throw new Error(`Ошибка запуска обучения: ${response.status}`);
  }
  return response.json();
}

export async function stopLearning(): Promise<{ status: string; metrics: any }> {
  const response = await apiFetch("/api/v1/learning/stop", { method: "POST" });
  if (!response.ok) {
    throw new Error(`Ошибка остановки обучения: ${response.status}`);
  }
  return response.json();
}

export async function advanceCurriculum(): Promise<any> {
  const response = await apiFetch("/api/v1/learning/curriculum/advance", { method: "POST" });
  if (!response.ok) {
    throw new Error(`Ошибка повышения уровня: ${response.status}`);
  }
  return response.json();
}
