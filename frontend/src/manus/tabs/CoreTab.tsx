/**
 * tabs/CoreTab.tsx
 *
 * Панель подключенных модулей ядра. Каждая плитка берет состояние из
 * реального backend API, а не из моков.
 */

import { useCallback, useEffect, useMemo, useState, type ReactNode } from 'react';
import {
  Activity,
  AlertTriangle,
  Archive,
  Box,
  Brain,
  CheckCircle2,
  Cpu,
  Database,
  GitBranch,
  HardDrive,
  Layers,
  Loader2,
  Mic,
  Network,
  Play,
  RefreshCw,
  Search,
  Server,
  Sparkles,
  Zap,
} from 'lucide-react';

type ProbeStatus = 'ok' | 'warn' | 'error' | 'idle';

interface ProbeResult<T> {
  ok: boolean;
  data?: T;
  error?: string;
}

interface HealthDetail {
  status: string;
  uptime_s: number;
  version: string;
  subsystems: {
    engine?: {
      status: string;
      patterns: number;
      edges: number;
      documents_loaded: boolean;
      embeddings_ready: boolean;
      causal_index_ready: boolean;
      error?: string;
    };
    persistence?: {
      status: string;
      db_size_mb?: number;
      patterns_count?: number;
      edges_count?: number;
      enabled?: boolean;
      error?: string;
    };
    corpus?: {
      status: string;
      files: number;
      size_kb: number;
      error?: string;
    };
    memory?: {
      rss_mb: number;
      percent: number;
    };
    disk?: {
      free_gb: number;
      percent_used: number;
    };
  };
}

interface AiStats {
  model_available: boolean;
  graph_patterns: number;
  graph_edges: number;
  graph_max_patterns: number;
  graph_max_edges: number;
  graph_documents: number;
  graph_tokens: number;
  graph_version: number;
  formula_generation: number;
  formula_fitness: number;
  formula_layers: number;
  formula_layers_fast: number;
  embedding_vocab_size: number;
  embedding_trained_pairs: number;
  sentence_store_size: number;
}

interface ModelStats {
  exists: boolean;
  path: string;
  size_mb: number;
  patterns: number;
  edges: number;
}

interface KnowledgeStats {
  documents: number;
  relations: number;
  reason_blocks: number;
  by_type: Record<string, number>;
}

interface GenomeInfo {
  content: string;
  size: number;
}

interface GpuStatus {
  status: string;
  db_path: string;
  documents: number;
  embeddings: number;
  size_bytes: number;
}

interface ArchiverProjectStatus {
  success: boolean;
  project_root: string;
  seed_bin_available: boolean;
  vault_available: boolean;
  default_archive_root: string;
  default_restore_root: string;
}

interface VoiceHealth {
  enabled: boolean;
  provider: string;
  detail: string;
}

interface SwarmStatus {
  local_node_id: string;
  total_nodes: number;
  active_nodes: number;
  target_nodes: number;
  sync_events: number;
}

interface SyncStatus {
  enabled: boolean;
  detail: string | null;
  node_id: string;
  global_version: number;
  peers: number;
  deltas_sent: number;
  deltas_received: number;
  bytes_sent: number;
  bytes_received: number;
}

interface SystemStats {
  cpu: number;
  memory: number;
  memory_used_gb: number;
  uptime: number;
  processes: number;
}

interface ObserverNodes {
  nodes: Array<{ pid: number; status: string; uptime: number }>;
  count: number;
}

interface Snapshot {
  health?: HealthDetail;
  ai?: AiStats;
  model?: ModelStats;
  knowledge?: KnowledgeStats;
  genome?: GenomeInfo;
  gpu?: GpuStatus;
  archiver?: ArchiverProjectStatus;
  voice?: VoiceHealth;
  swarm?: SwarmStatus;
  sync?: SyncStatus;
  system?: SystemStats;
  observer?: ObserverNodes;
  factory?: unknown[];
  errors: Record<string, string>;
  fetchedAt?: number;
}

interface ModuleTile {
  id: string;
  title: string;
  subtitle: string;
  status: ProbeStatus;
  icon: ReactNode;
  facts: string[];
}

const statusLabel: Record<ProbeStatus, string> = {
  ok: 'OK',
  warn: 'Внимание',
  error: 'Ошибка',
  idle: 'Нет данных',
};

const extractError = (payload: unknown, fallback: string): string => {
  if (typeof payload === 'string' && payload.trim()) {
    return payload;
  }
  if (payload && typeof payload === 'object') {
    const detail = (payload as { detail?: unknown }).detail;
    if (typeof detail === 'string') {
      return detail;
    }
    if (detail) {
      return JSON.stringify(detail);
    }
  }
  return fallback;
};

const readJson = async <T,>(url: string, init?: RequestInit): Promise<ProbeResult<T>> => {
  try {
    const response = await fetch(url, init);
    const raw = await response.text();
    let payload: unknown = null;
    if (raw) {
      try {
        payload = JSON.parse(raw);
      } catch {
        payload = raw;
      }
    }

    if (!response.ok) {
      return {
        ok: false,
        error: extractError(payload, `${response.status} ${response.statusText}`),
      };
    }

    return { ok: true, data: payload as T };
  } catch (error) {
    return { ok: false, error: error instanceof Error ? error.message : String(error) };
  }
};

const formatInt = (value?: number): string => (Number.isFinite(value) ? Math.round(value as number).toLocaleString('ru-RU') : '0');
const formatPercent = (value?: number): string => (Number.isFinite(value) ? `${Number(value).toFixed(1)}%` : '0%');
const formatMb = (value?: number): string => (Number.isFinite(value) ? `${Number(value).toFixed(2)} МБ` : '0 МБ');
const formatBytes = (value?: number): string => {
  if (!Number.isFinite(value)) return '0 Б';
  const bytes = Math.max(0, Number(value));
  if (bytes >= 1024 ** 2) return `${(bytes / 1024 ** 2).toFixed(2)} МБ`;
  if (bytes >= 1024) return `${(bytes / 1024).toFixed(1)} КБ`;
  return `${Math.round(bytes)} Б`;
};
const formatUptime = (seconds?: number): string => {
  if (!Number.isFinite(seconds)) return '0 мин';
  const total = Math.max(0, Number(seconds));
  const days = Math.floor(total / 86400);
  const hours = Math.floor((total % 86400) / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  if (days > 0) return `${days} д ${hours} ч`;
  if (hours > 0) return `${hours} ч ${minutes} мин`;
  return `${minutes} мин`;
};

export const CoreTab = () => {
  const [snapshot, setSnapshot] = useState<Snapshot>({ errors: {} });
  const [loading, setLoading] = useState(true);
  const [probeText, setProbeText] = useState('compression formulas knowledge graph');
  const [action, setAction] = useState<'cognition' | 'concept' | 'reload' | null>(null);
  const [actionResult, setActionResult] = useState<string>('');
  const [actionError, setActionError] = useState('');

  const refresh = useCallback(async () => {
    setLoading(true);

    const [
      health,
      ai,
      model,
      knowledge,
      genome,
      gpu,
      archiver,
      voice,
      swarm,
      sync,
      system,
      observer,
      factory,
    ] = await Promise.all([
      readJson<HealthDetail>('/api/v1/health/detail'),
      readJson<AiStats>('/api/v1/ai/stats'),
      readJson<ModelStats>('/api/v1/model/stats'),
      readJson<KnowledgeStats>('/api/knowledge/stats'),
      readJson<GenomeInfo>('/api/fs/genome'),
      readJson<GpuStatus>('/api/gpu/status'),
      readJson<ArchiverProjectStatus>('/api/archiver/project/status'),
      readJson<VoiceHealth>('/api/v1/ai/voice/health'),
      readJson<SwarmStatus>('/api/v1/swarm/status'),
      readJson<SyncStatus>('/api/v1/sync/status'),
      readJson<SystemStats>('/api/system/stats'),
      readJson<ObserverNodes>('/api/observer/nodes'),
      readJson<unknown[]>('/api/factory/items'),
    ]);

    const errors: Record<string, string> = {};
    const next: Snapshot = { errors, fetchedAt: Date.now() };

    const attach = <T,>(key: keyof Snapshot, result: ProbeResult<T>) => {
      if (result.ok) {
        (next as unknown as Record<string, unknown>)[key] = result.data;
      } else if (result.error) {
        errors[String(key)] = result.error;
      }
    };

    attach('health', health);
    attach('ai', ai);
    attach('model', model);
    attach('knowledge', knowledge);
    attach('genome', genome);
    attach('gpu', gpu);
    attach('archiver', archiver);
    attach('voice', voice);
    attach('swarm', swarm);
    attach('sync', sync);
    attach('system', system);
    attach('observer', observer);
    attach('factory', factory);

    setSnapshot(next);
    setLoading(false);
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const modules = useMemo<ModuleTile[]>(() => {
    const engine = snapshot.health?.subsystems.engine;
    const persistence = snapshot.health?.subsystems.persistence;
    const corpus = snapshot.health?.subsystems.corpus;
    const memory = snapshot.health?.subsystems.memory;
    const disk = snapshot.health?.subsystems.disk;
    const syncDisabled = snapshot.sync?.enabled === false || snapshot.errors.sync?.toLowerCase().includes('token');

    return [
      {
        id: 'engine',
        title: 'AI Engine',
        subtitle: engine?.documents_loaded || snapshot.ai?.model_available ? 'корпус загружен' : 'ожидает корпус',
        status: engine?.status === 'ok' || snapshot.ai?.model_available ? 'ok' : snapshot.errors.health ? 'error' : 'idle',
        icon: <Brain size={22} />,
        facts: [
          `${formatInt(snapshot.ai?.graph_patterns ?? engine?.patterns)} паттернов`,
          `${formatInt(snapshot.ai?.graph_edges ?? engine?.edges)} связей`,
          `версия ${formatInt(snapshot.ai?.graph_version)}`,
        ],
      },
      {
        id: 'formula',
        title: 'Formula Core',
        subtitle: 'числовая формульная модель',
        status: snapshot.ai ? 'ok' : snapshot.errors.ai ? 'error' : 'idle',
        icon: <Sparkles size={22} />,
        facts: [
          `поколение ${formatInt(snapshot.ai?.formula_generation)}`,
          `fitness ${snapshot.ai?.formula_fitness?.toFixed(4) ?? '0.0000'}`,
          `${formatInt(snapshot.ai?.formula_layers)} слоев`,
        ],
      },
      {
        id: 'persistence',
        title: 'Persistence',
        subtitle: persistence?.enabled === false ? 'хранилище отключено' : 'SQLite + граф',
        status: persistence?.status === 'ok' ? 'ok' : persistence?.status === 'disabled' ? 'warn' : snapshot.errors.health ? 'error' : 'idle',
        icon: <Database size={22} />,
        facts: [
          `${formatInt(persistence?.patterns_count)} паттернов БД`,
          `${formatInt(persistence?.edges_count)} связей БД`,
          formatMb(persistence?.db_size_mb),
        ],
      },
      {
        id: 'corpus',
        title: 'Corpus',
        subtitle: 'текстовая база знаний',
        status: corpus?.status === 'ok' ? 'ok' : corpus?.status === 'missing' ? 'warn' : 'idle',
        icon: <Layers size={22} />,
        facts: [
          `${formatInt(corpus?.files)} файлов`,
          `${formatInt(corpus?.size_kb)} КБ`,
          `${formatInt(snapshot.ai?.graph_documents)} документов графа`,
        ],
      },
      {
        id: 'knowledge',
        title: 'Knowledge API',
        subtitle: 'поиск, контекст, обратная связь',
        status: snapshot.knowledge ? 'ok' : snapshot.errors.knowledge ? 'error' : 'idle',
        icon: <Search size={22} />,
        facts: [
          `${formatInt(snapshot.knowledge?.documents)} документов`,
          `${formatInt(snapshot.knowledge?.relations)} отношений`,
          `${formatInt(snapshot.knowledge?.reason_blocks)} reasoning-блоков`,
        ],
      },
      {
        id: 'genome',
        title: 'Genome FS',
        subtitle: snapshot.genome?.content ? 'kolibri.genome доступен' : 'геном не найден',
        status: snapshot.genome?.size ? 'ok' : snapshot.genome ? 'warn' : snapshot.errors.genome ? 'error' : 'idle',
        icon: <GitBranch size={22} />,
        facts: [
          formatBytes(snapshot.genome?.size),
          snapshot.genome?.content?.split('\n')[0] ?? 'нет заголовка',
          'filesystem bridge',
        ],
      },
      {
        id: 'gpu',
        title: 'GPU Store',
        subtitle: 'локальное vector-search хранилище',
        status: snapshot.gpu?.status === 'ok' ? 'ok' : snapshot.errors.gpu ? 'error' : 'idle',
        icon: <HardDrive size={22} />,
        facts: [
          `${formatInt(snapshot.gpu?.documents)} документов`,
          `${formatInt(snapshot.gpu?.embeddings)} эмбеддингов`,
          formatBytes(snapshot.gpu?.size_bytes),
        ],
      },
      {
        id: 'model',
        title: 'KLM Model',
        subtitle: snapshot.model?.exists ? 'файл модели найден' : 'файл модели не найден',
        status: snapshot.model?.exists ? 'ok' : snapshot.model ? 'warn' : snapshot.errors.model ? 'error' : 'idle',
        icon: <HardDrive size={22} />,
        facts: [
          formatMb(snapshot.model?.size_mb),
          `${formatInt(snapshot.model?.patterns)} C-паттернов`,
          `${formatInt(snapshot.model?.edges)} C-связей`,
        ],
      },
      {
        id: 'archiver',
        title: 'Archiver',
        subtitle: 'seed+bin и Kolibri Vault',
        status: snapshot.archiver?.success ? 'ok' : snapshot.errors.archiver ? 'error' : 'idle',
        icon: <Archive size={22} />,
        facts: [
          `seed+bin: ${snapshot.archiver?.seed_bin_available ? 'готов' : 'нет'}`,
          `.klb: ${snapshot.archiver?.vault_available ? 'готов' : 'нет'}`,
          snapshot.archiver?.default_archive_root ?? '/tmp/kolibri_archives',
        ],
      },
      {
        id: 'voice',
        title: 'Voice',
        subtitle: snapshot.voice?.provider ?? 'OpenAI Audio',
        status: snapshot.voice?.enabled ? 'ok' : snapshot.voice ? 'warn' : snapshot.errors.voice ? 'error' : 'idle',
        icon: <Mic size={22} />,
        facts: [
          snapshot.voice?.enabled ? 'ключ API настроен' : 'ключ API не настроен',
          snapshot.voice?.detail ?? 'нет ответа',
          'TTS + STT endpoints',
        ],
      },
      {
        id: 'swarm',
        title: 'Swarm',
        subtitle: snapshot.swarm?.local_node_id ?? 'локальный узел',
        status: snapshot.swarm ? 'ok' : snapshot.errors.swarm ? 'error' : 'idle',
        icon: <Network size={22} />,
        facts: [
          `${formatInt(snapshot.swarm?.active_nodes)} активных`,
          `${formatInt(snapshot.swarm?.total_nodes)} всего`,
          `цель ${formatInt(snapshot.swarm?.target_nodes)}`,
        ],
      },
      {
        id: 'sync',
        title: 'Delta Sync',
        subtitle: syncDisabled ? 'нужен KOLIBRI_SWARM_TOKEN' : 'межузловая синхронизация',
        status: syncDisabled ? 'warn' : snapshot.sync ? 'ok' : snapshot.errors.sync ? 'error' : 'idle',
        icon: <GitBranch size={22} />,
        facts: [
          snapshot.sync ? `версия ${formatInt(snapshot.sync.global_version)}` : 'API недоступен',
          snapshot.sync?.detail ?? snapshot.errors.sync ?? 'готов к обмену дельтами',
          `${formatInt(snapshot.sync?.peers)} peers`,
        ],
      },
      {
        id: 'system',
        title: 'System Bridge',
        subtitle: 'метрики процесса и ОС',
        status: snapshot.system ? 'ok' : snapshot.errors.system ? 'error' : 'idle',
        icon: <Cpu size={22} />,
        facts: [
          `CPU ${formatPercent(snapshot.system?.cpu)}`,
          `RAM ${formatPercent(snapshot.system?.memory)}`,
          `${formatInt(snapshot.system?.processes)} процессов`,
        ],
      },
      {
        id: 'observer',
        title: 'Observer',
        subtitle: 'локальные kolibri_node',
        status: snapshot.observer ? 'ok' : snapshot.errors.observer ? 'error' : 'idle',
        icon: <Server size={22} />,
        facts: [
          `${formatInt(snapshot.observer?.count)} процессов`,
          `uptime backend ${formatUptime(snapshot.health?.uptime_s)}`,
          `диск свободно ${formatInt(disk?.free_gb)} ГБ`,
        ],
      },
      {
        id: 'factory',
        title: 'Content Factory',
        subtitle: 'генерация и аналитика контента',
        status: snapshot.factory ? 'ok' : snapshot.errors.factory ? 'error' : 'idle',
        icon: <Box size={22} />,
        facts: [
          `${formatInt(snapshot.factory?.length)} задач`,
          'trends / videos / approval',
          `RSS ${formatMb(memory?.rss_mb)}`,
        ],
      },
    ];
  }, [snapshot]);

  const moduleCounts = modules.reduce(
    (acc, item) => {
      acc[item.status] += 1;
      return acc;
    },
    { ok: 0, warn: 0, error: 0, idle: 0 } as Record<ProbeStatus, number>,
  );

  const runAction = async (nextAction: 'cognition' | 'concept' | 'reload') => {
    setAction(nextAction);
    setActionResult('');
    setActionError('');

    const query = probeText.trim() || 'kolibri core';
    const seed = Date.now() % 2147483647;
    const request =
      nextAction === 'reload'
        ? readJson<Record<string, unknown>>('/api/v1/ai/reload', { method: 'POST' })
        : nextAction === 'cognition'
          ? readJson<Record<string, unknown>>('/api/v1/cognition/enhanced', {
              method: 'POST',
              headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify({ query }),
            })
          : readJson<Record<string, unknown>>('/api/v1/concept/run', {
              method: 'POST',
              headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify({
                query,
                corpus: [query, 'Kolibri core connects formulas, graph memory, archiver, swarm and cognition.'],
                peer_count: 3,
                swarm_rounds: 1,
                formula_generations: 2,
                cognition_depth: 2,
                seed,
              }),
            });

    const result = await request;
    if (result.ok) {
      setActionResult(JSON.stringify(result.data, null, 2));
      if (nextAction === 'reload') {
        void refresh();
      }
    } else {
      setActionError(result.error ?? 'Неизвестная ошибка');
    }
    setAction(null);
  };

  return (
    <div className="core-tab">
      <header className="core-header">
        <div>
          <div className="core-kicker">Kolibri AGI Runtime</div>
          <h1 className="core-title">Ядро</h1>
          <p className="core-subtitle">
            Подключены AI engine, корпус, граф, архивация, swarm, синхронизация, голос, фабрика и системный мост.
          </p>
        </div>
        <button type="button" className="core-refresh" onClick={() => void refresh()} disabled={loading}>
          {loading ? <Loader2 size={18} className="core-spin" /> : <RefreshCw size={18} />}
          <span>Обновить</span>
        </button>
      </header>

      <section className="core-summary" aria-label="Сводка модулей">
        <div className="core-summary-item is-ok">
          <CheckCircle2 size={18} />
          <span>{moduleCounts.ok} OK</span>
        </div>
        <div className="core-summary-item is-warn">
          <AlertTriangle size={18} />
          <span>{moduleCounts.warn} предупреждений</span>
        </div>
        <div className="core-summary-item is-error">
          <Activity size={18} />
          <span>{moduleCounts.error} ошибок</span>
        </div>
        <div className="core-summary-item">
          <Server size={18} />
          <span>{snapshot.fetchedAt ? new Date(snapshot.fetchedAt).toLocaleTimeString('ru-RU') : 'нет снимка'}</span>
        </div>
      </section>

      <section className="core-grid" aria-label="Модули ядра">
        {modules.map((module) => (
          <article key={module.id} className={`core-module is-${module.status}`}>
            <div className="core-module-top">
              <span className="core-module-icon" aria-hidden="true">{module.icon}</span>
              <span className="core-module-status">{statusLabel[module.status]}</span>
            </div>
            <h2>{module.title}</h2>
            <p>{module.subtitle}</p>
            <ul>
              {module.facts.map((fact) => (
                <li key={fact}>{fact}</li>
              ))}
            </ul>
          </article>
        ))}
      </section>

      <section className="core-lab" aria-label="Проверка ядра">
        <div className="core-lab-main">
          <div className="core-lab-title">
            <Zap size={18} />
            <span>Живая проверка когнитивного контура</span>
          </div>
          <div className="core-probe-row">
            <input
              value={probeText}
              onChange={(event) => setProbeText(event.target.value)}
              placeholder="Запрос для cognition/concept runtime"
            />
            <button type="button" onClick={() => void runAction('cognition')} disabled={action !== null}>
              {action === 'cognition' ? <Loader2 size={16} className="core-spin" /> : <Brain size={16} />}
              <span>Когниция</span>
            </button>
            <button type="button" onClick={() => void runAction('concept')} disabled={action !== null}>
              {action === 'concept' ? <Loader2 size={16} className="core-spin" /> : <Play size={16} />}
              <span>Концепт</span>
            </button>
            <button type="button" onClick={() => void runAction('reload')} disabled={action !== null}>
              {action === 'reload' ? <Loader2 size={16} className="core-spin" /> : <RefreshCw size={16} />}
              <span>Reload</span>
            </button>
          </div>
        </div>

        {actionError && (
          <div className="core-action-error">
            <AlertTriangle size={16} />
            <span>{actionError}</span>
          </div>
        )}

        {actionResult && (
          <pre className="core-action-output">{actionResult}</pre>
        )}
      </section>

      <style>{`
        .core-tab {
          height: 100%;
          overflow: auto;
          padding: 24px;
          color: var(--text-primary);
        }

        .core-header {
          display: flex;
          align-items: flex-start;
          justify-content: space-between;
          gap: 16px;
          margin-bottom: 18px;
        }

        .core-kicker {
          color: var(--accent-primary);
          font-size: 12px;
          font-weight: 700;
          text-transform: uppercase;
          letter-spacing: 0.08em;
          margin-bottom: 6px;
        }

        .core-title {
          margin: 0;
          font-size: 30px;
          font-weight: 700;
          letter-spacing: 0;
        }

        .core-subtitle {
          max-width: 760px;
          margin: 8px 0 0;
          color: var(--text-muted);
          font-size: 14px;
          line-height: 1.5;
        }

        .core-refresh {
          min-height: 40px;
          border: 1px solid var(--border-primary);
          border-radius: 8px;
          background: var(--bg-tertiary);
          color: var(--text-secondary);
          display: inline-flex;
          align-items: center;
          gap: 8px;
          padding: 0 14px;
          cursor: pointer;
          flex-shrink: 0;
        }

        .core-refresh:disabled {
          opacity: 0.55;
          cursor: wait;
        }

        .core-summary {
          display: grid;
          grid-template-columns: repeat(4, minmax(0, 1fr));
          gap: 10px;
          margin-bottom: 16px;
        }

        .core-summary-item {
          min-height: 46px;
          border: 1px solid var(--border-primary);
          border-radius: 8px;
          background: var(--bg-tertiary);
          color: var(--text-secondary);
          display: inline-flex;
          align-items: center;
          gap: 8px;
          padding: 0 12px;
          font-size: 13px;
          font-weight: 600;
          min-width: 0;
        }

        .core-summary-item.is-ok { color: var(--success); }
        .core-summary-item.is-warn { color: var(--warning); }
        .core-summary-item.is-error { color: var(--error); }

        .core-grid {
          display: grid;
          grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
          gap: 12px;
        }

        .core-module {
          border: 1px solid var(--border-primary);
          border-radius: 8px;
          background: var(--bg-card);
          padding: 16px;
          min-height: 190px;
          display: flex;
          flex-direction: column;
          gap: 10px;
        }

        .core-module.is-ok { border-color: color-mix(in srgb, var(--success) 34%, var(--border-primary)); }
        .core-module.is-warn { border-color: color-mix(in srgb, var(--warning) 42%, var(--border-primary)); }
        .core-module.is-error { border-color: color-mix(in srgb, var(--error) 46%, var(--border-primary)); }

        .core-module-top {
          display: flex;
          justify-content: space-between;
          align-items: center;
          gap: 10px;
        }

        .core-module-icon {
          width: 40px;
          height: 40px;
          border-radius: 8px;
          background: var(--accent-bg);
          color: var(--accent-primary);
          display: inline-flex;
          align-items: center;
          justify-content: center;
        }

        .core-module-status {
          border: 1px solid var(--border-primary);
          border-radius: 999px;
          color: var(--text-muted);
          padding: 4px 8px;
          font-size: 11px;
          font-weight: 700;
        }

        .core-module.is-ok .core-module-status { color: var(--success); }
        .core-module.is-warn .core-module-status { color: var(--warning); }
        .core-module.is-error .core-module-status { color: var(--error); }

        .core-module h2 {
          margin: 0;
          font-size: 16px;
          font-weight: 700;
          letter-spacing: 0;
        }

        .core-module p {
          margin: 0;
          color: var(--text-muted);
          font-size: 13px;
          line-height: 1.45;
          min-height: 38px;
        }

        .core-module ul {
          list-style: none;
          padding: 0;
          margin: auto 0 0;
          display: grid;
          gap: 6px;
        }

        .core-module li {
          color: var(--text-secondary);
          font-size: 12px;
          line-height: 1.35;
          overflow-wrap: anywhere;
        }

        .core-lab {
          margin-top: 16px;
          border: 1px solid var(--border-primary);
          border-radius: 8px;
          background: var(--bg-card);
          padding: 16px;
        }

        .core-lab-title {
          display: flex;
          align-items: center;
          gap: 8px;
          color: var(--text-primary);
          font-weight: 700;
          margin-bottom: 12px;
        }

        .core-probe-row {
          display: grid;
          grid-template-columns: minmax(180px, 1fr) repeat(3, auto);
          gap: 8px;
          align-items: center;
        }

        .core-probe-row input {
          min-height: 40px;
          border: 1px solid var(--border-primary);
          border-radius: 8px;
          background: var(--bg-input);
          color: var(--text-primary);
          padding: 0 12px;
          outline: none;
          min-width: 0;
        }

        .core-probe-row input:focus {
          border-color: var(--border-accent);
        }

        .core-probe-row button {
          min-height: 40px;
          border: 1px solid var(--border-primary);
          border-radius: 8px;
          background: var(--bg-tertiary);
          color: var(--text-secondary);
          display: inline-flex;
          align-items: center;
          justify-content: center;
          gap: 7px;
          padding: 0 12px;
          cursor: pointer;
        }

        .core-probe-row button:hover:not(:disabled) {
          border-color: var(--border-accent);
          color: var(--text-primary);
        }

        .core-probe-row button:disabled {
          opacity: 0.55;
          cursor: wait;
        }

        .core-action-error {
          margin-top: 12px;
          display: flex;
          align-items: center;
          gap: 8px;
          color: var(--error);
          font-size: 13px;
        }

        .core-action-output {
          margin: 14px 0 0;
          max-height: 360px;
          overflow: auto;
          border: 1px solid var(--border-primary);
          border-radius: 8px;
          background: var(--bg-overlay);
          color: var(--text-secondary);
          padding: 12px;
          font-size: 12px;
          line-height: 1.45;
          white-space: pre-wrap;
        }

        .core-spin {
          animation: core-spin 1s linear infinite;
        }

        @keyframes core-spin {
          to { transform: rotate(360deg); }
        }

        @media (max-width: 900px) {
          .core-tab { padding: 16px; }
          .core-header { flex-direction: column; }
          .core-refresh { width: 100%; justify-content: center; }
          .core-summary { grid-template-columns: repeat(2, minmax(0, 1fr)); }
          .core-grid { grid-template-columns: 1fr; }
          .core-probe-row { grid-template-columns: 1fr; }
          .core-probe-row button { width: 100%; }
        }
      `}</style>
    </div>
  );
};
