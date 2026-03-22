import type { QualityBenchmarkHistoryPoint, QualityBenchmarkHistoryResponse } from "@/types";

export type QualityState = "loading" | "ok" | "risk" | "unknown";

export interface QualityOverview {
  state: QualityState;
  label: string;
  badgeClass: string;
  scoreAvg: number;
  p95Avg: number;
  gateFailures: number;
  latest: QualityBenchmarkHistoryPoint | null;
  recentRuns: QualityBenchmarkHistoryPoint[];
}

function isHealthy(score: number, gateFailures: number, p95: number): boolean {
  return score >= 0.85 && gateFailures === 0 && (p95 <= 3200 || p95 <= 0);
}

export function getQualityOverview(
  history: QualityBenchmarkHistoryResponse | null,
  loading = false,
  error = "",
): QualityOverview {
  const scoreAvg = Number(history?.trend?.score_avg ?? 0);
  const p95Avg = Number(history?.trend?.latency_p95_ms_avg ?? 0);
  const gateFailures = Number(history?.trend?.gate_failures ?? 0);
  const latest = history?.items?.[0] ?? null;
  const recentRuns = history?.items?.slice(0, 4) ?? [];

  const latestHealthy = latest
    ? isHealthy(Number(latest.score ?? 0), latest.gates_overall_pass ? 0 : 1, Number(latest.latency_p95_ms ?? 0))
    : false;
  const averageHealthy = isHealthy(scoreAvg, gateFailures, p95Avg);

  let state: QualityState = "unknown";
  if (loading && !history) {
    state = "loading";
  } else if (history?.count) {
    state = averageHealthy && latestHealthy ? "ok" : "risk";
  } else if (error) {
    state = "unknown";
  }

  return {
    state,
    label:
      state === "ok"
        ? "Стабильно"
        : state === "risk"
        ? "Нужен контроль"
        : state === "loading"
        ? "Проверяю"
        : "Нет eval",
    badgeClass:
      state === "ok"
        ? "border-emerald-400/40 bg-emerald-500/15 text-emerald-200"
        : state === "risk"
        ? "border-amber-400/40 bg-amber-500/15 text-amber-100"
        : "border-border/25 bg-card/60 text-muted",
    scoreAvg,
    p95Avg,
    gateFailures,
    latest,
    recentRuns,
  };
}

export function formatQualityScore(value: number | undefined): string {
  const numeric = Number(value ?? 0);
  if (!Number.isFinite(numeric)) return "—";
  return numeric.toFixed(3);
}

export function formatQualityMs(value: number | undefined): string {
  const numeric = Number(value ?? 0);
  if (!Number.isFinite(numeric) || numeric <= 0) return "—";
  return `${Math.round(numeric)} мс`;
}

export function formatQualityRunTime(value: number | undefined): string {
  const numeric = Number(value ?? 0);
  if (!Number.isFinite(numeric) || numeric <= 0) return "—";
  return new Date(numeric * 1000).toLocaleString("ru-RU", {
    day: "2-digit",
    month: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
  });
}
