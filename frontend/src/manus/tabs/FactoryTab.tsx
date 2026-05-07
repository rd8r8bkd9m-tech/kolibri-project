/**
 * tabs/FactoryTab.tsx
 *
 * Content Factory в основном интерфейсе: идеи, производство,
 * согласование, публикация и аналитика через реальные /api/factory endpoints.
 */

import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  BarChart3,
  CheckCircle2,
  Edit3,
  FileText,
  Loader2,
  Play,
  RefreshCw,
  Save,
  Search,
  Sparkles,
  Trash2,
  Video,
  X,
} from 'lucide-react';

type ContentStatus =
  | 'analysis'
  | 'idea_generation'
  | 'idea_approval'
  | 'production'
  | 'content_approval'
  | 'publishing'
  | 'analytics';

interface ContentItem {
  id: string;
  topic: string;
  status: ContentStatus;
  content: string;
  analysis_report: string;
  created_at: string;
  platform: string;
  views: number;
  engagement_rate: number;
  romi: number;
}

interface TrendInsight {
  id: string;
  niche: string;
  title: string;
  score: number;
  rationale: string;
  source: string;
  created_at: string;
}

interface VideoReference {
  id: string;
  niche: string;
  title: string;
  url: string;
  channel: string;
  views: number;
  engagement_rate: number;
  reason: string;
  created_at: string;
}

const STATUS_LABELS: Record<ContentStatus, string> = {
  analysis: 'Анализ',
  idea_generation: 'Идеи',
  idea_approval: 'Согласование идеи',
  production: 'Производство',
  content_approval: 'Согласование контента',
  publishing: 'Публикация',
  analytics: 'Аналитика',
};

const STATUS_ORDER: ContentStatus[] = [
  'analysis',
  'idea_generation',
  'idea_approval',
  'production',
  'content_approval',
  'publishing',
  'analytics',
];

const nextAction = (status: ContentStatus): { label: string; endpoint: string; icon: 'check' | 'play' | 'chart' } | null => {
  switch (status) {
    case 'idea_approval':
      return { label: 'Утвердить идею', endpoint: 'approve_idea', icon: 'check' };
    case 'production':
      return { label: 'Произвести', endpoint: 'produce', icon: 'play' };
    case 'content_approval':
      return { label: 'Утвердить контент', endpoint: 'approve_content', icon: 'check' };
    case 'publishing':
      return { label: 'Опубликовать', endpoint: 'publish', icon: 'play' };
    case 'analytics':
      return { label: 'Обновить аналитику', endpoint: 'refresh_analytics', icon: 'chart' };
    default:
      return null;
  }
};

const actionIcon = (icon: 'check' | 'play' | 'chart') => {
  if (icon === 'check') return <CheckCircle2 size={15} />;
  if (icon === 'chart') return <BarChart3 size={15} />;
  return <Play size={15} />;
};

const formatInt = (value: number): string => Math.round(value || 0).toLocaleString('ru-RU');
const formatDate = (value: string): string => {
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? 'нет даты' : date.toLocaleString('ru-RU');
};

export const FactoryTab = () => {
  const [items, setItems] = useState<ContentItem[]>([]);
  const [trends, setTrends] = useState<TrendInsight[]>([]);
  const [videos, setVideos] = useState<VideoReference[]>([]);
  const [niche, setNiche] = useState('Новости ИИ');
  const [loading, setLoading] = useState(true);
  const [busy, setBusy] = useState('');
  const [error, setError] = useState('');
  const [editing, setEditing] = useState<ContentItem | null>(null);
  const [draftContent, setDraftContent] = useState('');

  const fetchData = useCallback(async () => {
    setLoading(true);
    setError('');
    try {
      const [itemsRes, trendsRes, videosRes] = await Promise.all([
        fetch('/api/factory/items'),
        fetch(`/api/factory/trends?niche=${encodeURIComponent(niche)}`),
        fetch(`/api/factory/videos?niche=${encodeURIComponent(niche)}`),
      ]);
      if (!itemsRes.ok) throw new Error('items API недоступен');
      setItems(await itemsRes.json());
      if (trendsRes.ok) setTrends(await trendsRes.json());
      if (videosRes.ok) setVideos(await videosRes.json());
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setLoading(false);
    }
  }, [niche]);

  useEffect(() => {
    void fetchData();
  }, [fetchData]);

  const stats = useMemo(() => {
    const byStatus = new Map<ContentStatus, number>();
    items.forEach((item) => byStatus.set(item.status, (byStatus.get(item.status) ?? 0) + 1));
    return {
      total: items.length,
      production: (byStatus.get('production') ?? 0) + (byStatus.get('content_approval') ?? 0),
      published: byStatus.get('analytics') ?? 0,
      views: items.reduce((sum, item) => sum + item.views, 0),
    };
  }, [items]);

  const startCycle = async () => {
    setBusy('cycle');
    setError('');
    try {
      const response = await fetch('/api/factory/start_cycle', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ niche, count: 3 }),
      });
      if (!response.ok) throw new Error('Не удалось запустить цикл');
      await fetchData();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy('');
    }
  };

  const analyzeTrends = async () => {
    setBusy('trends');
    setError('');
    try {
      const response = await fetch('/api/factory/trends/analyze', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ niche, limit: 5 }),
      });
      if (!response.ok) throw new Error('Не удалось собрать тренды');
      setTrends(await response.json());
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy('');
    }
  };

  const findVideos = async () => {
    setBusy('videos');
    setError('');
    try {
      const response = await fetch('/api/factory/videos/best', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ niche, limit: 5 }),
      });
      if (!response.ok) throw new Error('Не удалось найти видео');
      setVideos(await response.json());
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy('');
    }
  };

  const runItemAction = async (item: ContentItem) => {
    const action = nextAction(item.status);
    if (!action) return;

    setBusy(item.id);
    setError('');
    try {
      const response = await fetch(`/api/factory/items/${item.id}/${action.endpoint}`, { method: 'POST' });
      if (!response.ok) {
        const data = await response.json().catch(() => null);
        throw new Error(typeof data?.detail === 'string' ? data.detail : 'Не удалось выполнить действие');
      }
      await fetchData();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy('');
    }
  };

  const deleteItem = async (item: ContentItem) => {
    setBusy(item.id);
    setError('');
    try {
      const response = await fetch(`/api/factory/items/${item.id}`, { method: 'DELETE' });
      if (!response.ok) throw new Error('Не удалось удалить задачу');
      if (editing?.id === item.id) setEditing(null);
      await fetchData();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy('');
    }
  };

  const openEditor = (item: ContentItem) => {
    setEditing(item);
    setDraftContent(item.content || item.analysis_report || '');
  };

  const saveEditor = async () => {
    if (!editing) return;
    setBusy('save');
    setError('');
    try {
      const response = await fetch(`/api/factory/items/${editing.id}/content`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ content: draftContent }),
      });
      if (!response.ok) throw new Error('Не удалось сохранить текст');
      setEditing(null);
      await fetchData();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy('');
    }
  };

  return (
    <div className="factory-tab">
      <header className="factory-header">
        <div>
          <div className="factory-kicker">Content Factory</div>
          <h1 className="factory-title">Фабрика</h1>
          <p className="factory-subtitle">Тренды, идеи, сценарии, согласование, публикация и аналитика подключены к backend.</p>
        </div>
        <button type="button" className="factory-refresh" onClick={() => void fetchData()} disabled={loading}>
          {loading ? <Loader2 size={18} className="factory-spin" /> : <RefreshCw size={18} />}
          <span>Обновить</span>
        </button>
      </header>

      <section className="factory-control">
        <div className="factory-input-wrap">
          <Search size={17} />
          <input value={niche} onChange={(event) => setNiche(event.target.value)} placeholder="Ниша контента" />
        </div>
        <button type="button" onClick={() => void startCycle()} disabled={Boolean(busy)}>
          {busy === 'cycle' ? <Loader2 size={16} className="factory-spin" /> : <Sparkles size={16} />}
          <span>Запустить цикл</span>
        </button>
        <button type="button" onClick={() => void analyzeTrends()} disabled={Boolean(busy)}>
          {busy === 'trends' ? <Loader2 size={16} className="factory-spin" /> : <BarChart3 size={16} />}
          <span>Тренды</span>
        </button>
        <button type="button" onClick={() => void findVideos()} disabled={Boolean(busy)}>
          {busy === 'videos' ? <Loader2 size={16} className="factory-spin" /> : <Video size={16} />}
          <span>Видео</span>
        </button>
      </section>

      {error && (
        <div className="factory-error">
          <X size={16} />
          <span>{error}</span>
        </div>
      )}

      <section className="factory-stats" aria-label="Сводка фабрики">
        <div><strong>{formatInt(stats.total)}</strong><span>задач</span></div>
        <div><strong>{formatInt(stats.production)}</strong><span>в производстве</span></div>
        <div><strong>{formatInt(stats.published)}</strong><span>в аналитике</span></div>
        <div><strong>{formatInt(stats.views)}</strong><span>просмотров</span></div>
      </section>

      <section className="factory-workflow" aria-label="Этапы">
        {STATUS_ORDER.map((status, index) => (
          <div key={status} className="factory-step">
            <span>{index + 1}</span>
            <strong>{STATUS_LABELS[status]}</strong>
          </div>
        ))}
      </section>

      <main className="factory-main">
        <section className="factory-items">
          <div className="factory-section-head">
            <FileText size={18} />
            <h2>Материалы</h2>
          </div>

          {loading ? (
            <div className="factory-empty"><Loader2 size={24} className="factory-spin" /> Загрузка...</div>
          ) : items.length === 0 ? (
            <div className="factory-empty">Материалов нет. Запустите цикл для выбранной ниши.</div>
          ) : (
            <div className="factory-list">
              {items.map((item) => {
                const action = nextAction(item.status);
                return (
                  <article key={item.id} className="factory-item">
                    <div className="factory-item-main">
                      <div className="factory-item-top">
                        <span className={`factory-status is-${item.status}`}>{STATUS_LABELS[item.status]}</span>
                        <span>{formatDate(item.created_at)}</span>
                      </div>
                      <h3>{item.topic}</h3>
                      <p>{item.content || item.analysis_report || 'Текст появится после производства.'}</p>
                      <div className="factory-item-metrics">
                        <span>{item.platform}</span>
                        <span>{formatInt(item.views)} views</span>
                        <span>{item.engagement_rate.toFixed(1)}% engagement</span>
                        <span>{item.romi.toFixed(1)}% ROMI</span>
                      </div>
                    </div>
                    <div className="factory-item-actions">
                      {action && (
                        <button type="button" onClick={() => void runItemAction(item)} disabled={busy === item.id}>
                          {busy === item.id ? <Loader2 size={15} className="factory-spin" /> : actionIcon(action.icon)}
                          <span>{action.label}</span>
                        </button>
                      )}
                      <button type="button" onClick={() => openEditor(item)}>
                        <Edit3 size={15} />
                        <span>Редактировать</span>
                      </button>
                      <button type="button" className="is-danger" onClick={() => void deleteItem(item)} disabled={busy === item.id}>
                        <Trash2 size={15} />
                      </button>
                    </div>
                  </article>
                );
              })}
            </div>
          )}
        </section>

        <aside className="factory-side">
          <section>
            <div className="factory-section-head">
              <BarChart3 size={18} />
              <h2>Тренды</h2>
            </div>
            {trends.length === 0 ? (
              <div className="factory-side-empty">Нажмите “Тренды”.</div>
            ) : (
              trends.slice(0, 5).map((trend) => (
                <div key={trend.id} className="factory-side-row">
                  <strong>{trend.title}</strong>
                  <span>{trend.score.toFixed(1)} / 100 · {trend.source}</span>
                  <p>{trend.rationale}</p>
                </div>
              ))
            )}
          </section>

          <section>
            <div className="factory-section-head">
              <Video size={18} />
              <h2>Видео-референсы</h2>
            </div>
            {videos.length === 0 ? (
              <div className="factory-side-empty">Нажмите “Видео”.</div>
            ) : (
              videos.slice(0, 5).map((video) => (
                <a key={video.id} className="factory-side-row" href={video.url} target="_blank" rel="noreferrer">
                  <strong>{video.title}</strong>
                  <span>{video.channel} · {formatInt(video.views)} views</span>
                  <p>{video.reason}</p>
                </a>
              ))
            )}
          </section>
        </aside>
      </main>

      {editing && (
        <div className="factory-modal" role="dialog" aria-modal="true" aria-label="Редактор материала">
          <div className="factory-modal-body">
            <div className="factory-modal-head">
              <div>
                <span>Редактор</span>
                <strong>{editing.topic}</strong>
              </div>
              <button type="button" onClick={() => setEditing(null)} aria-label="Закрыть">
                <X size={18} />
              </button>
            </div>
            <textarea value={draftContent} onChange={(event) => setDraftContent(event.target.value)} />
            <div className="factory-modal-actions">
              <button type="button" onClick={() => setEditing(null)}>Отмена</button>
              <button type="button" onClick={() => void saveEditor()} disabled={busy === 'save'}>
                {busy === 'save' ? <Loader2 size={16} className="factory-spin" /> : <Save size={16} />}
                <span>Сохранить</span>
              </button>
            </div>
          </div>
        </div>
      )}

      <style>{`
        .factory-tab { height: 100%; overflow: auto; padding: 24px; color: var(--text-primary); }
        .factory-header { display: flex; justify-content: space-between; gap: 16px; align-items: flex-start; margin-bottom: 18px; }
        .factory-kicker { color: var(--accent-primary); font-size: 12px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.08em; margin-bottom: 6px; }
        .factory-title { margin: 0; font-size: 30px; font-weight: 700; letter-spacing: 0; }
        .factory-subtitle { margin: 8px 0 0; max-width: 680px; color: var(--text-muted); font-size: 14px; line-height: 1.45; }
        .factory-refresh, .factory-control button, .factory-item-actions button, .factory-modal-actions button {
          min-height: 40px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-tertiary); color: var(--text-secondary);
          display: inline-flex; align-items: center; justify-content: center; gap: 8px; padding: 0 12px; cursor: pointer; font-weight: 600; font-size: 13px;
        }
        .factory-refresh:disabled, .factory-control button:disabled, .factory-item-actions button:disabled, .factory-modal-actions button:disabled { opacity: 0.55; cursor: wait; }
        .factory-control { display: grid; grid-template-columns: minmax(220px, 1fr) repeat(3, auto); gap: 8px; margin-bottom: 14px; }
        .factory-input-wrap { min-height: 40px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-input); display: flex; align-items: center; gap: 8px; padding: 0 12px; color: var(--text-muted); }
        .factory-input-wrap input { width: 100%; min-width: 0; border: 0; outline: 0; background: transparent; color: var(--text-primary); }
        .factory-error { min-height: 38px; border: 1px solid color-mix(in srgb, var(--error) 40%, var(--border-primary)); border-radius: 8px; color: var(--error); display: flex; align-items: center; gap: 8px; padding: 0 12px; margin-bottom: 12px; }
        .factory-stats { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 10px; margin-bottom: 14px; }
        .factory-stats div { border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-card); min-height: 72px; padding: 14px; display: grid; gap: 4px; }
        .factory-stats strong { font-size: 24px; }
        .factory-stats span { color: var(--text-muted); font-size: 12px; }
        .factory-workflow { display: grid; grid-template-columns: repeat(7, minmax(0, 1fr)); gap: 8px; margin-bottom: 16px; }
        .factory-step { border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-tertiary); min-height: 54px; display: flex; align-items: center; gap: 8px; padding: 8px; min-width: 0; }
        .factory-step span { width: 24px; height: 24px; border-radius: 50%; background: var(--accent-bg); color: var(--accent-primary); display: inline-flex; align-items: center; justify-content: center; font-size: 12px; flex-shrink: 0; }
        .factory-step strong { font-size: 12px; line-height: 1.2; overflow-wrap: anywhere; }
        .factory-main { display: grid; grid-template-columns: minmax(0, 1fr) minmax(280px, 360px); gap: 14px; align-items: start; }
        .factory-section-head { display: flex; align-items: center; gap: 8px; margin-bottom: 10px; }
        .factory-section-head h2 { margin: 0; font-size: 17px; }
        .factory-list { display: grid; gap: 10px; }
        .factory-item { border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-card); padding: 14px; display: grid; grid-template-columns: minmax(0, 1fr) auto; gap: 14px; }
        .factory-item-top { display: flex; justify-content: space-between; gap: 10px; color: var(--text-dimmed); font-size: 11px; margin-bottom: 8px; }
        .factory-status { border-radius: 999px; padding: 3px 8px; border: 1px solid var(--border-primary); color: var(--accent-primary); font-weight: 700; }
        .factory-item h3 { margin: 0 0 8px; font-size: 16px; letter-spacing: 0; }
        .factory-item p { margin: 0; color: var(--text-secondary); font-size: 13px; line-height: 1.45; max-height: 76px; overflow: hidden; }
        .factory-item-metrics { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 10px; color: var(--text-muted); font-size: 11px; }
        .factory-item-metrics span { border: 1px solid var(--border-primary); border-radius: 999px; padding: 3px 8px; }
        .factory-item-actions { display: flex; flex-direction: column; gap: 8px; align-items: stretch; min-width: 160px; }
        .factory-item-actions .is-danger { color: var(--error); min-width: 40px; }
        .factory-side { display: grid; gap: 14px; }
        .factory-side section { border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-card); padding: 14px; }
        .factory-side-row { display: grid; gap: 4px; padding: 10px 0; border-top: 1px solid var(--border-primary); color: inherit; text-decoration: none; }
        .factory-side-row:first-of-type { border-top: 0; }
        .factory-side-row strong { font-size: 13px; line-height: 1.3; }
        .factory-side-row span { color: var(--text-muted); font-size: 12px; }
        .factory-side-row p { margin: 0; color: var(--text-secondary); font-size: 12px; line-height: 1.35; }
        .factory-empty, .factory-side-empty { border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-card); color: var(--text-muted); min-height: 120px; display: flex; align-items: center; justify-content: center; gap: 8px; padding: 16px; text-align: center; }
        .factory-side-empty { min-height: 76px; }
        .factory-modal { position: fixed; inset: 0; z-index: 100; background: rgba(0,0,0,.62); display: flex; align-items: center; justify-content: center; padding: 18px; }
        .factory-modal-body { width: min(820px, 100%); max-height: 88vh; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-secondary); display: grid; grid-template-rows: auto minmax(260px, 1fr) auto; overflow: hidden; }
        .factory-modal-head { display: flex; justify-content: space-between; align-items: center; gap: 12px; padding: 14px; border-bottom: 1px solid var(--border-primary); }
        .factory-modal-head div { display: grid; gap: 3px; min-width: 0; }
        .factory-modal-head span { color: var(--text-muted); font-size: 12px; }
        .factory-modal-head strong { overflow-wrap: anywhere; }
        .factory-modal-head button { width: 36px; height: 36px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-tertiary); color: var(--text-secondary); display: inline-flex; align-items: center; justify-content: center; }
        .factory-modal textarea { width: 100%; height: 100%; min-height: 300px; border: 0; resize: none; outline: none; background: var(--bg-input); color: var(--text-primary); padding: 14px; font: inherit; line-height: 1.5; }
        .factory-modal-actions { display: flex; justify-content: flex-end; gap: 8px; padding: 14px; border-top: 1px solid var(--border-primary); }
        .factory-spin { animation: factory-spin 1s linear infinite; }
        @keyframes factory-spin { to { transform: rotate(360deg); } }
        @media (max-width: 1100px) {
          .factory-main { grid-template-columns: 1fr; }
          .factory-workflow { grid-template-columns: repeat(4, minmax(0, 1fr)); }
        }
        @media (max-width: 760px) {
          .factory-tab { padding: 16px; }
          .factory-header { flex-direction: column; }
          .factory-refresh { width: 100%; }
          .factory-control, .factory-stats, .factory-workflow { grid-template-columns: 1fr; }
          .factory-item { grid-template-columns: 1fr; }
          .factory-item-actions { min-width: 0; }
        }
      `}</style>
    </div>
  );
};
