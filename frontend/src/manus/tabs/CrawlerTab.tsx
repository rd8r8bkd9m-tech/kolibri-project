/**
 * tabs/CrawlerTab.tsx
 *
 * Колибри AI Learning Agent — автономный агент обучения.
 * Вводишь тему → агент ищет по DuckDuckGo/Bing/Wikipedia →
 * загружает страницы → обучает модель → показывает результат.
 *
 * Также поддерживает прямой ввод URL (режим "url").
 */

import { useState, useEffect, useRef, useCallback } from 'react';
import {
  Globe,
  Play,
  Square,
  Loader2,
  CheckCircle2,
  AlertCircle,
  Database,
  Zap,
  BarChart3,
  Clock,
  FileText,
  RefreshCw,
  Search,
  Network,
  ArrowRight,
  Target,
  Activity,
  ChevronDown,
  ChevronUp,
  Link2,
} from 'lucide-react';

/* ═══════════════════════════════════════ типы ═══════════════════════════════════════ */

interface AgentStatus {
  running: boolean;
  topic: string;
  phase: string;
  progress: number;
  urls_found: number;
  search_results: SearchResult[];
  urls_crawled: number;
  urls_total: number;
  urls_failed: number;
  current_url: string;
  bytes_downloaded: number;
  pages_text: PageInfo[];
  patterns: number;
  edges: number;
  tokens: number;
  model_size_mb: number;
  time_elapsed: number;
  events: AgentEvent[];
}

interface SearchResult {
  url: string;
  title: string;
  snippet: string;
  source: string;
}

interface PageInfo {
  url: string;
  title: string;
  chars: number;
  ok: boolean;
}

interface AgentEvent {
  ts: number;
  phase: string;
  msg: string;
}

interface CrawlResult {
  status: string;
  pages_crawled: number;
  patterns: number;
  edges: number;
  tokens: number;
  model_size_mb: number;
  time_sec: number;
  output: string;
}

interface ModelInfo {
  exists: boolean;
  path: string;
  size_mb: number;
  patterns: number;
  edges: number;
  max_patterns: number;
  max_edges: number;
}

/* ═══════════════════════════════════════ константы ═══════════════════════════════════════ */

const API = '/api/v1';

const TOPIC_PRESETS = [
  { label: 'Искусственный интеллект', icon: '🤖', topic: 'artificial intelligence machine learning' },
  { label: 'Квантовые вычисления', icon: '⚛️', topic: 'quantum computing qubits' },
  { label: 'Нейронные сети', icon: '🧠', topic: 'neural networks deep learning transformers' },
  { label: 'Космос и астрономия', icon: '🚀', topic: 'space exploration astronomy planets' },
  { label: 'Блокчейн и крипто', icon: '🔗', topic: 'blockchain cryptocurrency DeFi' },
  { label: 'Робототехника', icon: '🤖', topic: 'robotics automation engineering' },
  { label: 'Генетика и биотех', icon: '🧬', topic: 'genetics biotechnology DNA CRISPR' },
  { label: 'Кибербезопасность', icon: '🛡️', topic: 'cybersecurity hacking encryption' },
  { label: 'Компрессия данных', icon: '📦', topic: 'data compression algorithms lossless' },
  { label: 'Операционные системы', icon: '💻', topic: 'operating systems Linux kernel' },
];

const PHASE_CONFIG: Record<string, { label: string; color: string; icon: string }> = {
  idle: { label: 'Ожидание', color: '#71717a', icon: '⏸️' },
  searching: { label: 'Поиск', color: '#3b82f6', icon: '🔍' },
  crawling: { label: 'Загрузка', color: '#f59e0b', icon: '🕷️' },
  training: { label: 'Обучение', color: '#8b5cf6', icon: '🧠' },
  complete: { label: 'Готово', color: '#10b981', icon: '✅' },
  error: { label: 'Ошибка', color: '#ef4444', icon: '❌' },
};

const PHASES_ORDER = ['searching', 'crawling', 'training', 'complete'];

const SOURCE_COLORS: Record<string, string> = {
  duckduckgo: '#de5833',
  bing: '#00809d',
  wikipedia_en: '#636466',
  wikipedia_ru: '#636466',
};

/* ═══════════════════════════════════════ компонент ═══════════════════════════════════════ */

export const CrawlerTab = () => {
  /* — mode — */
  const [inputMode, setInputMode] = useState<'topic' | 'url'>('topic');

  /* — topic agent state — */
  const [topic, setTopic] = useState('');
  const [maxUrls, setMaxUrls] = useState(25);
  const [agentStatus, setAgentStatus] = useState<AgentStatus | null>(null);
  const [isAgentRunning, setIsAgentRunning] = useState(false);

  /* — url crawl state — */
  const [url, setUrl] = useState('');
  const [crawlMode, setCrawlMode] = useState<'url' | 'crawl'>('crawl');
  const [depth, setDepth] = useState(1);
  const [maxPages, setMaxPages] = useState(10);
  const [isCrawling, setIsCrawling] = useState(false);
  const [crawlResult, setCrawlResult] = useState<CrawlResult | null>(null);

  /* — shared — */
  const [model, setModel] = useState<ModelInfo | null>(null);
  const [error, setError] = useState('');
  const [showUrls, setShowUrls] = useState(false);

  const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const eventsRef = useRef<HTMLDivElement>(null);

  /* ═══════ helpers ═══════ */

  const fetchModel = useCallback(async () => {
    try {
      const r = await fetch(`${API}/model/stats`);
      if (r.ok) setModel(await r.json());
    } catch { /* ok */ }
  }, []);

  useEffect(() => { fetchModel(); }, [fetchModel]);

  // Auto-scroll events
  useEffect(() => {
    if (eventsRef.current) {
      eventsRef.current.scrollTop = eventsRef.current.scrollHeight;
    }
  }, [agentStatus?.events]);

  /* ═══════ Agent polling ═══════ */

  const startAgentPolling = useCallback(() => {
    pollRef.current = setInterval(async () => {
      try {
        const r = await fetch(`${API}/agent/status`);
        if (!r.ok) return;
        const data: AgentStatus = await r.json();
        setAgentStatus(data);

        if (!data.running && data.phase !== 'idle') {
          // Agent finished
          setIsAgentRunning(false);
          if (pollRef.current) {
            clearInterval(pollRef.current);
            pollRef.current = null;
          }
          fetchModel();
        }
      } catch { /* ok */ }
    }, 600);
  }, [fetchModel]);

  const stopPolling = useCallback(() => {
    if (pollRef.current) {
      clearInterval(pollRef.current);
      pollRef.current = null;
    }
  }, []);

  useEffect(() => () => stopPolling(), [stopPolling]);

  /* ═══════ Start Agent ═══════ */

  const handleStartAgent = async () => {
    if (!topic.trim()) return;
    setError('');
    setAgentStatus(null);
    setCrawlResult(null);
    setIsAgentRunning(true);

    try {
      const r = await fetch(`${API}/agent/start`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          topic: topic.trim(),
          max_urls: maxUrls,
          engines: ['duckduckgo', 'wikipedia_en', 'wikipedia_ru', 'bing'],
          restart_if_running: true,
        }),
      });

      if (!r.ok) {
        const data = await r.json();
        setError(data.detail || 'Ошибка запуска агента');
        setIsAgentRunning(false);
        return;
      }

      startAgentPolling();
    } catch (e) {
      setError(`Не удалось подключиться к backend: ${e}`);
      setIsAgentRunning(false);
    }
  };

  /* ═══════ Stop Agent ═══════ */

  const handleStopAgent = async () => {
    try {
      await fetch(`${API}/agent/stop`, { method: 'POST' });
    } catch { /* ok */ }
    setIsAgentRunning(false);
    stopPolling();
  };

  /* ═══════ URL Crawl ═══════ */

  const handleStartCrawl = async () => {
    if (!url.trim()) return;
    setError('');
    setCrawlResult(null);
    setAgentStatus(null);
    setIsCrawling(true);

    try {
      const r = await fetch(`${API}/crawl`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          url: url.trim(),
          mode: crawlMode,
          depth,
          max_pages: maxPages,
          delay: 0.3,
        }),
      });

      const raw = (await r.json()) as unknown;
      if (!r.ok) {
        const detail =
          typeof raw === 'object' &&
          raw !== null &&
          'detail' in raw &&
          typeof (raw as { detail?: unknown }).detail === 'string'
            ? (raw as { detail: string }).detail
            : 'Ошибка';
        setError(detail);
        return;
      }

      const data = raw as CrawlResult;
      setCrawlResult(data);
      fetchModel();
    } catch (e) {
      setError(`Ошибка: ${e}`);
    } finally {
      setIsCrawling(false);
    }
  };

  /* ═══════ Helpers for rendering ═══════ */

  const phase = agentStatus?.phase || 'idle';
  const phaseConfig = PHASE_CONFIG[phase] || PHASE_CONFIG.idle;
  const progress = agentStatus?.progress || 0;
  const events = agentStatus?.events || [];
  const searchResults = agentStatus?.search_results || [];
  const pagesText = agentStatus?.pages_text || [];
  const isActive = isAgentRunning || (agentStatus?.running ?? false);
  const patternFill = model && model.max_patterns > 0 ? Math.min(100, (model.patterns / model.max_patterns) * 100) : 0;
  const edgesFill = model && model.max_edges > 0 ? Math.min(100, (model.edges / model.max_edges) * 100) : 0;

  /* ═══════ RENDER ═══════ */
  return (
    <div className="agent-tab">
      {/* ═══════ HEADER ═══════ */}
      <div className="agent-header">
        <div className="agent-header-left">
          <div className="agent-logo">
            <div className="agent-logo-glow" />
            <Globe size={28} />
          </div>
          <div>
            <h1 className="agent-title">
              Колибри <span className="agent-title-accent">Learning Agent</span>
            </h1>
            <p className="agent-subtitle">
              Автономное обучение из интернета — поиск, загрузка, обучение
            </p>
          </div>
        </div>

        {/* Mode toggle */}
        <div className="mode-toggle">
          <button
            className={`mode-toggle-btn ${inputMode === 'topic' ? 'active' : ''}`}
            onClick={() => setInputMode('topic')}
          >
            <Target size={14} />
            Тема
          </button>
          <button
            className={`mode-toggle-btn ${inputMode === 'url' ? 'active' : ''}`}
            onClick={() => setInputMode('url')}
          >
            <Link2 size={14} />
            URL
          </button>
        </div>
      </div>

      <div className="agent-body">
        {/* ═══════ LEFT: Controls ═══════ */}
        <div className="agent-controls">
          <div className="quick-guide">
            <div className="quick-guide-title">
              <Target size={14} />
              <span>Как пользоваться</span>
            </div>
            <ol>
              <li>Выберите режим: <strong>Тема</strong> или <strong>URL</strong>.</li>
              <li>Заполните поле и нажмите <strong>Запустить</strong>.</li>
              <li>Следите за прогрессом справа: фаза, статистика, источники и лог.</li>
            </ol>
          </div>

          {inputMode === 'topic' ? (
            /* ——— Topic Mode ——— */
            <>
              <div className="control-section">
                <label className="control-label">
                  <Search size={14} />
                  Тема для изучения
                </label>
                <div className="topic-input-wrap">
                  <Target size={18} className="topic-icon" />
                  <input
                    type="text"
                    className="topic-input"
                    value={topic}
                    onChange={e => setTopic(e.target.value)}
                    placeholder="Квантовые вычисления, AI, космос..."
                    disabled={isActive}
                    onKeyDown={e => e.key === 'Enter' && handleStartAgent()}
                  />
                </div>
              </div>

              {/* Пресеты тем */}
              <div className="presets">
                <label className="control-label">Быстрый выбор темы</label>
                <div className="preset-grid">
                  {TOPIC_PRESETS.map(p => (
                    <button
                      key={p.topic}
                      className={`preset-btn ${topic === p.topic ? 'active' : ''}`}
                      onClick={() => setTopic(p.topic)}
                      disabled={isActive}
                    >
                      <span>{p.icon}</span>
                      <span>{p.label}</span>
                    </button>
                  ))}
                </div>
              </div>

              {/* Параметры */}
              <div className="control-section">
                <label className="control-label">Параметры поиска</label>
                <div className="agent-params">
                  <div className="param-row">
                    <label>Макс. страниц</label>
                    <div className="param-control">
                      <input
                        type="range" min={5} max={60} step={5}
                        value={maxUrls} onChange={e => setMaxUrls(+e.target.value)}
                        disabled={isActive}
                      />
                      <span className="param-value">{maxUrls}</span>
                    </div>
                  </div>
                </div>
              </div>

              {/* Кнопка запуска */}
              {!isActive ? (
                <button
                  className="start-btn agent-start"
                  onClick={handleStartAgent}
                  disabled={!topic.trim()}
                >
                  <Play size={20} />
                  Запустить агента
                </button>
              ) : (
                <button className="start-btn agent-stop" onClick={handleStopAgent}>
                  <Square size={20} />
                  Остановить
                </button>
              )}
            </>
          ) : (
            /* ——— URL Mode ——— */
            <>
              <div className="control-section">
                <label className="control-label">
                  <Globe size={14} />
                  URL для обучения
                </label>
                <div className="topic-input-wrap">
                  <Globe size={18} className="topic-icon" />
                  <input
                    type="url"
                    className="topic-input"
                    value={url}
                    onChange={e => setUrl(e.target.value)}
                    placeholder="https://en.wikipedia.org/wiki/..."
                    disabled={isCrawling}
                    onKeyDown={e => e.key === 'Enter' && handleStartCrawl()}
                  />
                </div>
              </div>

              <div className="control-section">
                <label className="control-label">Режим</label>
                <div className="url-mode-switch">
                  <button
                    className={`mode-btn ${crawlMode === 'url' ? 'active' : ''}`}
                    onClick={() => setCrawlMode('url')}
                    disabled={isCrawling}
                  >
                    <FileText size={16} /> Одна страница
                  </button>
                  <button
                    className={`mode-btn ${crawlMode === 'crawl' ? 'active' : ''}`}
                    onClick={() => setCrawlMode('crawl')}
                    disabled={isCrawling}
                  >
                    <Network size={16} /> BFS-обход
                  </button>
                </div>
              </div>

              {crawlMode === 'crawl' && (
                <div className="agent-params">
                  <div className="param-row">
                    <label>Глубина</label>
                    <div className="param-control">
                      <input
                        type="range" min={0} max={3} step={1}
                        value={depth} onChange={e => setDepth(+e.target.value)}
                        disabled={isCrawling}
                      />
                      <span className="param-value">{depth}</span>
                    </div>
                  </div>
                  <div className="param-row">
                    <label>Макс. страниц</label>
                    <div className="param-control">
                      <input
                        type="range" min={1} max={50} step={1}
                        value={maxPages} onChange={e => setMaxPages(+e.target.value)}
                        disabled={isCrawling}
                      />
                      <span className="param-value">{maxPages}</span>
                    </div>
                  </div>
                </div>
              )}

              <button
                className="start-btn agent-start"
                onClick={handleStartCrawl}
                disabled={isCrawling || !url.trim()}
              >
                {isCrawling ? (
                  <><Loader2 size={20} className="spin" /> Загрузка...</>
                ) : (
                  <><Play size={20} /> Запустить</>
                )}
              </button>
            </>
          )}

          {/* Error */}
          {error && (
            <div className="error-banner">
              <AlertCircle size={16} />
              <span>{error}</span>
            </div>
          )}

          {/* Model card (always visible) */}
          {model && model.exists && (
            <div className="model-card">
              <div className="model-header">
                <Database size={16} />
                <span>Текущая модель</span>
                <button className="model-refresh" onClick={fetchModel}>
                  <RefreshCw size={12} />
                </button>
              </div>
              <div className="model-bars">
                <div className="model-bar-item">
                  <div className="bar-label">
                    <span>Паттерны</span>
                    <span>{model.patterns.toLocaleString()} / {model.max_patterns.toLocaleString()}</span>
                  </div>
                  <div className="bar-track">
                    <div
                      className="bar-fill patterns"
                      style={{ width: `${patternFill}%` }}
                    />
                  </div>
                </div>
                <div className="model-bar-item">
                  <div className="bar-label">
                    <span>Связи</span>
                    <span>{model.edges.toLocaleString()} / {model.max_edges.toLocaleString()}</span>
                  </div>
                  <div className="bar-track">
                    <div
                      className="bar-fill edges"
                      style={{ width: `${edgesFill}%` }}
                    />
                  </div>
                </div>
              </div>
              <div className="model-size">{model.size_mb} МБ / 50 МБ</div>
            </div>
          )}
        </div>

        {/* ═══════ RIGHT: Dashboard ═══════ */}
        <div className="agent-dashboard">

          {/* ——— Agent Phase Indicator ——— */}
          {(isActive || (agentStatus && phase !== 'idle')) && (
            <div className="phase-indicator">
              {PHASES_ORDER.map((p, i) => {
                const cfg = PHASE_CONFIG[p];
                const isCurrent = phase === p;
                const isDone = PHASES_ORDER.indexOf(phase) > i || phase === 'complete';

                return (
                  <div key={p} className="phase-step-wrap">
                    <div
                      className={`phase-step ${isCurrent ? 'current' : ''} ${isDone ? 'done' : ''} ${phase === 'error' && isCurrent ? 'error' : ''}`}
                    >
                      <div className="phase-dot">
                        {isDone && !isCurrent ? (
                          <CheckCircle2 size={16} />
                        ) : isCurrent ? (
                          <div className="phase-pulse" />
                        ) : (
                          <div className="phase-empty" />
                        )}
                      </div>
                      <span className="phase-label">{cfg.icon} {cfg.label}</span>
                    </div>
                    {i < PHASES_ORDER.length - 1 && (
                      <div className={`phase-connector ${isDone ? 'done' : ''}`}>
                        <ArrowRight size={14} />
                      </div>
                    )}
                  </div>
                );
              })}
            </div>
          )}

          {/* ——— Progress bar ——— */}
          {(isActive || (agentStatus && phase !== 'idle')) && (
            <div className="progress-section">
              <div className="progress-header">
                <span className="progress-label" style={{ color: phaseConfig.color }}>
                  {phaseConfig.icon} {phaseConfig.label}
                  {agentStatus?.current_url ? ` — ${agentStatus.current_url.substring(0, 60)}...` : ''}
                </span>
                <span className="progress-pct">{Math.round(progress)}%</span>
              </div>
              <div className="progress-bar">
                <div
                  className="progress-fill"
                  style={{
                    width: `${progress}%`,
                    background: `linear-gradient(90deg, ${phaseConfig.color}, ${phaseConfig.color}88)`,
                  }}
                />
              </div>
            </div>
          )}

          {/* ——— Stats Grid ——— */}
          {agentStatus && phase !== 'idle' && (
            <div className="stats-grid">
              <div className="stat-card">
                <Search size={18} />
                <div className="stat-num">{agentStatus.urls_found}</div>
                <div className="stat-lbl">Найдено URL</div>
              </div>
              <div className="stat-card">
                <Globe size={18} />
                <div className="stat-num">
                  {agentStatus.urls_crawled}
                  {agentStatus.urls_failed > 0 && (
                    <span className="stat-fail"> (-{agentStatus.urls_failed})</span>
                  )}
                </div>
                <div className="stat-lbl">Загружено</div>
              </div>
              <div className="stat-card">
                <FileText size={18} />
                <div className="stat-num">{agentStatus.tokens.toLocaleString()}</div>
                <div className="stat-lbl">Токенов</div>
              </div>
              <div className="stat-card">
                <Database size={18} />
                <div className="stat-num">{agentStatus.patterns.toLocaleString()}</div>
                <div className="stat-lbl">Паттернов</div>
              </div>
              <div className="stat-card">
                <Zap size={18} />
                <div className="stat-num">{agentStatus.edges.toLocaleString()}</div>
                <div className="stat-lbl">Связей</div>
              </div>
              <div className="stat-card">
                <Clock size={18} />
                <div className="stat-num">{agentStatus.time_elapsed.toFixed(1)}с</div>
                <div className="stat-lbl">Время</div>
              </div>
            </div>
          )}

          {/* ——— URL crawl result ——— */}
          {crawlResult && crawlResult.status === 'ok' && (
            <div className="stats-grid">
              <div className="stat-card">
                <Globe size={18} />
                <div className="stat-num">{crawlResult.pages_crawled}</div>
                <div className="stat-lbl">Страниц</div>
              </div>
              <div className="stat-card">
                <FileText size={18} />
                <div className="stat-num">{crawlResult.tokens.toLocaleString()}</div>
                <div className="stat-lbl">Токенов</div>
              </div>
              <div className="stat-card">
                <Database size={18} />
                <div className="stat-num">{crawlResult.patterns.toLocaleString()}</div>
                <div className="stat-lbl">Паттернов</div>
              </div>
              <div className="stat-card">
                <Zap size={18} />
                <div className="stat-num">{crawlResult.edges.toLocaleString()}</div>
                <div className="stat-lbl">Связей</div>
              </div>
              <div className="stat-card">
                <BarChart3 size={18} />
                <div className="stat-num">{crawlResult.model_size_mb} МБ</div>
                <div className="stat-lbl">Модель</div>
              </div>
              <div className="stat-card">
                <Clock size={18} />
                <div className="stat-num">{crawlResult.time_sec}с</div>
                <div className="stat-lbl">Время</div>
              </div>
            </div>
          )}

          {/* ——— Search Results ——— */}
          {searchResults.length > 0 && (
            <div className="urls-section">
              <button
                className="urls-toggle"
                onClick={() => setShowUrls(!showUrls)}
              >
                <Search size={14} />
                <span>Найденные источники ({searchResults.length})</span>
                {showUrls ? <ChevronUp size={14} /> : <ChevronDown size={14} />}
              </button>

              {showUrls && (
                <div className="urls-list">
                  {searchResults.map((sr, i) => {
                    const page = pagesText.find(p => p.url === sr.url);
                    const srcColor = SOURCE_COLORS[sr.source] || '#71717a';
                    return (
                      <div key={i} className="url-item">
                        <div className="url-status">
                          {page ? (
                            page.ok ? (
                              <CheckCircle2 size={14} style={{ color: '#10b981' }} />
                            ) : (
                              <AlertCircle size={14} style={{ color: '#ef4444' }} />
                            )
                          ) : (
                            <div className="url-pending" />
                          )}
                        </div>
                        <div className="url-info">
                          <div className="url-title">{sr.title || sr.url.substring(0, 60)}</div>
                          <div className="url-addr">
                            {sr.url.replace(/^https?:\/\//, '').substring(0, 70)}
                          </div>
                        </div>
                        <span className="url-source" style={{ color: srcColor, borderColor: srcColor }}>
                          {sr.source.replace('_', ' ')}
                        </span>
                        {page?.ok && (
                          <span className="url-chars">{(page.chars / 1000).toFixed(1)}K</span>
                        )}
                      </div>
                    );
                  })}
                </div>
              )}
            </div>
          )}

          {/* ——— Activity Feed ——— */}
          {events.length > 0 && (
            <div className="events-section">
              <div className="events-header">
                <Activity size={14} />
                <span>Лог активности</span>
                <span className="events-count">{events.length}</span>
              </div>
              <div className="events-list" ref={eventsRef}>
                {events.map((ev, i) => {
                  const color = PHASE_CONFIG[ev.phase]?.color || '#71717a';
                  return (
                    <div key={i} className="event-item">
                      <div className="event-dot" style={{ background: color }} />
                      <span className="event-msg">{ev.msg}</span>
                    </div>
                  );
                })}
              </div>
            </div>
          )}

          {/* ——— Empty State ——— */}
          {!isActive && !agentStatus?.phase?.match(/complete|error/) && !crawlResult && (
            <div className="empty-state">
              <div className="empty-icon-wrap">
                <div className="empty-globe-ring" />
                <Globe size={48} strokeWidth={1} />
              </div>
              <h3>Колибри Learning Agent</h3>
              <p>
                Введите тему — агент найдёт страницы в DuckDuckGo, Bing и Wikipedia,
                загрузит их, извлечёт текст и обучит модель автоматически
              </p>
              <div className="empty-engines">
                <span className="engine-badge ddg">DuckDuckGo</span>
                <span className="engine-badge bing">Bing</span>
                <span className="engine-badge wiki">Wikipedia</span>
              </div>
            </div>
          )}

          {/* ——— Complete banner ——— */}
          {phase === 'complete' && !isActive && (
            <div className="complete-banner">
              <div className="complete-icon">🎉</div>
              <div className="complete-text">
                <h3>Обучение завершено!</h3>
                <p>
                  Агент обработал {agentStatus?.urls_found} источников,
                  извлёк {agentStatus?.tokens.toLocaleString()} токенов и обучил
                  модель ({agentStatus?.model_size_mb} МБ) за {agentStatus?.time_elapsed.toFixed(1)} сек
                </p>
              </div>
              <button
                className="try-again-btn"
                onClick={() => {
                  setAgentStatus(null);
                  setTopic('');
                }}
              >
                <RefreshCw size={16} />
                Новая тема
              </button>
            </div>
          )}
        </div>
      </div>

      {/* ═══════ STYLES (Manus-style) ═══════ */}
      <style>{`
        .agent-tab {
          display: flex; flex-direction: column; height: 100%; overflow: hidden;
        }

        /* ——— Header ——— */
        .agent-header {
          padding: 16px 24px 14px;
          border-bottom: 1px solid var(--border-primary);
          display: flex; align-items: center; justify-content: space-between;
          background: var(--bg-secondary);
        }
        .agent-header-left {
          display: flex; align-items: center; gap: 14px;
        }
        .agent-logo {
          position: relative;
          width: 44px; height: 44px;
          border-radius: 12px;
          background: var(--accent-bg);
          display: flex; align-items: center; justify-content: center;
          color: var(--accent-primary);
        }
        .agent-logo-glow {
          position: absolute; inset: -3px;
          border-radius: 15px;
          background: transparent;
        }
        .agent-title {
          font-size: 18px; font-weight: 600; margin: 0; color: var(--text-primary);
        }
        .agent-title-accent {
          color: var(--text-secondary); font-weight: 400;
        }
        .agent-subtitle {
          font-size: 12px; color: var(--text-muted); margin: 2px 0 0;
        }

        /* Mode toggle */
        .mode-toggle {
          display: flex; gap: 2px;
          background: var(--bg-tertiary);
          border-radius: 8px; padding: 3px;
          border: 1px solid var(--border-primary);
        }
        .mode-toggle-btn {
          display: flex; align-items: center; gap: 6px;
          padding: 7px 14px;
          border: none; border-radius: 6px;
          background: transparent; color: var(--text-muted);
          font-size: 13px; font-weight: 500; cursor: pointer;
          transition: all 0.15s;
        }
        .mode-toggle-btn:hover { color: var(--text-primary); }
        .mode-toggle-btn.active {
          background: var(--bg-secondary); color: var(--text-primary);
        }

        /* ——— Body layout ——— */
        .agent-body {
          flex: 1; display: grid;
          grid-template-columns: 340px 1fr;
          overflow: hidden;
        }

        /* ——— Controls ——— */
        .agent-controls {
          padding: 16px;
          border-right: 1px solid var(--border-primary);
          overflow-y: auto;
          display: flex; flex-direction: column; gap: 14px;
          background: var(--bg-secondary);
        }
        .control-section,
        .presets {
          display: flex;
          flex-direction: column;
          gap: 6px;
        }
        .quick-guide {
          border: 1px solid var(--border-primary);
          border-radius: 10px;
          padding: 10px 12px;
          background: var(--bg-tertiary);
        }
        .quick-guide-title {
          display: flex;
          align-items: center;
          gap: 6px;
          font-size: 12px;
          color: var(--text-primary);
          font-weight: 600;
          margin-bottom: 6px;
        }
        .quick-guide ol {
          margin: 0;
          padding-left: 18px;
          display: grid;
          gap: 4px;
          color: var(--text-secondary);
          font-size: 12px;
          line-height: 1.4;
        }
        .control-label {
          display: flex; align-items: center; gap: 6px;
          font-size: 11px; font-weight: 600; color: var(--text-muted);
          text-transform: uppercase; letter-spacing: 0.5px;
          margin-bottom: 6px;
        }
        .topic-input-wrap {
          position: relative; display: flex; align-items: center;
        }
        .topic-icon {
          position: absolute; left: 12px; color: var(--text-dimmed);
        }
        .topic-input {
          width: 100%;
          padding: 12px 14px 12px 40px;
          background: var(--bg-input);
          border: 1px solid var(--border-primary);
          border-radius: 10px; color: var(--text-primary);
          font-size: 14px; outline: none;
          transition: border-color 0.15s;
          font-family: 'Inter', sans-serif;
        }
        .topic-input:focus { border-color: var(--accent-primary); }
        .topic-input::placeholder { color: var(--text-dimmed); }

        /* Presets */
        .preset-grid {
          display: grid; grid-template-columns: 1fr 1fr; gap: 4px;
        }
        .preset-btn {
          display: flex; align-items: center; gap: 6px;
          padding: 8px 10px;
          background: var(--bg-input);
          border: 1px solid var(--border-primary);
          border-radius: 8px; color: var(--text-secondary);
          font-size: 11px; cursor: pointer;
          transition: all 0.15s; text-align: left;
        }
        .preset-btn:hover:not(:disabled) {
          background: var(--bg-hover);
          border-color: var(--border-hover);
          color: var(--text-primary);
        }
        .preset-btn.active {
          background: var(--accent-bg);
          border-color: var(--border-accent);
          color: var(--accent-primary);
        }
        .preset-btn:disabled { opacity: 0.4; cursor: not-allowed; }

        /* Params */
        .agent-params {
          background: var(--bg-tertiary);
          border: 1px solid var(--border-primary);
          border-radius: 10px; padding: 12px;
          display: flex; flex-direction: column; gap: 10px;
        }
        .param-row {
          display: flex; justify-content: space-between; align-items: center;
        }
        .param-row label { font-size: 13px; color: var(--text-secondary); }
        .param-control { display: flex; align-items: center; gap: 10px; }
        .param-control input[type=range] {
          width: 100px; accent-color: var(--accent-primary);
        }
        .param-value {
          min-width: 28px; text-align: right;
          font-size: 14px; font-weight: 600; color: var(--accent-primary);
          font-variant-numeric: tabular-nums;
        }

        /* URL mode switch */
        .url-mode-switch {
          display: grid; grid-template-columns: 1fr 1fr; gap: 4px;
        }
        .mode-btn {
          display: flex; align-items: center; justify-content: center; gap: 8px;
          padding: 10px; background: var(--bg-input);
          border: 1px solid var(--border-primary);
          border-radius: 8px; color: var(--text-secondary);
          font-size: 12px; cursor: pointer; transition: all 0.15s;
        }
        .mode-btn:hover:not(:disabled) { background: var(--bg-hover); color: var(--text-primary); }
        .mode-btn.active {
          background: var(--accent-bg);
          border-color: var(--border-accent);
          color: var(--accent-primary);
        }

        /* Start button */
        .start-btn {
          display: flex; align-items: center; justify-content: center; gap: 10px;
          padding: 12px; border: none; border-radius: 10px;
          color: white; font-size: 14px; font-weight: 600;
          cursor: pointer; transition: all 0.2s;
          font-family: 'Inter', sans-serif;
        }
        .start-btn:disabled { opacity: 0.4; cursor: not-allowed; }
        .agent-start {
          background: var(--accent-primary);
        }
        .agent-start:hover:not(:disabled) {
          opacity: 0.9;
          transform: translateY(-1px);
        }
        .agent-stop {
          background: var(--error);
        }
        .agent-stop:hover {
          opacity: 0.9;
        }

        .spin { animation: spin 1s linear infinite; }
        @keyframes spin { to { transform: rotate(360deg); } }

        .error-banner {
          display: flex; align-items: center; gap: 8px;
          padding: 10px 12px;
          background: rgba(239,68,68,0.06);
          border: 1px solid rgba(239,68,68,0.2);
          border-radius: 10px; color: var(--error); font-size: 13px;
        }

        /* Model card */
        .model-card {
          background: var(--bg-tertiary);
          border: 1px solid var(--border-primary);
          border-radius: 10px; padding: 14px;
          margin-top: auto;
        }
        .model-header {
          display: flex; align-items: center; gap: 8px;
          color: var(--text-muted); font-size: 12px; font-weight: 600;
          margin-bottom: 12px;
        }
        .model-refresh {
          margin-left: auto; background: none; border: none;
          color: var(--text-dimmed); cursor: pointer; padding: 2px;
        }
        .model-refresh:hover { color: var(--text-primary); }
        .model-bars { display: flex; flex-direction: column; gap: 10px; }
        .bar-label {
          display: flex; justify-content: space-between;
          font-size: 11px; color: var(--text-muted); margin-bottom: 4px;
        }
        .bar-track {
          height: 5px; background: var(--bg-hover);
          border-radius: 3px; overflow: hidden;
        }
        .bar-fill { height: 100%; border-radius: 3px; transition: width 0.5s ease; }
        .bar-fill.patterns { background: var(--accent-primary); }
        .bar-fill.edges { background: var(--info); }
        .model-size {
          margin-top: 8px; text-align: right;
          font-size: 11px; color: var(--text-dimmed);
        }

        /* ——— Dashboard ——— */
        .agent-dashboard {
          padding: 16px;
          overflow-y: auto;
          display: flex; flex-direction: column; gap: 12px;
          background: var(--bg-primary);
        }

        /* Phase indicator */
        .phase-indicator {
          display: flex; align-items: center; justify-content: center;
          gap: 0; padding: 10px 0;
        }
        .phase-step-wrap {
          display: flex; align-items: center;
        }
        .phase-step {
          display: flex; align-items: center; gap: 8px;
          padding: 8px 14px;
          border-radius: 8px;
          background: var(--bg-secondary);
          border: 1px solid var(--border-primary);
          transition: all 0.3s;
        }
        .phase-step.current {
          background: var(--accent-bg);
          border-color: var(--border-accent);
        }
        .phase-step.done {
          background: rgba(34,197,94,0.06);
          border-color: rgba(34,197,94,0.15);
        }
        .phase-step.error {
          background: rgba(239,68,68,0.06);
          border-color: rgba(239,68,68,0.15);
        }
        .phase-dot { width: 16px; height: 16px; display: flex; align-items: center; justify-content: center; }
        .phase-dot svg { color: var(--success); }
        .phase-pulse {
          width: 10px; height: 10px; border-radius: 50%;
          background: var(--accent-primary);
          animation: phase-pulse-anim 1.5s ease-in-out infinite;
        }
        @keyframes phase-pulse-anim {
          0%, 100% { opacity: 1; transform: scale(1); }
          50% { opacity: 0.7; transform: scale(1.25); }
        }
        .phase-empty {
          width: 8px; height: 8px; border-radius: 50%;
          background: var(--border-primary);
        }
        .phase-label { font-size: 12px; color: var(--text-secondary); font-weight: 500; }
        .phase-step.current .phase-label { color: var(--accent-primary); }
        .phase-step.done .phase-label { color: var(--success); }
        .phase-connector {
          padding: 0 6px; color: var(--border-primary);
        }
        .phase-connector.done { color: var(--success); }

        /* Progress */
        .progress-section {
          background: var(--bg-secondary);
          border: 1px solid var(--border-primary);
          border-radius: 10px; padding: 14px;
        }
        .progress-header {
          display: flex; justify-content: space-between; margin-bottom: 8px;
        }
        .progress-label {
          font-size: 12px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
        }
        .progress-pct {
          font-size: 14px; font-weight: 700; color: var(--accent-primary);
          font-variant-numeric: tabular-nums;
        }
        .progress-bar {
          height: 4px; background: var(--bg-hover);
          border-radius: 2px; overflow: hidden;
        }
        .progress-fill {
          height: 100%; border-radius: 2px;
          transition: width 0.3s ease;
        }

        /* Stats grid */
        .stats-grid {
          display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px;
        }
        .stat-card {
          background: var(--bg-secondary);
          border: 1px solid var(--border-primary);
          border-radius: 10px; padding: 14px;
          display: flex; flex-direction: column; align-items: center; gap: 4px;
          text-align: center; color: var(--text-muted);
        }
        .stat-num {
          font-size: 18px; font-weight: 700; color: var(--text-primary);
          font-variant-numeric: tabular-nums;
        }
        .stat-fail { font-size: 12px; color: var(--error); font-weight: 400; }
        .stat-lbl {
          font-size: 10px; text-transform: uppercase; letter-spacing: 0.3px;
        }

        /* URLs section */
        .urls-section {
          background: var(--bg-secondary);
          border: 1px solid var(--border-primary);
          border-radius: 10px; overflow: hidden;
        }
        .urls-toggle {
          width: 100%; display: flex; align-items: center; gap: 8px;
          padding: 10px 14px;
          background: none; border: none;
          color: var(--text-secondary); font-size: 12px; font-weight: 600;
          cursor: pointer; text-align: left;
          font-family: 'Inter', sans-serif;
        }
        .urls-toggle:hover { color: var(--text-primary); }
        .urls-list {
          max-height: 260px; overflow-y: auto;
          border-top: 1px solid var(--border-primary);
        }
        .url-item {
          display: flex; align-items: center; gap: 10px;
          padding: 8px 14px;
          border-bottom: 1px solid var(--border-primary);
          font-size: 12px;
        }
        .url-item:hover { background: var(--bg-hover); }
        .url-status { width: 16px; flex-shrink: 0; }
        .url-pending {
          width: 8px; height: 8px; border-radius: 50%;
          background: var(--border-primary); margin: 3px;
        }
        .url-info { flex: 1; min-width: 0; }
        .url-title {
          color: var(--text-primary); font-weight: 500;
          overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
        }
        .url-addr {
          color: var(--text-dimmed); font-size: 11px;
          overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
        }
        .url-source {
          font-size: 10px; font-weight: 600;
          padding: 2px 6px; border: 1px solid;
          border-radius: 4px; flex-shrink: 0;
          text-transform: uppercase; letter-spacing: 0.3px;
        }
        .url-chars {
          color: var(--text-muted); font-size: 11px;
          font-variant-numeric: tabular-nums;
          flex-shrink: 0;
        }

        /* Events section */
        .events-section {
          background: var(--bg-secondary);
          border: 1px solid var(--border-primary);
          border-radius: 10px; overflow: hidden;
        }
        .events-header {
          display: flex; align-items: center; gap: 8px;
          padding: 10px 14px;
          background: var(--bg-tertiary);
          border-bottom: 1px solid var(--border-primary);
          font-size: 12px; color: var(--text-muted); font-weight: 600;
        }
        .events-count {
          margin-left: auto;
          background: var(--accent-bg);
          color: var(--accent-primary); padding: 1px 7px;
          border-radius: 8px; font-size: 10px;
        }
        .events-list {
          max-height: 250px; overflow-y: auto;
          padding: 8px 14px;
          font-family: 'JetBrains Mono', 'Fira Code', monospace;
          font-size: 11px; line-height: 1.7;
        }
        .event-item {
          display: flex; align-items: flex-start; gap: 8px;
        }
        .event-dot {
          width: 6px; height: 6px; border-radius: 50%;
          flex-shrink: 0; margin-top: 6px;
        }
        .event-msg { color: var(--text-secondary); }

        /* Empty state */
        .empty-state {
          display: flex; flex-direction: column; align-items: center; justify-content: center;
          height: 100%; text-align: center; padding: 40px;
        }
        .empty-icon-wrap {
          position: relative;
          width: 80px; height: 80px; border-radius: 20px;
          background: var(--accent-bg);
          display: flex; align-items: center; justify-content: center;
          color: var(--accent-primary); margin-bottom: 20px;
        }
        .empty-globe-ring {
          position: absolute; inset: -6px;
          border: 1px solid var(--border-primary);
          border-radius: 26px;
          animation: ring-spin 20s linear infinite;
        }
        @keyframes ring-spin {
          to { transform: rotate(360deg); }
        }
        .empty-state h3 {
          font-size: 18px; font-weight: 600; color: var(--text-primary); margin: 0 0 8px;
        }
        .empty-state p {
          font-size: 13px; color: var(--text-muted); max-width: 400px; line-height: 1.6;
          margin: 0 0 18px;
        }
        .empty-engines {
          display: flex; gap: 8px;
        }
        .engine-badge {
          padding: 5px 12px; border-radius: 6px;
          font-size: 11px; font-weight: 600;
          border: 1px solid;
        }
        .engine-badge.ddg { color: #de5833; border-color: rgba(222,88,51,0.2); background: rgba(222,88,51,0.06); }
        .engine-badge.bing { color: #00809d; border-color: rgba(0,128,157,0.2); background: rgba(0,128,157,0.06); }
        .engine-badge.wiki { color: #636466; border-color: rgba(99,100,102,0.2); background: rgba(99,100,102,0.06); }

        /* Complete banner */
        .complete-banner {
          display: flex; align-items: center; gap: 16px;
          padding: 18px 20px;
          background: rgba(34,197,94,0.04);
          border: 1px solid rgba(34,197,94,0.15);
          border-radius: 12px;
        }
        .complete-icon { font-size: 28px; }
        .complete-text { flex: 1; }
        .complete-text h3 {
          font-size: 16px; font-weight: 600; color: var(--success); margin: 0 0 4px;
        }
        .complete-text p { font-size: 13px; color: var(--text-secondary); margin: 0; line-height: 1.5; }
        .try-again-btn {
          display: flex; align-items: center; gap: 6px;
          padding: 10px 18px;
          background: var(--accent-bg);
          border: 1px solid var(--border-accent);
          border-radius: 8px; color: var(--accent-primary);
          font-size: 13px; font-weight: 600;
          cursor: pointer; flex-shrink: 0;
          transition: all 0.15s;
          font-family: 'Inter', sans-serif;
        }
        .try-again-btn:hover {
          background: var(--bg-hover);
        }

        @media (max-width: 980px) {
          .agent-header {
            padding: 14px;
            flex-direction: column;
            align-items: stretch;
            gap: 10px;
          }
          .agent-body {
            display: block;
            overflow-y: auto;
          }
          .agent-controls,
          .agent-dashboard {
            min-height: auto;
            border-right: none;
            padding: 12px;
          }
          .agent-controls {
            border-bottom: 1px solid var(--border-primary);
          }
          .phase-indicator {
            justify-content: flex-start;
            overflow-x: auto;
            padding-bottom: 6px;
          }
          .phase-step {
            min-width: max-content;
          }
        }

        @media (max-width: 760px) {
          .mode-toggle {
            width: 100%;
          }
          .mode-toggle-btn {
            flex: 1;
            justify-content: center;
          }
          .preset-grid,
          .url-mode-switch {
            grid-template-columns: 1fr;
          }
          .param-row {
            flex-direction: column;
            align-items: flex-start;
            gap: 8px;
          }
          .param-control {
            width: 100%;
          }
          .param-control input[type=range] {
            width: 100%;
          }
          .stats-grid {
            grid-template-columns: repeat(2, minmax(0, 1fr));
          }
          .empty-state {
            height: auto;
            min-height: 240px;
            padding: 24px 12px;
          }
          .complete-banner {
            flex-direction: column;
            align-items: flex-start;
          }
          .try-again-btn {
            width: 100%;
            justify-content: center;
          }
        }

        @media (max-width: 460px) {
          .agent-header-left {
            align-items: flex-start;
          }
          .agent-title {
            font-size: 16px;
          }
          .agent-subtitle {
            font-size: 11px;
          }
          .stats-grid {
            grid-template-columns: 1fr;
          }
          .progress-label {
            max-width: 220px;
          }
        }

        /* Scrollbars */
        .agent-controls::-webkit-scrollbar,
        .agent-dashboard::-webkit-scrollbar,
        .events-list::-webkit-scrollbar,
        .urls-list::-webkit-scrollbar {
          width: 4px;
        }
        .agent-controls::-webkit-scrollbar-thumb,
        .agent-dashboard::-webkit-scrollbar-thumb,
        .events-list::-webkit-scrollbar-thumb,
        .urls-list::-webkit-scrollbar-thumb {
          background: var(--scrollbar-thumb); border-radius: 2px;
        }
      `}</style>
    </div>
  );
};
