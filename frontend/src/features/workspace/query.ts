import { useQuery } from "@tanstack/react-query";
import { fetchModelStatus, fetchQualityBenchmarkHistory, fetchSwarmRuntimeStatus } from "@/api";
import { fetchAnalytics, fetchLiveQueuePending, fetchLiveQueueStats } from "@/api/liveQueue";

export const appQueryKeys = {
  modelStatus: ["model-status"] as const,
  qualityHistory: (limit: number) => ["quality-history", limit] as const,
  swarmStatus: ["swarm-status"] as const,
  liveQueueRoot: ["live-queue"] as const,
  liveQueuePending: (limit: number) => ["live-queue", "pending", limit] as const,
  liveQueueStats: ["live-queue", "stats"] as const,
  liveQueueAnalytics: ["live-queue", "analytics"] as const,
};

export function useModelStatusQuery() {
  return useQuery({
    queryKey: appQueryKeys.modelStatus,
    queryFn: fetchModelStatus,
  });
}

export function useQualityHistoryQuery(limit = 12) {
  return useQuery({
    queryKey: appQueryKeys.qualityHistory(limit),
    queryFn: () => fetchQualityBenchmarkHistory(limit),
  });
}

export function useSwarmStatusQuery() {
  return useQuery({
    queryKey: appQueryKeys.swarmStatus,
    queryFn: fetchSwarmRuntimeStatus,
    refetchInterval: 60_000,
  });
}

export function useLiveQueuePendingQuery(limit = 100) {
  return useQuery({
    queryKey: appQueryKeys.liveQueuePending(limit),
    queryFn: () => fetchLiveQueuePending(limit),
    refetchInterval: 30_000,
  });
}

export function useLiveQueueStatsQuery() {
  return useQuery({
    queryKey: appQueryKeys.liveQueueStats,
    queryFn: fetchLiveQueueStats,
    refetchInterval: 30_000,
  });
}

export function useLiveQueueAnalyticsQuery(enabled = true) {
  return useQuery({
    queryKey: appQueryKeys.liveQueueAnalytics,
    queryFn: fetchAnalytics,
    enabled,
    refetchInterval: enabled ? 30_000 : false,
  });
}
