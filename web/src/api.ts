export const API_BASE_URL = import.meta.env.VITE_API_BASE_URL ?? "";

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
  self_check?: Record<string, unknown>;
  client_id?: string;
}

const JSON_HEADERS = {
  "Content-Type": "application/json",
};

function buildUrl(path: string): string {
  return `${API_BASE_URL}${path}`;
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
    const text = await response.text();
    throw new Error(`Сервер вернул ${response.status}: ${response.statusText}. ${text}`);
  }

  return response.json();
}

export async function sendChatMessageStream(
  payload: ChatRequest,
  onToken?: (token: string) => void,
  onThinking?: (thinking: string) => void,
  onDone?: (data: any) => void,
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
    const text = await response.text();
    throw new Error(`Сервер вернул ${response.status}: ${response.statusText}. ${text}`);
  }

  const reader = response.body?.getReader();
  const decoder = new TextDecoder();

  if (!reader) {
    throw new Error("No response body");
  }

  let buffer = "";
  let eventType = "";

  while (true) {
    const { done, value } = await reader.read();
    if (done) break;

    buffer += decoder.decode(value, { stream: true });
    const lines = buffer.split("\n");
    buffer = lines.pop() || "";

    for (const line of lines) {
      if (line.startsWith("event: ")) {
        eventType = line.slice(7);
      } else if (line.startsWith("data: ")) {
        const data = line.slice(6);
        try {
          const parsed = JSON.parse(data);
          if (eventType === "token" && onToken) {
            onToken(parsed.text);
          } else if (eventType === "thinking" && onThinking) {
            onThinking(parsed.step);
          } else if (eventType === "done" && onDone) {
            onDone(parsed);
          } else if (eventType === "error" && onError) {
            onError(parsed.error);
          }
        } catch (e) {
          // Ignore invalid JSON
        }
      }
    }
  }
}
