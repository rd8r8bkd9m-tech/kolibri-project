/**
 * tabs/CrawlerTab.tsx
 *
 * Kolibri AI Learning Agent — автономный агент обучения.
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

      const data: CrawlResult = await r.json();
      if (!r.ok) {
        setError((data as any).detail || 'Ошибка');
        setIsCrawling(false);
        return;
      }

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
              Kolibri <span className="agent-title-accent">Learning Agent</span>
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
                      style={{ width: `${(model.patterns / model.max_patterns) * 100}%` }}
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
                      style={{ width: `${(model.edges / model.max_edges) * 100}%` }}
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
              <h3>Kolibri Learning Agent</h3>
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

      {/* ═══════ STYLES ═══════ */}
      <style>{`
        .agent-tab {
          display: flex; flex-direction: column; height: 100%; overflow: hidden;
        }

        /* ——— Header ——— */
        .agent-header {
          padding: 20px 24px 16px;
          border-bottom: 1px solid rgba(255,255,255,0.06);
          display: flex; align-items: center; justify-content: space-between;
        }
        .agent-header-left {
          display: flex; align-items: center; gap: 16px;
        }
        .agent-logo {
          position: relative;
          width: 52px; height: 52px;
          border-radius: 16px;
          background: linear-gradient(135deg, rgba(16,185,129,0.2), rgba(59,130,246,0.2));
          display: flex; align-items: center; justify-content: center;
          color: #34d399;
        }
        .agent-logo-glow {
          position: absolute; inset: -4px;
          border-radius: 20px;
          background: radial-gradient(circle, rgba(52,211,153,0.15), transparent 70%);
          animation: pulse-glow 3s ease-in-out infinite;
        }
        @keyframes pulse-glow {
          0%, 100% { opacity: 0.5; transform: scale(1); }
          50% { opacity: 1; transform: scale(1.1); }
        }
        .agent-title {
          font-size: 22px; font-weight: 700; margin: 0; color: #fafafa;
        }
        .agent-title-accent {
          background: linear-gradient(135deg, #34d399, #60a5fa);
          -webkit-background-clip: text; -webkit-text-fill-color: transparent;
        }
        .agent-subtitle {
          font-size: 13px; color: #71717a; margin: 2px 0 0;
        }

        /* Mode toggle */
        .mode-toggle {
          display: flex; gap: 2px;
          background: rgba(39,39,42,0.6);
          border-radius: 10px; padding: 3px;
          border: 1px solid rgba(255,255,255,0.06);
        }
        .mode-toggle-btn {
          display: flex; align-items: center; gap: 6px;
          padding: 8px 16px;
          border: none; border-radius: 8px;
          background: transparent; color: #71717a;
          font-size: 13px; font-weight: 500; cursor: pointer;
          transition: all 0.15s;
        }
        .mode-toggle-btn:hover { color: #d4d4d8; }
        .mode-toggle-btn.active {
          background: rgba(52,211,153,0.15); color: #34d399;
        }

        /* ——— Body layout ——— */
        .agent-body {
          flex: 1; display: grid;
          grid-template-columns: 360px 1fr;
          overflow: hidden;
        }

        /* ——— Controls ——— */
        .agent-controls {
          padding: 18px;
          border-right: 1px solid rgba(255,255,255,0.06);
          overflow-y: auto;
          display: flex; flex-direction: column; gap: 16px;
        }
        .control-label {
          display: flex; align-items: center; gap: 6px;
          font-size: 11px; font-weight: 600; color: #71717a;
          text-transform: uppercase; letter-spacing: 0.5px;
          margin-bottom: 8px;
        }
        .topic-input-wrap {
          position: relative; display: flex; align-items: center;
        }
        .topic-icon {
          position: absolute; left: 14px; color: #52525b;
        }
        .topic-input {
          width: 100%;
          padding: 14px 16px 14px 44px;
          background: rgba(39,39,42,0.6);
          border: 1px solid rgba(255,255,255,0.08);
          border-radius: 12px; color: #fafafa;
          font-size: 14px; outline: none;
          transition: border-color 0.15s;
        }
        .topic-input:focus { border-color: rgba(52,211,153,0.5); }
        .topic-input::placeholder { color: #52525b; }

        /* Presets */
        .preset-grid {
          display: grid; grid-template-columns: 1fr 1fr; gap: 5px;
        }
        .preset-btn {
          display: flex; align-items: center; gap: 7px;
          padding: 9px 10px;
          background: rgba(39,39,42,0.4);
          border: 1px solid rgba(255,255,255,0.05);
          border-radius: 9px; color: #a1a1aa;
          font-size: 11px; cursor: pointer;
          transition: all 0.15s; text-align: left;
        }
        .preset-btn:hover:not(:disabled) {
          background: rgba(52,211,153,0.08);
          border-color: rgba(52,211,153,0.2);
          color: #d4d4d8;
        }
        .preset-btn.active {
          background: rgba(52,211,153,0.12);
          border-color: rgba(52,211,153,0.35);
          color: #34d399;
        }
        .preset-btn:disabled { opacity: 0.4; cursor: not-allowed; }

        /* Params */
        .agent-params {
          background: rgba(39,39,42,0.3);
          border: 1px solid rgba(255,255,255,0.04);
          border-radius: 10px; padding: 12px;
          display: flex; flex-direction: column; gap: 10px;
        }
        .param-row {
          display: flex; justify-content: space-between; align-items: center;
        }
        .param-row label { font-size: 13px; color: #a1a1aa; }
        .param-control { display: flex; align-items: center; gap: 10px; }
        .param-control input[type=range] {
          width: 100px; accent-color: #34d399;
        }
        .param-value {
          min-width: 28px; text-align: right;
          font-size: 14px; font-weight: 600; color: #34d399;
          font-variant-numeric: tabular-nums;
        }

        /* URL mode switch */
        .url-mode-switch {
          display: grid; grid-template-columns: 1fr 1fr; gap: 5px;
        }
        .mode-btn {
          display: flex; align-items: center; justify-content: center; gap: 8px;
          padding: 10px; background: rgba(39,39,42,0.4);
          border: 1px solid rgba(255,255,255,0.06);
          border-radius: 10px; color: #a1a1aa;
          font-size: 12px; cursor: pointer; transition: all 0.15s;
        }
        .mode-btn:hover:not(:disabled) { background: rgba(63,63,70,0.5); color: #fafafa; }
        .mode-btn.active {
          background: rgba(52,211,153,0.15);
          border-color: rgba(52,211,153,0.4);
          color: #34d399;
        }

        /* Start button */
        .start-btn {
          display: flex; align-items: center; justify-content: center; gap: 10px;
          padding: 14px; border: none; border-radius: 12px;
          color: white; font-size: 15px; font-weight: 600;
          cursor: pointer; transition: all 0.2s;
        }
        .start-btn:disabled { opacity: 0.4; cursor: not-allowed; }
        .agent-start {
          background: linear-gradient(135deg, #059669, #0d9488);
        }
        .agent-start:hover:not(:disabled) {
          transform: translateY(-1px);
          box-shadow: 0 6px 20px rgba(5,150,105,0.3);
        }
        .agent-stop {
          background: linear-gradient(135deg, #dc2626, #ef4444);
        }
        .agent-stop:hover {
          box-shadow: 0 6px 20px rgba(220,38,38,0.3);
        }

        .spin { animation: spin 1s linear infinite; }
        @keyframes spin { to { transform: rotate(360deg); } }

        .error-banner {
          display: flex; align-items: center; gap: 8px;
          padding: 10px 12px;
          background: rgba(239,68,68,0.1);
          border: 1px solid rgba(239,68,68,0.3);
          border-radius: 10px; color: #f87171; font-size: 13px;
        }

        /* Model card */
        .model-card {
          background: rgba(39,39,42,0.3);
          border: 1px solid rgba(255,255,255,0.05);
          border-radius: 10px; padding: 14px;
          margin-top: auto;
        }
        .model-header {
          display: flex; align-items: center; gap: 8px;
          color: #71717a; font-size: 12px; font-weight: 600;
          margin-bottom: 12px;
        }
        .model-refresh {
          margin-left: auto; background: none; border: none;
          color: #3f3f46; cursor: pointer; padding: 2px;
        }
        .model-refresh:hover { color: #fafafa; }
        .model-bars { display: flex; flex-direction: column; gap: 10px; }
        .bar-label {
          display: flex; justify-content: space-between;
          font-size: 11px; color: #71717a; margin-bottom: 4px;
        }
        .bar-track {
          height: 6px; background: rgba(255,255,255,0.06);
          border-radius: 3px; overflow: hidden;
        }
        .bar-fill { height: 100%; border-radius: 3px; transition: width 0.5s ease; }
        .bar-fill.patterns { background: linear-gradient(90deg, #6366f1, #818cf8); }
        .bar-fill.edges { background: linear-gradient(90deg, #8b5cf6, #c084fc); }
        .model-size {
          margin-top: 8px; text-align: right;
          font-size: 11px; color: #3f3f46;
        }

        /* ——— Dashboard ——— */
        .agent-dashboard {
          padding: 18px;
          overflow-y: auto;
          display: flex; flex-direction: column; gap: 14px;
        }

        /* Phase indicator */
        .phase-indicator {
          display: flex; align-items: center; justify-content: center;
          gap: 0; padding: 12px 0;
        }
        .phase-step-wrap {
          display: flex; align-items: center;
        }
        .phase-step {
          display: flex; align-items: center; gap: 8px;
          padding: 8px 14px;
          border-radius: 10px;
          background: rgba(39,39,42,0.3);
          border: 1px solid rgba(255,255,255,0.04);
          transition: all 0.3s;
        }
        .phase-step.current {
          background: rgba(52,211,153,0.1);
          border-color: rgba(52,211,153,0.3);
          box-shadow: 0 0 20px rgba(52,211,153,0.1);
        }
        .phase-step.done {
          background: rgba(16,185,129,0.08);
          border-color: rgba(16,185,129,0.2);
        }
        .phase-step.error {
          background: rgba(239,68,68,0.1);
          border-color: rgba(239,68,68,0.3);
        }
        .phase-dot { width: 16px; height: 16px; display: flex; align-items: center; justify-content: center; }
        .phase-dot svg { color: #10b981; }
        .phase-pulse {
          width: 10px; height: 10px; border-radius: 50%;
          background: #34d399;
          animation: phase-pulse-anim 1.5s ease-in-out infinite;
        }
        @keyframes phase-pulse-anim {
          0%, 100% { opacity: 1; box-shadow: 0 0 0 0 rgba(52,211,153,0.4); }
          50% { opacity: 0.7; box-shadow: 0 0 0 8px rgba(52,211,153,0); }
        }
        .phase-empty {
          width: 8px; height: 8px; border-radius: 50%;
          background: rgba(255,255,255,0.1);
        }
        .phase-label { font-size: 12px; color: #a1a1aa; font-weight: 500; }
        .phase-step.current .phase-label { color: #34d399; }
        .phase-step.done .phase-label { color: #10b981; }
        .phase-connector {
          padding: 0 6px; color: rgba(255,255,255,0.1);
        }
        .phase-connector.done { color: rgba(16,185,129,0.4); }

        /* Progress */
        .progress-section {
          background: rgba(39,39,42,0.3);
          border: 1px solid rgba(255,255,255,0.05);
          border-radius: 10px; padding: 14px;
        }
        .progress-header {
          display: flex; justify-content: space-between; margin-bottom: 8px;
        }
        .progress-label {
          font-size: 12px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
        }
        .progress-pct {
          font-size: 14px; font-weight: 700; color: #34d399;
          font-variant-numeric: tabular-nums;
        }
        .progress-bar {
          height: 5px; background: rgba(255,255,255,0.06);
          border-radius: 3px; overflow: hidden;
        }
        .progress-fill {
          height: 100%; border-radius: 3px;
          transition: width 0.3s ease;
        }

        /* Stats grid */
        .stats-grid {
          display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px;
        }
        .stat-card {
          background: rgba(39,39,42,0.4);
          border: 1px solid rgba(255,255,255,0.05);
          border-radius: 10px; padding: 14px;
          display: flex; flex-direction: column; align-items: center; gap: 4px;
          text-align: center; color: #52525b;
        }
        .stat-num {
          font-size: 18px; font-weight: 700; color: #fafafa;
          font-variant-numeric: tabular-nums;
        }
        .stat-fail { font-size: 12px; color: #ef4444; font-weight: 400; }
        .stat-lbl {
          font-size: 10px; text-transform: uppercase; letter-spacing: 0.3px;
        }

        /* URLs section */
        .urls-section {
          background: rgba(39,39,42,0.3);
          border: 1px solid rgba(255,255,255,0.05);
          border-radius: 10px; overflow: hidden;
        }
        .urls-toggle {
          width: 100%; display: flex; align-items: center; gap: 8px;
          padding: 10px 14px;
          background: none; border: none;
          color: #a1a1aa; font-size: 12px; font-weight: 600;
          cursor: pointer; text-align: left;
        }
        .urls-toggle:hover { color: #fafafa; }
        .urls-list {
          max-height: 260px; overflow-y: auto;
          border-top: 1px solid rgba(255,255,255,0.04);
        }
        .url-item {
          display: flex; align-items: center; gap: 10px;
          padding: 8px 14px;
          border-bottom: 1px solid rgba(255,255,255,0.02);
          font-size: 12px;
        }
        .url-item:hover { background: rgba(255,255,255,0.02); }
        .url-status { width: 16px; flex-shrink: 0; }
        .url-pending {
          width: 8px; height: 8px; border-radius: 50%;
          background: rgba(255,255,255,0.1); margin: 3px;
        }
        .url-info { flex: 1; min-width: 0; }
        .url-title {
          color: #d4d4d8; font-weight: 500;
          overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
        }
        .url-addr {
          color: #3f3f46; font-size: 11px;
          overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
        }
        .url-source {
          font-size: 10px; font-weight: 600;
          padding: 2px 6px; border: 1px solid;
          border-radius: 4px; flex-shrink: 0;
          text-transform: uppercase; letter-spacing: 0.3px;
        }
        .url-chars {
          color: #52525b; font-size: 11px;
          font-variant-numeric: tabular-nums;
          flex-shrink: 0;
        }

        /* Events section */
        .events-section {
          background: rgba(9,9,11,0.6);
          border: 1px solid rgba(255,255,255,0.05);
          border-radius: 10px; overflow: hidden;
        }
        .events-header {
          display: flex; align-items: center; gap: 8px;
          padding: 10px 14px;
          background: rgba(39,39,42,0.3);
          border-bottom: 1px solid rgba(255,255,255,0.04);
          font-size: 12px; color: #52525b; font-weight: 600;
        }
        .events-count {
          margin-left: auto;
          background: rgba(52,211,153,0.15);
          color: #34d399; padding: 1px 7px;
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
        .event-msg { color: #a1a1aa; }

        /* Empty state */
        .empty-state {
          display: flex; flex-direction: column; align-items: center; justify-content: center;
          height: 100%; text-align: center; padding: 40px;
        }
        .empty-icon-wrap {
          position: relative;
          width: 88px; height: 88px; border-radius: 28px;
          background: linear-gradient(135deg, rgba(52,211,153,0.1), rgba(59,130,246,0.1));
          display: flex; align-items: center; justify-content: center;
          color: #34d399; margin-bottom: 20px;
        }
        .empty-globe-ring {
          position: absolute; inset: -6px;
          border: 2px solid rgba(52,211,153,0.1);
          border-radius: 34px;
          animation: ring-spin 20s linear infinite;
        }
        @keyframes ring-spin {
          to { transform: rotate(360deg); }
        }
        .empty-state h3 {
          font-size: 20px; font-weight: 600; color: #d4d4d8; margin: 0 0 8px;
        }
        .empty-state p {
          font-size: 14px; color: #52525b; max-width: 400px; line-height: 1.6;
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
        .engine-badge.ddg { color: #de5833; border-color: rgba(222,88,51,0.3); background: rgba(222,88,51,0.08); }
        .engine-badge.bing { color: #00809d; border-color: rgba(0,128,157,0.3); background: rgba(0,128,157,0.08); }
        .engine-badge.wiki { color: #636466; border-color: rgba(99,100,102,0.3); background: rgba(99,100,102,0.08); }

        /* Complete banner */
        .complete-banner {
          display: flex; align-items: center; gap: 16px;
          padding: 18px 20px;
          background: linear-gradient(135deg, rgba(16,185,129,0.1), rgba(59,130,246,0.05));
          border: 1px solid rgba(16,185,129,0.2);
          border-radius: 12px;
        }
        .complete-icon { font-size: 32px; }
        .complete-text { flex: 1; }
        .complete-text h3 {
          font-size: 16px; font-weight: 600; color: #34d399; margin: 0 0 4px;
        }
        .complete-text p { font-size: 13px; color: #a1a1aa; margin: 0; line-height: 1.5; }
        .try-again-btn {
          display: flex; align-items: center; gap: 6px;
          padding: 10px 18px;
          background: rgba(52,211,153,0.15);
          border: 1px solid rgba(52,211,153,0.3);
          border-radius: 10px; color: #34d399;
          font-size: 13px; font-weight: 600;
          cursor: pointer; flex-shrink: 0;
          transition: all 0.15s;
        }
        .try-again-btn:hover {
          background: rgba(52,211,153,0.25);
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
          background: rgba(255,255,255,0.06); border-radius: 2px;
        }
      `}</style>
    </div>
  );
};
