import { CompositionResult } from "../api";

export interface Message {
  id: string;
  role: "user" | "assistant";
  content: string;
  time: string;
  meta?: {
    confidence: number;
    method: string;
    duration_ms: number;
    knowledge_hits?: number;
    sources?: string[];
    source?: "local" | "server";
    thinking?: string;
    cognitive?: Record<string, any>;
    self_check?: {
      passed: boolean;
      reason: string;
      logic_score: number;
      hallucination_risk: number;
      verified_by?: string;
    };
    composition?: CompositionResult;
  };
}
