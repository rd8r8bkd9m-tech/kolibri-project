import { useQuery } from "@tanstack/react-query";
import { fetchModelStatus, fetchQualityBenchmarkHistory, fetchSwarmRuntimeStatus } from "@/api";

export const appQueryKeys = {
  modelStatus: ["model-status"] as const,
  qualityHistory: (limit: number) => ["quality-history", limit] as const,
  swarmStatus: ["swarm-status"] as const,
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
