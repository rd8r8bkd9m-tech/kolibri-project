import type {
  ChatApiResponse,
  ImagineRequest,
  ImagineResponse,
  LearnTextDemoResponse,
  ModelOption,
  ModelStatus,
  QualityBenchmarkHistoryResponse,
  SwarmKpackExportResponse,
  SwarmRuntimeStatusResponse,
} from "@/types";
import { kolibriBridge, type KolibriContextTurn } from "@/lib/kolibriBridge";
import { analyzeImageLocally } from "@/lib/localImageAnalysis";

export interface StreamRequest {
  prompt: string;
  model: ModelOption;
  sessionId: string;
  persona: "assistant" | "romantic" | "storyteller";
  memoryEnabled: boolean;
  context: KolibriContextTurn[];
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

function isWeatherPrompt(prompt: string): boolean {
  const normalized = prompt.trim().toLowerCase();
  if (!normalized) return false;
  return /(?:^|\s)(?:погода|погоде|погоду|погод|температур|ветер|осадк|дожд|снег)\b/u.test(normalized);
}

function looksLikeLocationOnlyPrompt(prompt: string): boolean {
  const normalized = prompt.trim().toLowerCase();
  if (!normalized || normalized.length > 40) return false;
  if (/[0-9]/u.test(normalized)) return false;
  const cleaned = normalized.replace(/[?!.,:;]/gu, " ").trim();

  const tokens = cleaned
    .split(/\s+/u)
    .map((token) => token.trim())
    .filter(Boolean);

  if (!tokens.length || tokens.length > 4) return false;

  const generic = new Set([
    "а",
    "и",
    "ну",
    "да",
    "еще",
    "ещё",
    "подробнее",
    "продолжай",
    "погода",
    "какая",
    "какой",
    "какие",
    "в",
    "во",
    "на",
  ]);

  const content = tokens.filter((token) => !generic.has(token));
  if (!content.length || content.length > 2) return false;
  return content.every((token) => token.length >= 3);
}

function extractLocationOnlyPrompt(prompt: string): string | null {
  if (!looksLikeLocationOnlyPrompt(prompt)) return null;
  const normalized = prompt
    .trim()
    .toLowerCase()
    .replace(/[?!.,:;]/gu, " ");
  const tokens = normalized
    .split(/\s+/u)
    .map((token) => token.trim())
    .filter(Boolean)
    .filter((token) => !new Set(["а", "и", "ну", "да", "в", "во", "на"]).has(token));
  if (!tokens.length) return null;
  return tokens.join(" ");
}

function buildEffectivePrompt(prompt: string, context: KolibriContextTurn[]): string {
  const trimmed = prompt.trim();
  if (!trimmed) return trimmed;

  if (isWeatherPrompt(trimmed)) {
    return trimmed;
  }

  const lastUserPrompt = context.length ? context[context.length - 1]?.prompt?.trim() ?? "" : "";
  const followupLocation = extractLocationOnlyPrompt(trimmed);
  if (lastUserPrompt && isWeatherPrompt(lastUserPrompt) && followupLocation) {
    return `какая погода в ${followupLocation}`;
  }

  return trimmed;
}

function preferBackendForPrompt(prompt: string, context: KolibriContextTurn[]): boolean {
  const normalized = prompt.trim().toLowerCase();
  if (!normalized) return true;

  if (isWeatherPrompt(normalized)) return true;
  if (buildEffectivePrompt(prompt, context) !== prompt.trim()) return true;

  const backendOnlyPatterns = [
    /^(?:сколько\s+будет|посчитай|вычисли)\b/u,
    /^[\d\s()+\-*/%.=^]+$/u,
    /^что\s+ты\s+знаешь\s+(?:о|об|про)\b/u,
    /^что\s+такое\b/u,
    /^кто\s+так(?:ой|ая|ие)\b/u,
    /^(?:объясни|поясни|обьясни)\b/u,
    /^расскажи(?:\s+подробно)?\s+(?:о|об|про)\b/u,
    /^что\s+изучает\b/u,
    /^чем\s+занимается\b/u,
    /^как\s+устроен(?:а|о)?\b/u,
    /^(?:почему\s+важ(?:ен|на|но)|зачем\s+нуж(?:ен|на|но))\b/u,
    /^как\s+меня\s+зовут\b/u,
    /^что\s+ты\s+знаешь\s+обо\s+мне\b/u,
    /^что\s+ты\s+помнишь\s+обо\s+мне\b/u,
    /^о\s+ч[её]м\s+мы\s+говорили\b/u,
    /^что\s+мы\s+обсуждали\b/u,
    /^что\s+было\s+до\s+этого\b/u,
    /^объясни\s+архитектур[ауы]\s+kolibri\b/u,
    /^объясни\s+архитектур[ауы]\s+колибри\b/u,
    /^ты\s+умеешь\b/u,
    /^ты\s+бог\b/u,
    /^ты\s+человек\b/u,
    /^(?:дебил|идиот|дурак|тупой)\b/u,
    /^а\s+подробнее\b/u,
    /^подробнее\b/u,
    /^продолжай\b/u,
    /^а\s+проще\b/u,
    /^проще\b/u,
    /^а\s+если\s+проще\b/u,
    /^короче\b/u,
    /^а\s+по\s+пунктам\b/u,
    /^по\s+пунктам\b/u,
    /^а\s+что\s+еще\b/u,
    /^а\s+что\s+ещё\b/u,
    /^что\s+еще\b/u,
    /^что\s+ещё\b/u,
    /^а\s+почему\b/u,
    /^почему\b/u,
    /^зачем\b/u,
    /^приведи\s+пример\b/u,
    /^пример\b/u,
    /^а\s+пример\b/u,
    /^сравни\b/u,
    /^(?:это\s+точно|точно|ты\s+уверен|правда)\b/u,
  ];

  return backendOnlyPatterns.some((pattern) => pattern.test(normalized));
}

function getStableClientId(): string {
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
    const primary = await fetch("/api/v1/ai/models");
    if (primary.ok) {
      return (await primary.json()) as ModelStatus;
    }
  } catch {
    // fallback below
  }

  const fallback = await fetch("/api/v1/model/stats");
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
) {
  const effectivePrompt = buildEffectivePrompt(payload.prompt, payload.context);
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

  const requestFallback = async (): Promise<string> => {
    const fallback = await fetch("/api/v1/ai/chat", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(chatBody),
      signal,
    });
    if (!fallback.ok) throw new Error(`Ошибка ответа модели: ${fallback.status}`);
    const data = (await fallback.json()) as { response?: string };
    return data.response ?? "";
  };

  const emitChars = (value: string) => {
    for (const char of value) onToken(char);
  };

  const fallbackWithSuffix = async (alreadyEmitted: string) => {
    const full = await requestFallback();
    if (!alreadyEmitted) {
      emitChars(full);
      return;
    }
    if (full.startsWith(alreadyEmitted)) {
      emitChars(full.slice(alreadyEmitted.length));
    }
  };

  if (
    (import.meta.env.VITE_KOLIBRI_RESPONSE_MODE ?? "script").toLowerCase() !== "llm" &&
    !preferBackendForPrompt(payload.prompt, payload.context)
  ) {
    try {
      const localAnswer = await kolibriBridge.ask(
        effectivePrompt,
        payload.persona,
        payload.context,
        !payload.memoryEnabled,
      );
      emitChars(localAnswer);
      return;
    } catch {
      // fallback to backend stream below
    }
  }

  let response: Response;
  try {
    response = await fetch("/api/v1/ai/chat/stream", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(chatBody),
      signal,
    });
  } catch {
    await fallbackWithSuffix("");
    return;
  }

  if (!response.ok || !response.body) {
    await fallbackWithSuffix("");
    return;
  }

  const reader = response.body.getReader();
  const decoder = new TextDecoder("utf-8");
  let buffer = "";
  let emitted = "";

  while (true) {
    let done = false;
    let value: Uint8Array | undefined;
    try {
      const readResult = await reader.read();
      done = readResult.done;
      value = readResult.value;
    } catch {
      await fallbackWithSuffix(emitted);
      return;
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
        await fallbackWithSuffix(emitted);
        return;
      }
      if (eventType !== "token") continue;
      try {
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
    await fallbackWithSuffix("");
  }
}

export async function imagineImage(payload: ImagineRequest): Promise<ImagineResponse> {
  const response = await fetch("/api/v1/ai/imagine", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
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

  const response = await fetch("/api/v1/ai/vision/analyze", {
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
  const response = await fetch("/api/v1/ai/demo/learn/text", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
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
  const response = await fetch(`/api/v1/ai/quality/benchmark/history?limit=${safeLimit}`);
  if (!response.ok) {
    throw new Error(`Не удалось загрузить историю качества: ${response.status}`);
  }
  return (await response.json()) as QualityBenchmarkHistoryResponse;
}

export async function fetchSwarmRuntimeStatus(): Promise<SwarmRuntimeStatusResponse> {
  const response = await fetch("/api/v1/swarm/runtime/status");
  if (!response.ok) {
    throw new Error(`Не удалось загрузить статус роя: ${response.status}`);
  }
  return (await response.json()) as SwarmRuntimeStatusResponse;
}

export async function startSwarmRuntime(): Promise<SwarmRuntimeStatusResponse> {
  const response = await fetch("/api/v1/swarm/runtime/start", { method: "POST" });
  if (!response.ok) {
    throw new Error(`Не удалось запустить рой: ${response.status}`);
  }
  return (await response.json()) as SwarmRuntimeStatusResponse;
}

export async function runSwarmComparison(): Promise<SwarmRuntimeStatusResponse> {
  const response = await fetch("/api/v1/swarm/runtime/refresh", { method: "POST" });
  if (!response.ok) {
    throw new Error(`Не удалось принудительно пересчитать рой: ${response.status}`);
  }
  return (await response.json()) as SwarmRuntimeStatusResponse;
}

export async function runSwarmComparisonIfIdle(): Promise<SwarmRuntimeStatusResponse> {
  const response = await fetch("/api/v1/swarm/runtime/run", { method: "POST" });
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
  const response = await fetch("/api/v1/swarm/runtime/ingest/text", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
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
  const response = await fetch("/api/v1/swarm/runtime/ingest/url", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
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
  const response = await fetch("/api/v1/swarm/runtime/kpack/export", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  if (!response.ok) {
    const body = await response.json().catch(() => ({} as { detail?: string }));
    throw new Error(typeof body.detail === "string" ? body.detail : `Не удалось экспортировать .kpack: ${response.status}`);
  }
  return (await response.json()) as SwarmKpackExportResponse;
}

export async function downloadSwarmKpack(payload: Pick<SwarmKpackExportResponse, "download_url" | "filename">): Promise<void> {
  const response = await fetch(payload.download_url);
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

  const response = await fetch("/api/v1/swarm/runtime/kpack/import", {
    method: "POST",
    body: form,
  });
  if (!response.ok) {
    const body = await response.json().catch(() => ({} as { detail?: string }));
    throw new Error(typeof body.detail === "string" ? body.detail : `Не удалось импортировать .kpack: ${response.status}`);
  }
  return (await response.json()) as SwarmRuntimeStatusResponse;
}
