/**
 * tabs/SettingsTab.tsx
 *
 * Полнофункциональные настройки с рабочим переключением тем,
 * сохранением в localStorage и реальными данными из backend.
 */

import { useState, useEffect, useCallback } from 'react';
import {
  Settings,
  Palette,
  Zap,
  Database,
  Globe,
  Moon,
  Sun,
  Monitor,
  ChevronRight,
  Save,
  RefreshCw,
  Check,
  HardDrive,
  Trash2,
  RotateCcw,
} from 'lucide-react';
import { useTheme, type ThemeMode } from '../ThemeContext';

const API = '/api';

interface AppSettings {
  language: string;
  autoSave: boolean;
  defaultSearchEngines: { ddg: boolean; wiki: boolean; bing: boolean };
  maxCrawlPages: number;
  crawlDelay: number;
  defaultModel: string;
}

const DEFAULT_SETTINGS: AppSettings = {
  language: 'ru',
  autoSave: true,
  defaultSearchEngines: { ddg: true, wiki: true, bing: true },
  maxCrawlPages: 20,
  crawlDelay: 0.3,
  defaultModel: 'kolibri_web.klm',
};

const STORAGE_KEY = 'kolibri-settings';

function loadSettings(): AppSettings {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (raw) return { ...DEFAULT_SETTINGS, ...JSON.parse(raw) };
  } catch {}
  return { ...DEFAULT_SETTINGS };
}

function saveSettings(s: AppSettings) {
  try { localStorage.setItem(STORAGE_KEY, JSON.stringify(s)); } catch {}
}

interface SectionDef {
  id: string;
  title: string;
  icon: React.ElementType;
  description: string;
}

const SECTIONS: SectionDef[] = [
  { id: 'appearance', title: 'Внешний вид', icon: Palette, description: 'Тема и оформление' },
  { id: 'general', title: 'Общие', icon: Settings, description: 'Основные настройки' },
  { id: 'ai', title: 'AI Агент', icon: Zap, description: 'Поиск и обучение' },
  { id: 'storage', title: 'Хранилище', icon: Database, description: 'Модели и данные' },
];

export const SettingsTab = () => {
  const { mode: themeMode, setMode: setThemeMode } = useTheme();
  const [activeSection, setActiveSection] = useState('appearance');
  const [settings, setSettings] = useState<AppSettings>(loadSettings);
  const [saved, setSaved] = useState(false);
  const [models, setModels] = useState<Array<{ name: string; size_mb: number; path: string }>>([]);
  const [storageInfo, setStorageInfo] = useState({ total_mb: 0, used_mb: 0 });

  const fetchModels = useCallback(async () => {
    try {
      const r = await fetch(`${API}/v1/model/list`);
      const data: Array<{ name: string; size_mb: number; path: string }> = await r.json();
      setModels(data);
      const used = data.reduce((sum, m) => sum + m.size_mb, 0);
      setStorageInfo({ total_mb: 50, used_mb: Math.round(used * 100) / 100 });
    } catch {}
  }, []);

  useEffect(() => { fetchModels(); }, [fetchModels]);

  const updateSetting = <K extends keyof AppSettings>(key: K, value: AppSettings[K]) => {
    setSettings(prev => {
      const next = { ...prev, [key]: value };
      if (settings.autoSave) saveSettings(next);
      return next;
    });
  };

  const handleSave = () => {
    saveSettings(settings);
    setSaved(true);
    setTimeout(() => setSaved(false), 2000);
  };

  const handleReset = () => {
    setSettings({ ...DEFAULT_SETTINGS });
    setThemeMode('dark');
    saveSettings(DEFAULT_SETTINGS);
    localStorage.removeItem('kolibri-theme');
  };

  const handleDeleteModel = async (name: string) => {
    try {
      await fetch(`${API}/v1/model/${name}`, { method: 'DELETE' });
      fetchModels();
    } catch {}
  };

  return (
    <div className="settings-tab">
      <div className="settings-header">
        <div className="settings-title-section">
          <h1 className="settings-title">Настройки</h1>
          <span className="settings-version">Kolibri OS v0.4.2</span>
        </div>
        <div className="settings-actions">
          <button className="s-action-btn secondary" onClick={handleReset}>
            <RefreshCw size={16} />
            <span>Сбросить</span>
          </button>
          <button className="s-action-btn primary" onClick={handleSave}>
            {saved ? <Check size={16} /> : <Save size={16} />}
            <span>{saved ? 'Сохранено!' : 'Сохранить'}</span>
          </button>
        </div>
      </div>

      <div className="settings-content">
        <div className="settings-sidebar">
          {SECTIONS.map(section => {
            const Icon = section.icon;
            return (
              <button
                key={section.id}
                className={`section-btn ${activeSection === section.id ? 'active' : ''}`}
                onClick={() => setActiveSection(section.id)}
              >
                <Icon size={18} />
                <div className="section-info">
                  <span className="section-title">{section.title}</span>
                  <span className="section-desc">{section.description}</span>
                </div>
                <ChevronRight size={16} className="section-arrow" />
              </button>
            );
          })}
        </div>

        <div className="settings-panel">
          {/* === ВНЕШНИЙ ВИД === */}
          {activeSection === 'appearance' && (
            <div className="settings-group">
              <h2 className="group-title">Внешний вид</h2>

              <div className="setting-item">
                <div className="setting-info">
                  <Palette size={18} />
                  <div>
                    <div className="setting-name">Тема оформления</div>
                    <div className="setting-desc">Выберите цветовую схему интерфейса</div>
                  </div>
                </div>
                <div className="theme-selector">
                  {([
                    { id: 'light' as ThemeMode, icon: Sun, label: 'Светлая' },
                    { id: 'dark' as ThemeMode, icon: Moon, label: 'Тёмная' },
                    { id: 'system' as ThemeMode, icon: Monitor, label: 'Системная' },
                  ]).map(theme => (
                    <button
                      key={theme.id}
                      className={`theme-btn ${themeMode === theme.id ? 'active' : ''}`}
                      onClick={() => setThemeMode(theme.id)}
                    >
                      <theme.icon size={16} />
                      <span>{theme.label}</span>
                    </button>
                  ))}
                </div>
              </div>

              {/* Превью темы */}
              <div className="theme-preview">
                <div className="preview-header">Предпросмотр</div>
                <div className="preview-body">
                  <div className="preview-sidebar-demo">
                    <div className="preview-nav-item active">Чат</div>
                    <div className="preview-nav-item">Агент</div>
                    <div className="preview-nav-item">Задачи</div>
                  </div>
                  <div className="preview-content-demo">
                    <div className="preview-card">
                      <div className="preview-text-title">Kolibri AI</div>
                      <div className="preview-text-body">Пример интерфейса</div>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          )}

          {/* === ОБЩИЕ === */}
          {activeSection === 'general' && (
            <div className="settings-group">
              <h2 className="group-title">Общие настройки</h2>

              <div className="setting-item">
                <div className="setting-info">
                  <Globe size={18} />
                  <div>
                    <div className="setting-name">Язык интерфейса</div>
                    <div className="setting-desc">Выберите язык системы</div>
                  </div>
                </div>
                <select
                  className="setting-select"
                  value={settings.language}
                  onChange={(e) => updateSetting('language', e.target.value)}
                >
                  <option value="ru">Русский</option>
                  <option value="en">English</option>
                </select>
              </div>

              <div className="setting-item">
                <div className="setting-info">
                  <Save size={18} />
                  <div>
                    <div className="setting-name">Автосохранение</div>
                    <div className="setting-desc">Автоматически сохранять настройки при изменении</div>
                  </div>
                </div>
                <label className="toggle">
                  <input
                    type="checkbox"
                    checked={settings.autoSave}
                    onChange={(e) => updateSetting('autoSave', e.target.checked)}
                  />
                  <span className="toggle-slider"></span>
                </label>
              </div>
            </div>
          )}

          {/* === AI АГЕНТ === */}
          {activeSection === 'ai' && (
            <div className="settings-group">
              <h2 className="group-title">AI Агент</h2>

              <div className="setting-item">
                <div className="setting-info">
                  <Globe size={18} />
                  <div>
                    <div className="setting-name">Поисковые движки</div>
                    <div className="setting-desc">Какие движки использовать при автопоиске</div>
                  </div>
                </div>
                <div className="engine-toggles">
                  {([
                    { key: 'ddg' as const, label: 'DuckDuckGo' },
                    { key: 'wiki' as const, label: 'Wikipedia' },
                    { key: 'bing' as const, label: 'Bing' },
                  ]).map(engine => (
                    <label key={engine.key} className="engine-toggle">
                      <input
                        type="checkbox"
                        checked={settings.defaultSearchEngines[engine.key]}
                        onChange={(e) => updateSetting('defaultSearchEngines', {
                          ...settings.defaultSearchEngines,
                          [engine.key]: e.target.checked,
                        })}
                      />
                      <span>{engine.label}</span>
                    </label>
                  ))}
                </div>
              </div>

              <div className="setting-item">
                <div className="setting-info">
                  <Zap size={18} />
                  <div>
                    <div className="setting-name">Макс. страниц при краулинге</div>
                    <div className="setting-desc">Лимит страниц для обхода в режиме агента</div>
                  </div>
                </div>
                <input
                  type="number"
                  className="setting-input"
                  value={settings.maxCrawlPages}
                  onChange={(e) => updateSetting('maxCrawlPages', Math.max(1, Math.min(200, parseInt(e.target.value) || 1)))}
                  min={1}
                  max={200}
                />
              </div>

              <div className="setting-item">
                <div className="setting-info">
                  <RotateCcw size={18} />
                  <div>
                    <div className="setting-name">Задержка между запросами (сек)</div>
                    <div className="setting-desc">Пауза между загрузками страниц</div>
                  </div>
                </div>
                <div className="slider-container">
                  <input
                    type="range"
                    className="setting-slider"
                    value={settings.crawlDelay}
                    onChange={(e) => updateSetting('crawlDelay', parseFloat(e.target.value))}
                    min={0}
                    max={3}
                    step={0.1}
                  />
                  <span className="slider-value">{settings.crawlDelay.toFixed(1)}с</span>
                </div>
              </div>
            </div>
          )}

          {/* === ХРАНИЛИЩЕ === */}
          {activeSection === 'storage' && (
            <div className="settings-group">
              <h2 className="group-title">Хранилище</h2>

              <div className="storage-stats">
                <div className="storage-bar">
                  <div
                    className="storage-used"
                    style={{ width: `${storageInfo.total_mb > 0 ? Math.min(100, (storageInfo.used_mb / storageInfo.total_mb) * 100) : 0}%` }}
                  />
                </div>
                <div className="storage-info-row">
                  <span>Использовано: {storageInfo.used_mb} МБ</span>
                  <span>Лимит модели: {storageInfo.total_mb} МБ</span>
                </div>
              </div>

              <div className="models-list">
                <div className="models-header">
                  <HardDrive size={16} />
                  <span>Обученные модели ({models.length})</span>
                </div>
                {models.length === 0 ? (
                  <div className="models-empty">
                    <Database size={32} strokeWidth={1} />
                    <p>Моделей пока нет. Обучите через вкладку AI Агент.</p>
                  </div>
                ) : (
                  models.map(m => (
                    <div key={m.name} className="model-item">
                      <div className="model-info-row">
                        <HardDrive size={16} />
                        <div>
                          <div className="model-name">{m.name}</div>
                          <div className="model-size">{m.size_mb} МБ</div>
                        </div>
                      </div>
                      <button
                        className="model-delete-btn"
                        onClick={() => handleDeleteModel(m.name)}
                        title="Удалить модель"
                      >
                        <Trash2 size={14} />
                      </button>
                    </div>
                  ))
                )}
              </div>
            </div>
          )}
        </div>
      </div>

      <style>{`
        .settings-tab { display: flex; flex-direction: column; height: 100%; padding: 24px; overflow: hidden; }
        .settings-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 24px; }
        .settings-title-section { display: flex; align-items: baseline; gap: 12px; }
        .settings-title { font-size: 28px; font-weight: 600; margin: 0; color: var(--text-primary); }
        .settings-version { font-size: 12px; color: var(--text-dimmed); }
        .settings-actions { display: flex; gap: 8px; }
        .s-action-btn { display: flex; align-items: center; gap: 6px; padding: 10px 16px; border-radius: 8px; font-size: 13px; font-weight: 500; cursor: pointer; transition: all 0.15s ease; }
        .s-action-btn.secondary { background: var(--bg-tertiary); border: 1px solid var(--border-primary); color: var(--text-secondary); }
        .s-action-btn.secondary:hover { background: var(--bg-hover); color: var(--text-primary); }
        .s-action-btn.primary { background: var(--accent-gradient); border: none; color: white; }
        .s-action-btn.primary:hover { transform: translateY(-1px); box-shadow: var(--shadow-elevated); }
        .settings-content { display: flex; gap: 24px; flex: 1; overflow: hidden; }
        .settings-sidebar { width: 280px; flex-shrink: 0; display: flex; flex-direction: column; gap: 4px; }
        .section-btn { display: flex; align-items: center; gap: 12px; padding: 14px 16px; background: transparent; border: 1px solid transparent; border-radius: 10px; color: var(--text-secondary); cursor: pointer; text-align: left; transition: all 0.15s ease; }
        .section-btn:hover { background: var(--bg-tertiary); }
        .section-btn.active { background: var(--accent-bg); border-color: var(--border-accent); color: var(--text-primary); }
        .section-btn.active .section-arrow { color: var(--accent-primary); }
        .section-info { flex: 1; min-width: 0; }
        .section-title { display: block; font-size: 14px; font-weight: 500; margin-bottom: 2px; }
        .section-desc { display: block; font-size: 11px; color: var(--text-dimmed); }
        .section-arrow { color: var(--text-faint); }
        .settings-panel { flex: 1; background: var(--bg-card); border: 1px solid var(--border-primary); border-radius: 16px; padding: 24px; overflow-y: auto; }
        .settings-group { max-width: 600px; }
        .group-title { font-size: 18px; font-weight: 600; color: var(--text-primary); margin: 0 0 24px 0; padding-bottom: 12px; border-bottom: 1px solid var(--border-primary); }

        .setting-item { display: flex; align-items: center; justify-content: space-between; padding: 16px 0; border-bottom: 1px solid var(--border-primary); gap: 16px; }
        .setting-info { display: flex; align-items: center; gap: 12px; color: var(--text-muted); min-width: 0; flex: 1; }
        .setting-name { font-size: 14px; font-weight: 500; color: var(--text-primary); margin-bottom: 2px; }
        .setting-desc { font-size: 12px; color: var(--text-dimmed); }
        .setting-select, .setting-input { padding: 8px 12px; background: var(--bg-overlay); border: 1px solid var(--border-hover); border-radius: 6px; color: var(--text-primary); font-size: 13px; min-width: 100px; }
        .setting-select:focus, .setting-input:focus { outline: none; border-color: var(--accent-primary); }

        .toggle { position: relative; width: 44px; height: 24px; cursor: pointer; flex-shrink: 0; }
        .toggle input { opacity: 0; width: 0; height: 0; }
        .toggle-slider { position: absolute; inset: 0; background: var(--bg-hover); border-radius: 12px; transition: all 0.2s ease; }
        .toggle-slider::before { content: ''; position: absolute; width: 18px; height: 18px; left: 3px; top: 3px; background: var(--text-primary); border-radius: 50%; transition: all 0.2s ease; }
        .toggle input:checked + .toggle-slider { background: var(--accent-gradient); }
        .toggle input:checked + .toggle-slider::before { transform: translateX(20px); }

        .theme-selector { display: flex; gap: 8px; flex-shrink: 0; }
        .theme-btn { display: flex; align-items: center; gap: 6px; padding: 10px 14px; background: var(--bg-overlay); border: 2px solid var(--border-primary); border-radius: 10px; color: var(--text-muted); font-size: 13px; cursor: pointer; transition: all 0.2s ease; }
        .theme-btn:hover { background: var(--bg-hover); color: var(--text-secondary); }
        .theme-btn.active { background: var(--accent-bg); border-color: var(--accent-primary); color: var(--accent-primary); box-shadow: 0 0 0 1px var(--accent-primary); }

        .theme-preview { margin-top: 20px; border: 1px solid var(--border-primary); border-radius: 12px; overflow: hidden; }
        .preview-header { padding: 8px 12px; background: var(--bg-tertiary); font-size: 11px; color: var(--text-dimmed); text-transform: uppercase; letter-spacing: 0.5px; }
        .preview-body { display: flex; height: 120px; }
        .preview-sidebar-demo { width: 80px; background: var(--bg-secondary); border-right: 1px solid var(--border-primary); padding: 8px; display: flex; flex-direction: column; gap: 4px; }
        .preview-nav-item { padding: 4px 8px; border-radius: 4px; font-size: 10px; color: var(--text-secondary); }
        .preview-nav-item.active { background: var(--accent-bg); color: var(--accent-primary); }
        .preview-content-demo { flex: 1; padding: 12px; display: flex; align-items: center; justify-content: center; background: var(--bg-primary); }
        .preview-card { background: var(--bg-card); border: 1px solid var(--border-primary); border-radius: 8px; padding: 12px; text-align: center; }
        .preview-text-title { font-size: 13px; font-weight: 600; color: var(--text-primary); margin-bottom: 4px; }
        .preview-text-body { font-size: 11px; color: var(--text-muted); }

        .engine-toggles { display: flex; flex-direction: column; gap: 8px; flex-shrink: 0; }
        .engine-toggle { display: flex; align-items: center; gap: 8px; font-size: 13px; color: var(--text-secondary); cursor: pointer; }
        .engine-toggle input { accent-color: var(--accent-primary); width: 16px; height: 16px; }

        .slider-container { display: flex; align-items: center; gap: 12px; flex-shrink: 0; }
        .setting-slider { width: 120px; height: 4px; -webkit-appearance: none; background: var(--bg-hover); border-radius: 2px; outline: none; }
        .setting-slider::-webkit-slider-thumb { -webkit-appearance: none; width: 16px; height: 16px; background: var(--accent-gradient); border-radius: 50%; cursor: pointer; }
        .slider-value { min-width: 40px; text-align: right; font-size: 13px; color: var(--text-secondary); font-family: monospace; }

        .storage-stats { margin-bottom: 24px; }
        .storage-bar { height: 8px; background: var(--bg-hover); border-radius: 4px; overflow: hidden; margin-bottom: 8px; }
        .storage-used { height: 100%; background: var(--accent-gradient); border-radius: 4px; transition: width 0.3s ease; }
        .storage-info-row { display: flex; justify-content: space-between; font-size: 12px; color: var(--text-dimmed); }

        .models-list { margin-top: 8px; }
        .models-header { display: flex; align-items: center; gap: 8px; padding: 12px 0; font-size: 14px; font-weight: 500; color: var(--text-primary); }
        .models-empty { display: flex; flex-direction: column; align-items: center; padding: 32px; color: var(--text-dimmed); text-align: center; }
        .models-empty p { margin-top: 12px; font-size: 13px; }
        .model-item { display: flex; align-items: center; justify-content: space-between; padding: 12px; background: var(--bg-tertiary); border: 1px solid var(--border-primary); border-radius: 8px; margin-bottom: 8px; }
        .model-info-row { display: flex; align-items: center; gap: 10px; color: var(--text-muted); }
        .model-name { font-size: 13px; font-weight: 500; color: var(--text-primary); }
        .model-size { font-size: 11px; color: var(--text-dimmed); }
        .model-delete-btn { width: 28px; height: 28px; border-radius: 6px; background: transparent; border: none; color: var(--text-dimmed); cursor: pointer; display: flex; align-items: center; justify-content: center; }
        .model-delete-btn:hover { background: rgba(239, 68, 68, 0.15); color: var(--error); }
      `}</style>
    </div>
  );
};
