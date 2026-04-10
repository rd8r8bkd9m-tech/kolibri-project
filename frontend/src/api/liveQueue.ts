import { apiFetch, JSON_HEADERS } from "../api";
import type {
  LiveQueueAnalyticsResponse,
  LiveQueueBulkActionResponse,
  LiveQueueEditResponse,
  LiveQueueExportResponse,
  LiveQueueItem,
  LiveQueueListResponse,
  LiveQueueSearchResponse,
  LiveQueueStats,
} from "../types/liveQueue";

const API_BASE = "/api/v1";

function getErrorDetail(body: unknown, fallback: string): string {
  if (body && typeof body === "object" && "detail" in body) {
    const detail = (body as { detail?: unknown }).detail;
    if (typeof detail === "string" && detail.trim()) {
      return detail;
    }
  }
  return fallback;
}

async function readJson<T>(response: Response): Promise<T> {
  return response.json().catch(() => ({} as T));
}

export async function fetchLiveQueuePending(limit = 50): Promise<LiveQueueListResponse> {
  const response = await apiFetch(`${API_BASE}/live-queue/list`, {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify({ limit }),
  });

  if (!response.ok) {
    const body = await readJson<{ detail?: string }>(response);
    throw new Error(getErrorDetail(body, `Не удалось загрузить live queue: ${response.status}`));
  }

  return readJson<LiveQueueListResponse>(response);
}

export async function approveQuestion(id: number): Promise<LiveQueueEditResponse> {
  const response = await apiFetch(`${API_BASE}/live-queue/approve`, {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify({ id }),
  });

  if (!response.ok) {
    const body = await readJson<{ detail?: string }>(response);
    throw new Error(getErrorDetail(body, `Не удалось одобрить вопрос: ${response.status}`));
  }

  return readJson<LiveQueueEditResponse>(response);
}

export async function rejectQuestion(id: number): Promise<LiveQueueEditResponse> {
  const response = await apiFetch(`${API_BASE}/live-queue/reject`, {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify({ id }),
  });

  if (!response.ok) {
    const body = await readJson<{ detail?: string }>(response);
    throw new Error(getErrorDetail(body, `Не удалось отклонить вопрос: ${response.status}`));
  }

  return readJson<LiveQueueEditResponse>(response);
}

export async function editQuestion(id: number, answer: string): Promise<LiveQueueEditResponse> {
  const response = await apiFetch(`${API_BASE}/live-queue/edit`, {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify({ id, answer }),
  });

  if (!response.ok) {
    const body = await readJson<{ detail?: string }>(response);
    throw new Error(getErrorDetail(body, `Не удалось обновить вопрос: ${response.status}`));
  }

  return readJson<LiveQueueEditResponse>(response);
}

export async function fetchLiveQueueStats(): Promise<LiveQueueStats> {
  const response = await apiFetch(`${API_BASE}/live-queue/stats`);

  if (!response.ok) {
    const body = await readJson<{ detail?: string }>(response);
    throw new Error(getErrorDetail(body, `Не удалось загрузить статистику очереди: ${response.status}`));
  }

  return readJson<LiveQueueStats>(response);
}

export async function searchQuestions(
  query: string,
  status = "pending",
  limit = 50,
): Promise<LiveQueueSearchResponse> {
  const response = await apiFetch(`${API_BASE}/live-queue/search`, {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify({ query, status, limit }),
  });

  if (!response.ok) {
    const body = await readJson<{ detail?: string }>(response);
    throw new Error(getErrorDetail(body, `Не удалось выполнить поиск: ${response.status}`));
  }

  return readJson<LiveQueueSearchResponse>(response);
}

export async function fetchAnalytics(): Promise<LiveQueueAnalyticsResponse> {
  const response = await apiFetch(`${API_BASE}/live-queue/analytics`);

  if (!response.ok) {
    const body = await readJson<{ detail?: string }>(response);
    throw new Error(getErrorDetail(body, `Не удалось загрузить аналитику очереди: ${response.status}`));
  }

  return readJson<LiveQueueAnalyticsResponse>(response);
}

export async function bulkApproveQuestion(ids: number[]): Promise<LiveQueueBulkActionResponse> {
  const response = await apiFetch(`${API_BASE}/live-queue/bulk-approve`, {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify({ ids }),
  });

  if (!response.ok) {
    const body = await readJson<{ detail?: string }>(response);
    throw new Error(getErrorDetail(body, `Не удалось массово одобрить вопросы: ${response.status}`));
  }

  return readJson<LiveQueueBulkActionResponse>(response);
}

export async function bulkRejectQuestion(ids: number[]): Promise<LiveQueueBulkActionResponse> {
  const response = await apiFetch(`${API_BASE}/live-queue/bulk-reject`, {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify({ ids }),
  });

  if (!response.ok) {
    const body = await readJson<{ detail?: string }>(response);
    throw new Error(getErrorDetail(body, `Не удалось массово отклонить вопросы: ${response.status}`));
  }

  return readJson<LiveQueueBulkActionResponse>(response);
}

export async function triggerExport(): Promise<LiveQueueExportResponse> {
  const response = await apiFetch(`${API_BASE}/live-queue/export`, {
    method: "POST",
  });

  if (!response.ok) {
    const body = await readJson<{ detail?: string }>(response);
    throw new Error(getErrorDetail(body, `Не удалось запустить экспорт очереди: ${response.status}`));
  }

  return readJson<LiveQueueExportResponse>(response);
}
