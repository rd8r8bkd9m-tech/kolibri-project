import { consumeSseBuffer, extractStreamText } from "./lib/streamProtocol.js";

export const API_BASE_URL = "";

export interface ChatRequest {
  message: string;
  conversation_id?: string;
  client_id?: string;
  temperature?: number;
  profile?: "fast" | "balanced" | "deep";
  time_budget_ms?: number;
  persona?: "assistant" | "romantic" | "storyteller";
  memory_enabled?: boolean;
  model?: string;
}

export interface ChatResponse {
  response: string;
  confidence: number;
  conversation_id: string;
  sources: string[];
  knowledge_hits: number;
  method: string;
  duration_ms: number;
  model_available: boolean;
  formula_data?: Record<string, unknown>;
  graph_stats?: Record<string, unknown>;
  cognitive?: Record<string, unknown>;
  self_check?: {
    passed: boolean;
    reason: string;
    logic_score: number;
    hallucination_risk: number;
    verified_by?: string;
  };
  thinking?: string;
  client_id?: string;
}

export interface Conversation {
  id: string;
  title: string;
  last_message_at: string;
  preview: string;
}

export interface ConversationDetail {
  id: string;
  messages: Array<{
    role: "user" | "assistant";
    content: string;
    timestamp: string;
    composition?: CompositionResult;
  }>;
}

export interface CompositionResult {
  fragments: Array<{
    question: string;
    answer: string;
    relevance: number;
  }>;
  composed_text: string;
}

export interface QualityBenchmarkPoint {
  id: string;
  category: string;
  question: string;
  expected?: string;
  passed: boolean;
  score: number;
  reason?: string;
  duration_ms: number;
}

export interface QualityBenchmarkResponse {
  run_id: string;
  trigger: string;
  started_at: number;
  finished_at: number;
  duration_ms: number;
  score: number;
  passed: number;
  total: number;
  pass_rate: number;
  latency_p50_ms: number;
  latency_p95_ms: number;
  placeholder_rate: number;
  hallucination_proxy_rate: number;
  gates?: {
    overall_pass: boolean;
    [key: string]: any;
  };
  categories: Array<{
    category: string;
    passed: number;
    total: number;
    pass_rate: number;
    score_avg: number;
  }>;
  details: QualityBenchmarkPoint[];
}

export interface QualityBenchmarkHistoryPoint {
  run_id: string;
  trigger: string;
  started_at: number;
  finished_at: number;
  duration_ms: number;
  score: number;
  passed: number;
  total: number;
  pass_rate: number;
  latency_p50_ms: number;
  latency_p95_ms: number;
  gates_overall_pass: boolean;
}

export interface QualityBenchmarkHistoryResponse {
  limit: number;
  count: number;
  items: QualityBenchmarkHistoryPoint[];
  trend: {
    score_avg: number;
    score_delta: number;
    latency_p95_ms_avg: number;
    latency_p95_ms_delta: number;
    gate_failures: number;
    category_pass_rate_avg: Record<string, number>;
  };
}

const JSON_HEADERS = {
  "Content-Type": "application/json",
};

function buildUrl(path: string): string {
  return `${API_BASE_URL}${path}`;
}

async function readHttpError(response: Response): Promise<string> {
  const text = await response.text();
  const detail = text.trim();

  if (response.status === 500 && !detail) {
    return "Backend недоступен через Vite proxy. Проверьте, что C-core запущен на http://127.0.0.1:8001.";
  }

  return detail
    ? `Сервер вернул ${response.status}: ${response.statusText}. ${detail}`
    : `Сервер вернул ${response.status}: ${response.statusText}.`;
}

export async function getConversations(): Promise<Conversation[]> {
  const response = await fetch(buildUrl("/api/v1/conversations"), {
    method: "GET",
    headers: JSON_HEADERS,
  });
  if (!response.ok) throw new Error(await readHttpError(response));
  return response.json();
}

export async function getConversation(id: string): Promise<ConversationDetail> {
  const response = await fetch(buildUrl(`/api/v1/conversations/${id}`), {
    method: "GET",
    headers: JSON_HEADERS,
  });
  if (!response.ok) throw new Error(await readHttpError(response));
  return response.json();
}

export async function sendChatMessage(
  payload: ChatRequest,
  signal?: AbortSignal,
): Promise<ChatResponse> {
  const response = await fetch(buildUrl("/api/v1/ai/chat"), {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify(payload),
    signal,
  });

  if (!response.ok) {
    throw new Error(await readHttpError(response));
  }

  return response.json();
}

export async function runQualityBenchmark(): Promise<QualityBenchmarkResponse> {
  const response = await fetch(buildUrl("/api/v1/ai/quality/benchmark/run"), {
    method: "POST",
    headers: JSON_HEADERS,
  });

  if (!response.ok) {
    throw new Error(await readHttpError(response));
  }

  return response.json();
}

export async function getQualityBenchmarkLatest(): Promise<QualityBenchmarkResponse> {
  const response = await fetch(buildUrl("/api/v1/ai/quality/benchmark"), {
    method: "GET",
    headers: JSON_HEADERS,
  });

  if (!response.ok) {
    throw new Error(await readHttpError(response));
  }

  return response.json();
}

export async function getQualityBenchmarkHistory(limit = 30): Promise<QualityBenchmarkHistoryResponse> {
  const response = await fetch(buildUrl(`/api/v1/ai/quality/benchmark/history?limit=${limit}`), {
    method: "GET",
    headers: JSON_HEADERS,
  });

  if (!response.ok) {
    throw new Error(await readHttpError(response));
  }

  return response.json();
}

export interface SwarmStatusResponse {
  active: boolean;
  nodes_active?: number;
  total_nodes?: number;
  uptime_s?: number;
  memory_usage_mb?: number;
  last_sync?: string;
  best_fitness?: number;
  generation?: number;
  [key: string]: any;
}

export async function getSwarmStatus(): Promise<SwarmStatusResponse> {
  const response = await fetch(buildUrl("/api/v1/swarm/runtime/status"), {
    method: "GET",
    headers: JSON_HEADERS,
  });

  if (!response.ok) {
    throw new Error(await readHttpError(response));
  }

  return response.json();
}

export async function startSwarm(): Promise<{ status: string }> {
  const response = await fetch(buildUrl("/api/v1/swarm/runtime/start"), {
    method: "POST",
    headers: JSON_HEADERS,
  });

  if (!response.ok) {
    throw new Error(await readHttpError(response));
  }

  return response.json();
}

export async function sendFeedback(messageId: string, type: 'up' | 'down'): Promise<void> {
  const response = await fetch(buildUrl("/api/v1/ai/feedback"), {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify({ messageId, type }),
  });
  if (!response.ok) throw new Error(await readHttpError(response));
}

export async function sendChatMessageStream(
  payload: ChatRequest,
  onToken?: (token: string) => void,
  onThinking?: (thinking: string) => void,
  onDone?: (data: Partial<ChatResponse>) => void,
  onError?: (error: string) => void,
  signal?: AbortSignal,
): Promise<void> {
  const response = await fetch(buildUrl("/api/v1/ai/chat/stream"), {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify(payload),
    signal,
  });

  if (!response.ok) {
    throw new Error(await readHttpError(response));
  }

  const reader = response.body?.getReader();
  const decoder = new TextDecoder();

  if (!reader) {
    throw new Error("No response body");
  }

  let buffer = "";

  while (true) {
    const { done, value } = await reader.read();
    if (done) break;

    buffer += decoder.decode(value, { stream: true });
    const parsed = consumeSseBuffer(buffer);
    buffer = parsed.remainder;

    for (const event of parsed.events) {
      if ((event.eventType === "token" || event.eventType === "message") && onToken) {
        onToken(extractStreamText(event.payload));
      } else if (event.eventType === "thinking" && onThinking) {
        const thinking =
          event.payload &&
          typeof event.payload === "object" &&
          "step" in event.payload &&
          typeof event.payload.step === "string"
            ? event.payload.step
            : extractStreamText(event.payload);
        onThinking(thinking);
      } else if (event.eventType === "done" && onDone) {
        onDone(event.payload as Partial<ChatResponse>);
      } else if (event.eventType === "error") {
        const error =
          event.payload &&
          typeof event.payload === "object" &&
          "error" in event.payload &&
          typeof event.payload.error === "string"
            ? event.payload.error
            : extractStreamText(event.payload) || "Streaming request failed";
        onError?.(error);
      }
    }
  }

  buffer += decoder.decode();
  if (buffer.trim()) {
    const parsed = consumeSseBuffer(`${buffer}\n\n`);
    for (const event of parsed.events) {
      if ((event.eventType === "token" || event.eventType === "message") && onToken) {
        onToken(extractStreamText(event.payload));
      } else if (event.eventType === "done" && onDone) {
        onDone(event.payload as Partial<ChatResponse>);
      }
    }
  }
}
