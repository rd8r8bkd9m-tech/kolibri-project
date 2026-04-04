export type ModelOption = "Колибри 4.1 • Быстрая" | "Колибри 4 • Тяжёлая" | "Колибри 5 • Превью";
export type PrimarySurface = "thread" | "chats";
export type ComposerAction = "attach" | "voice" | "teach" | "imagine" | "pack";
export type WorkspaceSurface = "swarm" | "packs" | "teach" | "quality" | "knowledge" | "learning";

export type MessageRole = "user" | "assistant";

export interface ChatMessage {
  id: string;
  role: MessageRole;
  content: string;
  createdAt: number;
  editedAt?: number;
  streaming?: boolean;
  imageUrl?: string;
}

export interface ChatSession {
  id: string;
  title: string;
  pinned?: boolean;
  customTitle?: boolean;
  updatedAt: number;
}

export interface AuthStatusResponse {
  auth_enabled: boolean;
  authenticated: boolean;
  user: string | null;
  role: string | null;
  account_id: string | null;
}

export interface AccountProfileResponse {
  account_id: string;
  authenticated: boolean;
  user: string | null;
  role: string | null;
  name: string;
  facts: string[];
  documents_count: number;
  updated_at: number;
}

export interface AccountPreferencesResponse {
  account_id: string;
  authenticated: boolean;
  user: string | null;
  role: string | null;
  theme: "system" | "light" | "dark";
  persona: "assistant" | "romantic" | "storyteller";
  memory_enabled: boolean;
  model: string | null;
  updated_at: number;
}

export interface ConversationSummary {
  conversation_id: string;
  title: string;
  pinned: boolean;
  created_at: number;
  updated_at: number;
}

export interface ConversationListResponse {
  account_id: string;
  items: ConversationSummary[];
}

export interface ConversationTurnItem {
  role: MessageRole | "system";
  content: string;
  created_at: number;
}

export interface ConversationTurnsResponse {
  account_id: string;
  conversation_id: string;
  items: ConversationTurnItem[];
}

export interface ModelStatus {
  primary_model: string;
  model_available: boolean;
  c_trainer_available: boolean;
  model_path: string;
  model_size_mb: number;
  patterns: number;
  edges: number;
  documents: number;
  epoch: number;
  formula_generation: number;
  embedding_vocab_size: number;
  sentence_store_size: number;
}

export interface ImagineRequest {
  prompt: string;
  style?: string;
  aspect?: "1:1" | "9:16" | "16:9";
  model?: string;
  quality?: "low" | "medium" | "high" | "auto";
}

export interface ImagineResponse {
  image_url: string;
  revised_prompt?: string | null;
  provider: string;
  model: string;
  duration_ms: number;
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
  weighted_passed: number;
  weighted_total: number;
  pass_rate: number;
  latency_p50_ms: number;
  latency_p95_ms: number;
  placeholder_rate: number;
  timeout_rate: number;
  error_rate: number;
  hallucination_proxy_rate: number;
  gates_overall_pass: boolean;
}

export interface QualityBenchmarkHistoryResponse {
  limit: number;
  count: number;
  items: QualityBenchmarkHistoryPoint[];
  trend: {
    score_avg: number;
    latency_p95_ms_avg: number;
    gate_failures: number;
    score_delta: number;
    latency_p95_ms_delta: number;
    category_pass_rate_avg?: Record<string, number>;
    category_weighted_pass_rate_avg?: Record<string, number>;
  };
}

export interface ChatApiResponse {
  response: string;
  confidence: number;
  conversation_id: string;
  sources: string[];
  knowledge_hits: number;
  method: string;
  duration_ms: number;
  model_available: boolean;
  formula_data?: Record<string, unknown> | null;
  graph_stats?: Record<string, unknown> | null;
  cognitive?: Record<string, unknown> | null;
  self_check?: Record<string, unknown> | null;
  client_id?: string | null;
}

export interface SwarmRoundPoint {
  round: number;
  avg_exact: number;
  avg_mae: number;
  best_exact: number;
  imports_total: number;
}

export interface SwarmLatestReport {
  timestamp: number;
  node_count: number;
  rounds: number;
  gens_per_round: number;
  single: {
    exact: number;
    mae: number;
    fitness: number;
  };
  isolated_final: {
    avg_exact: number;
    avg_mae: number;
    best_exact: number;
    imports_total: number;
  };
  swarm_final: {
    avg_exact: number;
    avg_mae: number;
    best_exact: number;
    imports_total: number;
  };
  comparison: {
    avg_exact_delta: number;
    avg_mae_delta: number;
    mae_improvement_x: number;
  };
  isolated_rounds: SwarmRoundPoint[];
  swarm_rounds: SwarmRoundPoint[];
}

export interface SwarmComparisonTarget {
  node_count: number;
  label: string;
  available: boolean;
  consensus_score?: number;
}

export interface SwarmNodeStatus {
  node_id: number;
  name: string;
  role: "anchor" | "learner" | "validator" | string;
  active: boolean;
  healthy: boolean;
  weight: number;
  state: string;
  last_activity_at: number;
}

export interface SwarmTopologyStatus {
  target_node_count: number;
  anchor_node_count: number;
  learner_node_count: number;
  validator_node_count: number;
  active_node_count: number;
  healthy_node_count: number;
  validator_quorum: number;
  validator_active_count: number;
  validator_disagreement_count: number;
  consensus_score: number;
  last_propagation_at?: number;
  comparison_targets?: SwarmComparisonTarget[];
  nodes?: SwarmNodeStatus[];
  network_available?: boolean;
}

export interface SwarmRuntimeStatusResponse {
  binary_available: boolean;
  binary_path: string;
  knowledge_binary_available?: boolean;
  knowledge_binary_path?: string;
  running: boolean;
  pid: number | null;
  knowledge_running?: boolean;
  knowledge_pid?: number | null;
  interval_sec: number;
  status_path: string;
  knowledge_status_path?: string;
  live_memory_path?: string;
  seed_memory_path?: string;
  live_memory_document_count?: number;
  live_memory_domains?: Array<{ domain: string; documents: number }>;
  last_ingest_at?: number;
  last_ingest_kind?: string;
  last_ingest_domain_delta?: Array<{ domain: string; before: number; after: number; delta: number }>;
  last_knowledge_refresh_delta?: {
    from_timestamp: number;
    to_timestamp: number;
    documents_delta: number;
    single_hit_delta: number;
    isolated_hit_delta: number;
    swarm_hit_delta: number;
    swarm_vs_single_delta_change: number;
    swarm_vs_isolated_delta_change: number;
  } | null;
  latest_demo?: {
    created_at: number;
    title: string;
    source: string;
    category: string;
    saved_documents: number;
    message: string;
    domain_delta?: Array<{ domain: string; before: number; after: number; delta: number }>;
    knowledge_delta?: {
      from_timestamp: number;
      to_timestamp: number;
      documents_delta: number;
      single_hit_delta: number;
      isolated_hit_delta: number;
      swarm_hit_delta: number;
      swarm_vs_single_delta_change: number;
      swarm_vs_isolated_delta_change: number;
    } | null;
    comparison_summary?: {
      documents_before: number;
      documents_after: number;
      documents_delta: number;
      single_hit_before: number;
      single_hit_after: number;
      isolated_hit_before: number;
      isolated_hit_after: number;
      swarm_hit_before: number;
      swarm_hit_after: number;
      swarm_vs_single_before: number;
      swarm_vs_single_after: number;
      swarm_vs_isolated_before: number;
      swarm_vs_isolated_after: number;
      focus_domain?: string;
      focus_domain_documents_delta?: number;
      focus_domain_single_hit_delta?: number;
      focus_domain_swarm_hit_delta?: number;
      focus_domain_advantage_delta?: number;
    } | null;
  } | null;
  refresh_running?: boolean;
  refresh_pending?: boolean;
  last_refresh_started_at?: number;
  last_refresh_finished_at?: number;
  last_refresh_reason?: string;
  last_error?: string;
  swarm_topology?: SwarmTopologyStatus | null;
  swarm_nodes?: SwarmNodeStatus[];
  latest: SwarmLatestReport | null;
  latest_knowledge?: {
    timestamp: number;
    node_count: number;
    rounds: number;
    total_documents: number;
    single: { hit_ratio: number };
    isolated_final: { hit_ratio: number; best_hit_ratio: number; imported_total: number };
    swarm_final: { hit_ratio: number; best_hit_ratio: number; imported_total: number };
    comparison: { swarm_vs_single_delta: number; swarm_vs_isolated_delta: number };
    domain_scores?: Array<{
      domain: string;
      documents: number;
      single_hit_ratio: number;
      isolated_hit_ratio: number;
      swarm_hit_ratio: number;
      swarm_vs_single_delta: number;
      swarm_vs_isolated_delta: number;
    }>;
    isolated_rounds: Array<{ round: number; hit_ratio: number; best_hit_ratio: number; imported_total: number }>;
    swarm_rounds: Array<{ round: number; hit_ratio: number; best_hit_ratio: number; imported_total: number }>;
  } | null;
  ingest?: {
    kind: "text" | "url";
    saved_documents: number;
    saved_paths?: string[];
    domain_delta?: Array<{ domain: string; before: number; after: number; delta: number }>;
    message: string;
    stdout?: string;
    stderr?: string;
  } | null;
  import?: {
    pack_path: string;
    manifest: {
      format: string;
      version: number;
      id: string;
      title: string;
      language: string;
      domains: string[];
    };
    imported_documents: number;
    skipped_documents: number;
    copied_paths: string[];
    live_memory_path: string;
    live_memory_document_count_before: number;
    live_memory_document_count_after: number;
    live_memory_document_delta: number;
    domain_delta?: Array<{ domain: string; before: number; after: number; delta: number }>;
    import_log_path: string;
  } | null;
}

export interface SwarmKpackExportResponse {
  path: string;
  filename: string;
  package_id: string;
  title: string;
  language: string;
  domains: string[];
  documents: number;
  download_url: string;
  manifest: {
    format: string;
    version: number;
    id: string;
    title: string;
    language: string;
    domains: string[];
    description: string;
    entrypoints: {
      default_query: string;
    };
    artifacts: {
      knowledge_dir: string;
      formula_index: string | null;
      provenance: string;
    };
    exported_at: string;
    source_kind: string;
  };
}

export interface LearnTextDemoResponse {
  report: string;
  chat: ChatApiResponse;
  demo: {
    kind: "text";
    saved_documents: number;
    saved_paths?: string[];
    domain_delta?: Array<{ domain: string; before: number; after: number; delta: number }>;
    knowledge_delta?: {
      from_timestamp: number;
      to_timestamp: number;
      documents_delta: number;
      single_hit_delta: number;
      isolated_hit_delta: number;
      swarm_hit_delta: number;
      swarm_vs_single_delta_change: number;
      swarm_vs_isolated_delta_change: number;
    } | null;
    domain_score_delta?: Array<{
      domain: string;
      documents_before: number;
      documents_after: number;
      documents_delta: number;
      single_hit_delta: number;
      isolated_hit_delta: number;
      swarm_hit_delta: number;
      swarm_vs_single_delta_change: number;
      swarm_vs_isolated_delta_change: number;
    }>;
    comparison_summary?: {
      documents_before: number;
      documents_after: number;
      documents_delta: number;
      single_hit_before: number;
      single_hit_after: number;
      isolated_hit_before: number;
      isolated_hit_after: number;
      swarm_hit_before: number;
      swarm_hit_after: number;
      swarm_vs_single_before: number;
      swarm_vs_single_after: number;
      swarm_vs_isolated_before: number;
      swarm_vs_isolated_after: number;
      focus_domain?: string;
      focus_domain_documents_delta?: number;
      focus_domain_single_hit_delta?: number;
      focus_domain_swarm_hit_delta?: number;
      focus_domain_advantage_delta?: number;
    } | null;
    before?: Record<string, unknown>;
    after?: Record<string, unknown>;
    message?: string;
  };
}
