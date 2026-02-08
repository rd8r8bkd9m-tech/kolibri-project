/**
 * tabs/KnowledgeTab.tsx
 *
 * Полнофункциональная вкладка базы знаний.
 * Загружает реальные данные модели из backend,
 * позволяет запрашивать модель и просматривать статистику.
 */

import { useState, useEffect, useCallback } from 'react';
import {
  Search,
  Database,
  BookOpen,
  BarChart3,
  Loader2,
  RefreshCw,
  Send,
  FileText,
  Network,
  HardDrive,
  Zap,
  AlertCircle,
  TrendingUp,
} from 'lucide-react';

const API = '/api';

interface ModelStats {
  exists: boolean;
  path: string;
  size_mb: number;
  patterns: number;
  edges: number;
  max_patterns: number;
  max_edges: number;
}

interface ModelInfo {
  name: string;
  path: string;
  size_mb: number;
  modified: number;
}

interface QueryResult {
  status: string;
  query: string;
  results: string[];
  raw_output: string;
}

export const KnowledgeTab = () => {
  const [stats, setStats] = useState<ModelStats | null>(null);
  const [models, setModels] = useState<ModelInfo[]>([]);
  const [loading, setLoading] = useState(true);
  const [queryText, setQueryText] = useState('');
  const [queryResults, setQueryResults] = useState<QueryResult | null>(null);
  const [querying, setQuerying] = useState(false);
  const [activeView, setActiveView] = useState<'overview' | 'query' | 'models'>('overview');

  const fetchData = useCallback(async () => {
    setLoading(true);
    try {
      const [statsRes, modelsRes] = await Promise.all([
        fetch(`${API}/v1/model/stats`),
        fetch(`${API}/v1/model/list`),
      ]);
      const statsData: ModelStats = await statsRes.json();
      const modelsData: ModelInfo[] = await modelsRes.json();
      setStats(statsData);
      setModels(modelsData);
    } catch (e) {
      console.error('Ошибка загрузки данных:', e);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => { fetchData(); }, [fetchData]);

  const handleQuery = async () => {
    if (!queryText.trim() || querying) return;
    setQuerying(true);
    setQueryResults(null);
    try {
      const r = await fetch(`${API}/v1/model/query`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ query: queryText }),
      });
      const data: QueryResult = await r.json();
      setQueryResults(data);
    } catch (e) {
      setQueryResults({
        status: 'error',
        query: queryText,
        results: [`Ошибка: ${e}`],
        raw_output: '',
      });
    } finally {
      setQuerying(false);
    }
  };

  const patternsPercent = stats ? Math.round((stats.patterns / stats.max_patterns) * 100) : 0;
  const edgesPercent = stats ? Math.round((stats.edges / stats.max_edges) * 100) : 0;

  return (
    <div className="knowledge-tab">
      <div className="knowledge-header">
        <div className="knowledge-title-section">
          <h1 className="knowledge-title">База знаний</h1>
          {stats?.exists && (
            <span className="knowledge-badge">
              <Database size={14} />
              <span>{(stats.patterns ?? 0).toLocaleString()} паттернов</span>
            </span>
          )}
        </div>
        <button className="refresh-btn" onClick={fetchData} disabled={loading}>
          <RefreshCw size={18} className={loading ? 'spinning' : ''} />
          <span>Обновить</span>
        </button>
      </div>

      {/* Переключатель видов */}
      <div className="view-tabs">
        {([
          { key: 'overview' as const, label: 'Обзор', icon: BarChart3 },
          { key: 'query' as const, label: 'Запрос к модели', icon: Search },
          { key: 'models' as const, label: 'Модели', icon: HardDrive },
        ]).map(({ key, label, icon: Icon }) => (
          <button
            key={key}
            className={`view-tab ${activeView === key ? 'active' : ''}`}
            onClick={() => setActiveView(key)}
          >
            <Icon size={16} />
            <span>{label}</span>
          </button>
        ))}
      </div>

      {/* === ОБЗОР === */}
      {activeView === 'overview' && (
        <div className="knowledge-content">
          {loading ? (
            <div className="loading-state">
              <Loader2 size={32} className="spinning" />
              <p>Загрузка данных модели...</p>
            </div>
          ) : !stats?.exists ? (
            <div className="empty-state">
              <Database size={64} strokeWidth={1} />
              <h2>Модель не обучена</h2>
              <p>Перейдите на вкладку <strong>AI Агент</strong> и запустите обучение на тему.</p>
            </div>
          ) : (
            <>
              {/* Статистические карточки */}
              <div className="stats-grid">
                <div className="stat-card accent">
                  <div className="stat-icon"><Database size={24} /></div>
                  <div className="stat-data">
                    <div className="stat-value">{stats.patterns.toLocaleString()}</div>
                    <div className="stat-label">Паттернов</div>
                    <div className="stat-bar">
                      <div className="stat-bar-fill" style={{ width: `${patternsPercent}%` }} />
                    </div>
                    <div className="stat-hint">{patternsPercent}% от {stats.max_patterns.toLocaleString()}</div>
                  </div>
                </div>

                <div className="stat-card">
                  <div className="stat-icon"><Network size={24} /></div>
                  <div className="stat-data">
                    <div className="stat-value">{stats.edges.toLocaleString()}</div>
                    <div className="stat-label">Связей</div>
                    <div className="stat-bar">
                      <div className="stat-bar-fill blue" style={{ width: `${edgesPercent}%` }} />
                    </div>
                    <div className="stat-hint">{edgesPercent}% от {stats.max_edges.toLocaleString()}</div>
                  </div>
                </div>

                <div className="stat-card">
                  <div className="stat-icon"><HardDrive size={24} /></div>
                  <div className="stat-data">
                    <div className="stat-value">{stats.size_mb} МБ</div>
                    <div className="stat-label">Размер модели</div>
                    <div className="stat-bar">
                      <div className="stat-bar-fill green" style={{ width: `${Math.min(100, (stats.size_mb / 50) * 100)}%` }} />
                    </div>
                    <div className="stat-hint">Лимит: 50 МБ</div>
                  </div>
                </div>

                <div className="stat-card">
                  <div className="stat-icon"><TrendingUp size={24} /></div>
                  <div className="stat-data">
                    <div className="stat-value">
                      {stats.patterns > 0 ? (stats.edges / stats.patterns).toFixed(1) : '0'}
                    </div>
                    <div className="stat-label">Связей / паттерн</div>
                    <div className="stat-hint-text">
                      {stats.patterns > 0 && stats.edges / stats.patterns > 2 ? 'Плотная сеть знаний' : 'Разреженная сеть'}
                    </div>
                  </div>
                </div>
              </div>

              {/* Информация о файле */}
              <div className="model-file-info">
                <FileText size={16} />
                <span className="file-path">{stats.path}</span>
              </div>
            </>
          )}
        </div>
      )}

      {/* === ЗАПРОС К МОДЕЛИ === */}
      {activeView === 'query' && (
        <div className="knowledge-content">
          <div className="query-section">
            <div className="query-input-area">
              <Search size={18} className="query-icon" />
              <input
                type="text"
                className="query-input"
                placeholder="Введите запрос к модели..."
                value={queryText}
                onChange={(e) => setQueryText(e.target.value)}
                onKeyDown={(e) => e.key === 'Enter' && handleQuery()}
                disabled={querying}
              />
              <button
                className="query-btn"
                onClick={handleQuery}
                disabled={querying || !queryText.trim()}
              >
                {querying ? <Loader2 size={18} className="spinning" /> : <Send size={18} />}
              </button>
            </div>

            <div className="query-hints">
              <span className="hint-label">Примеры:</span>
              {['artificial intelligence', 'neural network', 'compression', 'deep learning'].map(q => (
                <button key={q} className="hint-btn" onClick={() => setQueryText(q)}>{q}</button>
              ))}
            </div>
          </div>

          {queryResults && (
            <div className="query-results">
              <div className="results-header">
                <BookOpen size={16} />
                <span>Результаты для «{queryResults.query}»</span>
                <span className={`results-status ${queryResults.status}`}>
                  {queryResults.status === 'ok' ? 'Найдено' : 'Ошибка'}
                </span>
              </div>

              {queryResults.results.length > 0 ? (
                <div className="results-list">
                  {queryResults.results.map((r, i) => (
                    <div key={i} className="result-item">
                      <Zap size={14} />
                      <span>{r}</span>
                    </div>
                  ))}
                </div>
              ) : (
                <div className="no-results">
                  <AlertCircle size={24} />
                  <p>Совпадений не найдено. Попробуйте другой запрос.</p>
                </div>
              )}

              {queryResults.raw_output && (
                <details className="raw-output">
                  <summary>Сырой вывод</summary>
                  <pre>{queryResults.raw_output}</pre>
                </details>
              )}
            </div>
          )}

          {!stats?.exists && (
            <div className="query-warning">
              <AlertCircle size={18} />
              <span>Модель не обучена. Запросы будут недоступны до обучения.</span>
            </div>
          )}
        </div>
      )}

      {/* === МОДЕЛИ === */}
      {activeView === 'models' && (
        <div className="knowledge-content">
          {models.length === 0 ? (
            <div className="empty-state">
              <HardDrive size={64} strokeWidth={1} />
              <h2>Нет обученных моделей</h2>
              <p>Обучите модель через вкладку AI Агент.</p>
            </div>
          ) : (
            <div className="models-grid">
              {models.map(m => (
                <div key={m.name} className="model-card">
                  <div className="model-card-icon">
                    <HardDrive size={24} />
                  </div>
                  <div className="model-card-info">
                    <div className="model-card-name">{m.name}</div>
                    <div className="model-card-size">{m.size_mb} МБ</div>
                    <div className="model-card-date">
                      {new Date(m.modified * 1000).toLocaleString('ru-RU')}
                    </div>
                  </div>
                </div>
              ))}
            </div>
          )}
        </div>
      )}

      <style>{`
        .knowledge-tab { display: flex; flex-direction: column; height: 100%; padding: 24px; overflow-y: auto; }

        .knowledge-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }
        .knowledge-title-section { display: flex; align-items: center; gap: 12px; }
        .knowledge-title { font-size: 28px; font-weight: 600; margin: 0; color: var(--text-primary); }
        .knowledge-badge { display: flex; align-items: center; gap: 6px; background: var(--accent-bg); color: var(--accent-primary); padding: 6px 12px; border-radius: 20px; font-size: 12px; }
        .refresh-btn { display: flex; align-items: center; gap: 8px; padding: 10px 16px; background: var(--bg-tertiary); border: 1px solid var(--border-primary); border-radius: 10px; color: var(--text-secondary); font-size: 14px; cursor: pointer; }
        .refresh-btn:hover { background: var(--bg-hover); color: var(--text-primary); }
        .refresh-btn:disabled { opacity: 0.5; }
        .spinning { animation: spin 1s linear infinite; }
        @keyframes spin { to { transform: rotate(360deg); } }

        .view-tabs { display: flex; gap: 8px; margin-bottom: 24px; }
        .view-tab { display: flex; align-items: center; gap: 6px; padding: 10px 16px; background: var(--bg-tertiary); border: 1px solid var(--border-primary); border-radius: 10px; color: var(--text-secondary); font-size: 13px; cursor: pointer; transition: all 0.15s ease; }
        .view-tab:hover { background: var(--bg-hover); color: var(--text-primary); }
        .view-tab.active { background: var(--accent-bg); border-color: var(--border-accent); color: var(--accent-primary); }

        .knowledge-content { flex: 1; }

        .loading-state, .empty-state { display: flex; flex-direction: column; align-items: center; justify-content: center; padding: 80px 40px; color: var(--text-dimmed); text-align: center; }
        .empty-state h2 { font-size: 20px; color: var(--text-primary); margin: 16px 0 8px; }
        .empty-state p { font-size: 14px; color: var(--text-muted); }
        .loading-state p { margin-top: 12px; font-size: 14px; }

        .stats-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 16px; margin-bottom: 20px; }
        .stat-card { background: var(--bg-card); border: 1px solid var(--border-primary); border-radius: 16px; padding: 20px; display: flex; gap: 16px; transition: all 0.15s ease; }
        .stat-card:hover { border-color: var(--border-hover); box-shadow: var(--shadow-card); }
        .stat-card.accent { border-color: var(--border-accent); }
        .stat-icon { width: 48px; height: 48px; border-radius: 12px; background: var(--accent-bg); color: var(--accent-primary); display: flex; align-items: center; justify-content: center; flex-shrink: 0; }
        .stat-data { flex: 1; min-width: 0; }
        .stat-value { font-size: 24px; font-weight: 700; color: var(--text-primary); margin-bottom: 2px; }
        .stat-label { font-size: 13px; color: var(--text-muted); margin-bottom: 8px; }
        .stat-bar { height: 4px; background: var(--bg-hover); border-radius: 2px; overflow: hidden; margin-bottom: 4px; }
        .stat-bar-fill { height: 100%; background: var(--accent-gradient); border-radius: 2px; transition: width 0.5s ease; }
        .stat-bar-fill.blue { background: linear-gradient(90deg, #3b82f6, #60a5fa); }
        .stat-bar-fill.green { background: linear-gradient(90deg, #22c55e, #4ade80); }
        .stat-hint { font-size: 11px; color: var(--text-dimmed); }
        .stat-hint-text { font-size: 11px; color: var(--text-dimmed); margin-top: 4px; }

        .model-file-info { display: flex; align-items: center; gap: 8px; padding: 12px 16px; background: var(--bg-tertiary); border: 1px solid var(--border-primary); border-radius: 8px; color: var(--text-muted); font-size: 12px; }
        .file-path { font-family: 'JetBrains Mono', monospace; color: var(--text-secondary); }

        .query-section { margin-bottom: 24px; }
        .query-input-area { display: flex; align-items: center; gap: 12px; padding: 12px 16px; background: var(--bg-card); border: 1px solid var(--border-primary); border-radius: 12px; }
        .query-input-area:focus-within { border-color: var(--accent-primary); }
        .query-icon { color: var(--text-dimmed); flex-shrink: 0; }
        .query-input { flex: 1; background: transparent; border: none; outline: none; color: var(--text-primary); font-size: 14px; }
        .query-input::placeholder { color: var(--text-faint); }
        .query-btn { width: 40px; height: 40px; border-radius: 10px; background: var(--accent-gradient); border: none; color: white; cursor: pointer; display: flex; align-items: center; justify-content: center; flex-shrink: 0; }
        .query-btn:disabled { opacity: 0.4; cursor: not-allowed; }
        .query-btn:hover:not(:disabled) { transform: scale(1.05); }

        .query-hints { display: flex; align-items: center; gap: 8px; margin-top: 12px; flex-wrap: wrap; }
        .hint-label { font-size: 12px; color: var(--text-dimmed); }
        .hint-btn { padding: 4px 10px; background: var(--bg-tertiary); border: 1px solid var(--border-primary); border-radius: 6px; color: var(--text-muted); font-size: 12px; cursor: pointer; }
        .hint-btn:hover { background: var(--bg-hover); color: var(--text-primary); }

        .query-results { background: var(--bg-card); border: 1px solid var(--border-primary); border-radius: 12px; overflow: hidden; }
        .results-header { display: flex; align-items: center; gap: 8px; padding: 14px 16px; border-bottom: 1px solid var(--border-primary); font-size: 14px; color: var(--text-secondary); }
        .results-status { margin-left: auto; padding: 2px 8px; border-radius: 4px; font-size: 11px; }
        .results-status.ok { background: rgba(34, 197, 94, 0.15); color: var(--success); }
        .results-status.error { background: rgba(239, 68, 68, 0.15); color: var(--error); }
        .results-list { padding: 8px; }
        .result-item { display: flex; align-items: flex-start; gap: 8px; padding: 10px 12px; border-radius: 8px; font-size: 13px; color: var(--text-primary); line-height: 1.5; }
        .result-item:hover { background: var(--bg-hover); }
        .result-item svg { color: var(--accent-primary); flex-shrink: 0; margin-top: 2px; }
        .no-results { display: flex; flex-direction: column; align-items: center; padding: 32px; color: var(--text-dimmed); }
        .no-results p { margin-top: 8px; font-size: 13px; }

        .raw-output { padding: 12px 16px; border-top: 1px solid var(--border-primary); }
        .raw-output summary { font-size: 12px; color: var(--text-muted); cursor: pointer; }
        .raw-output pre { margin-top: 8px; padding: 12px; background: var(--bg-overlay); border-radius: 6px; font-size: 11px; color: var(--text-secondary); overflow-x: auto; white-space: pre-wrap; font-family: 'JetBrains Mono', monospace; }

        .query-warning { display: flex; align-items: center; gap: 8px; padding: 12px 16px; background: rgba(245, 158, 11, 0.1); border: 1px solid rgba(245, 158, 11, 0.2); border-radius: 8px; color: var(--warning); font-size: 13px; margin-top: 16px; }

        .models-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(250px, 1fr)); gap: 16px; }
        .model-card { background: var(--bg-card); border: 1px solid var(--border-primary); border-radius: 12px; padding: 20px; display: flex; gap: 16px; transition: all 0.15s ease; }
        .model-card:hover { border-color: var(--border-hover); box-shadow: var(--shadow-card); }
        .model-card-icon { width: 48px; height: 48px; border-radius: 12px; background: var(--accent-bg); color: var(--accent-primary); display: flex; align-items: center; justify-content: center; flex-shrink: 0; }
        .model-card-info { min-width: 0; }
        .model-card-name { font-size: 14px; font-weight: 600; color: var(--text-primary); margin-bottom: 4px; word-break: break-all; }
        .model-card-size { font-size: 13px; color: var(--text-muted); margin-bottom: 4px; }
        .model-card-date { font-size: 11px; color: var(--text-dimmed); }
      `}</style>
    </div>
  );
};
