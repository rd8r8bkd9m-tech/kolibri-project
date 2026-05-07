/**
 * ArchiverTab.tsx
 * 
 * Вкладка "Архиватор" — сжатие/распаковка текста через Calibre Predictive Compression.
 * Отображает статистику, поддержка zlib fallback.
 */

import { useState, useCallback } from 'react';
import { 
  Archive, 
  Upload, 
  Download, 
  BarChart3, 
  Zap,
  FileText,
  CheckCircle,
  AlertCircle,
  Loader2
} from 'lucide-react';

interface CompressResult {
  success: boolean;
  original_size: number;
  compressed_size: number;
  ratio: number;
  method: string;
  compressed_b64?: string;
  error?: string;
}

interface ArchiverStats {
  method: string;
  trained: boolean;
  evolve_rounds: number;
  native_available: boolean;
  super_hybrid?: {
    seed_bin_available: boolean;
    vault_available: boolean;
    default_archive_root: string;
    default_restore_root: string;
  };
}

interface ProjectArtifact {
  path: string;
  exists: boolean;
  size: number;
}

interface ProjectRunResult {
  success: boolean;
  error?: string;
  stdout?: string;
  stderr?: string;
  source_path?: string;
  target_path?: string;
  restore_dir?: string;
  vault_path?: string;
  report?: Record<string, number | boolean | string>;
  artifacts?: {
    seed?: ProjectArtifact;
    bin?: ProjectArtifact;
    vault?: ProjectArtifact;
    formula_report?: ProjectArtifact;
  };
  verify?: ProjectRunResult | null;
}

const API_BASE = '/api/archiver';
const DEFAULT_PROJECT_ROOT = '/Users/kolibri/Projects/kolibri-project';
const DEFAULT_ARCHIVE_ROOT = '/tmp/kolibri_archives/kolibri_project';
const DEFAULT_RESTORE_ROOT = '/tmp/kolibri_restores/kolibri_project';

const reportNumber = (report: ProjectRunResult['report'] | undefined, key: string): number | null => {
  const value = report?.[key];
  return typeof value === 'number' ? value : null;
};

const reportBool = (report: ProjectRunResult['report'] | undefined, key: string): boolean | null => {
  const value = report?.[key];
  return typeof value === 'boolean' ? value : null;
};

export const ArchiverTab = () => {
  const [inputText, setInputText] = useState('');
  const [compressedData, setCompressedData] = useState<string | null>(null);
  const [decompressedText, setDecompressedText] = useState<string | null>(null);
  const [lastResult, setLastResult] = useState<CompressResult | null>(null);
  const [stats, setStats] = useState<ArchiverStats | null>(null);
  const [loading, setLoading] = useState(false);
  const [projectOperation, setProjectOperation] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [mode, setMode] = useState<'compress' | 'decompress' | 'project'>('compress');
  const [projectSource, setProjectSource] = useState(DEFAULT_PROJECT_ROOT);
  const [projectSeed, setProjectSeed] = useState('kolibri_project');
  const [projectOutput, setProjectOutput] = useState(DEFAULT_ARCHIVE_ROOT);
  const [projectRestore, setProjectRestore] = useState(DEFAULT_RESTORE_ROOT);
  const [pairSeedPath, setPairSeedPath] = useState('');
  const [pairBinPath, setPairBinPath] = useState('');
  const [vaultPath, setVaultPath] = useState('');
  const [vaultStrategy, setVaultStrategy] = useState<'auto' | 'embedded_world_model' | 'materialized_atoms'>('auto');
  const [projectReport, setProjectReport] = useState<ProjectRunResult | null>(null);

  const fetchStats = useCallback(async () => {
    try {
      const res = await fetch(`${API_BASE}/stats`);
      if (res.ok) {
        setStats(await res.json());
      }
    } catch {
      // Сервер недоступен — не критично
    }
  }, []);

  const handleCompress = useCallback(async () => {
    if (!inputText.trim()) return;
    setLoading(true);
    setError(null);
    
    try {
      const res = await fetch(`${API_BASE}/compress`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ text: inputText }),
      });
      
      if (!res.ok) {
        throw new Error(`HTTP ${res.status}`);
      }
      
      const data: CompressResult = await res.json();
      setLastResult(data);
      
      if (data.success && data.compressed_b64) {
        setCompressedData(data.compressed_b64);
      } else {
        setError(data.error || 'Ошибка сжатия');
      }
    } catch (err) {
      setError(`Ошибка подключения: ${err instanceof Error ? err.message : 'unknown'}`);
    } finally {
      setLoading(false);
    }
  }, [inputText]);

  const handleDecompress = useCallback(async () => {
    if (!compressedData) return;
    setLoading(true);
    setError(null);
    
    try {
      const res = await fetch(`${API_BASE}/decompress`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ data_b64: compressedData }),
      });
      
      if (!res.ok) {
        throw new Error(`HTTP ${res.status}`);
      }
      
      const data = await res.json();
      if (data.success) {
        setDecompressedText(data.text || new TextDecoder().decode(
          Uint8Array.from(atob(data.data_b64 || ''), c => c.charCodeAt(0))
        ));
      } else {
        setError(data.error || 'Ошибка распаковки');
      }
    } catch (err) {
      setError(`Ошибка подключения: ${err instanceof Error ? err.message : 'unknown'}`);
    } finally {
      setLoading(false);
    }
  }, [compressedData]);

  const requestProjectRun = useCallback(async (
    endpoint: string,
    payload: Record<string, unknown>,
    operation: string,
  ) => {
    setLoading(true);
    setProjectOperation(operation);
    setError(null);

    try {
      const res = await fetch(`${API_BASE}/project/${endpoint}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });

      if (!res.ok) {
        throw new Error(`HTTP ${res.status}`);
      }

      const data: ProjectRunResult = await res.json();
      setProjectReport(data);

      if (data.artifacts?.seed?.path) setPairSeedPath(data.artifacts.seed.path);
      if (data.artifacts?.bin?.path) setPairBinPath(data.artifacts.bin.path);
      if (data.artifacts?.vault?.path) setVaultPath(data.artifacts.vault.path);
      if (data.restore_dir) setProjectRestore(data.restore_dir);
      if (data.target_path) setProjectRestore(data.target_path);

      if (!data.success) {
        setError(data.error || 'Операция архиватора завершилась ошибкой');
      }
    } catch (err) {
      setError(`Ошибка подключения: ${err instanceof Error ? err.message : 'unknown'}`);
    } finally {
      setLoading(false);
      setProjectOperation(null);
    }
  }, []);

  const handleProjectPair = useCallback(() => {
    const payload: Record<string, unknown> = {
      source_path: projectSource,
      seed: projectSeed,
      verify: true,
    };
    if (projectOutput.trim()) payload.output_dir = projectOutput;
    if (projectRestore.trim()) payload.restore_dir = projectRestore;
    requestProjectRun('pair', payload, 'pair');
  }, [projectOutput, projectRestore, projectSeed, projectSource, requestProjectRun]);

  const handleRestorePair = useCallback(() => {
    requestProjectRun('restore-pair', {
      seed_path: pairSeedPath,
      bin_path: pairBinPath,
      target_path: projectRestore,
    }, 'restore-pair');
  }, [pairBinPath, pairSeedPath, projectRestore, requestProjectRun]);

  const handleVaultCreate = useCallback(() => {
    const payload: Record<string, unknown> = {
      source_path: projectSource,
      seed: projectSeed,
      strategy: vaultStrategy,
      mode: 'standalone',
      verify: true,
    };
    if (vaultPath.trim()) payload.vault_path = vaultPath;
    requestProjectRun('vault', payload, 'vault');
  }, [projectSeed, projectSource, requestProjectRun, vaultPath, vaultStrategy]);

  const handleVaultExtract = useCallback(() => {
    requestProjectRun('extract-vault', {
      vault_path: vaultPath,
      target_path: projectRestore,
    }, 'extract-vault');
  }, [projectRestore, requestProjectRun, vaultPath]);

  const formatBytes = (bytes: number): string => {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
  };

  return (
    <div className="archiver-tab">
      <style>{`
        .archiver-tab {
          padding: 24px;
          max-width: 900px;
          margin: 0 auto;
        }
        .archiver-header {
          display: flex;
          align-items: center;
          gap: 12px;
          margin-bottom: 24px;
        }
        .archiver-header h2 {
          font-size: 20px;
          font-weight: 600;
          color: var(--text-primary);
          margin: 0;
        }
        .archiver-header-icon {
          display: flex;
          align-items: center;
          justify-content: center;
          width: 40px;
          height: 40px;
          border-radius: 10px;
          background: var(--accent-primary);
          color: white;
        }
        .archiver-mode-toggle {
          display: flex;
          gap: 4px;
          background: var(--bg-secondary);
          border-radius: 8px;
          padding: 4px;
          margin-bottom: 20px;
        }
        .archiver-mode-btn {
          flex: 1;
          padding: 8px 16px;
          border: none;
          border-radius: 6px;
          font-size: 14px;
          font-weight: 500;
          cursor: pointer;
          transition: all 0.2s;
          display: flex;
          align-items: center;
          justify-content: center;
          gap: 8px;
          color: var(--text-secondary);
          background: transparent;
        }
        .archiver-mode-btn.active {
          background: var(--bg-primary);
          color: var(--accent-primary);
          box-shadow: var(--shadow-card);
        }
        .archiver-textarea {
          width: 100%;
          min-height: 160px;
          padding: 14px;
          border: 1px solid var(--border-primary);
          border-radius: 10px;
          background: var(--bg-primary);
          color: var(--text-primary);
          font-family: 'SF Mono', 'Fira Code', monospace;
          font-size: 13px;
          line-height: 1.6;
          resize: vertical;
          outline: none;
          transition: border-color 0.2s;
          box-sizing: border-box;
        }
        .archiver-textarea:focus {
          border-color: var(--accent-primary);
        }
        .archiver-textarea::placeholder {
          color: var(--text-tertiary);
        }
        .archiver-project-grid {
          display: grid;
          grid-template-columns: repeat(2, minmax(0, 1fr));
          gap: 12px;
        }
        .archiver-field {
          display: flex;
          flex-direction: column;
          gap: 6px;
        }
        .archiver-field.full {
          grid-column: 1 / -1;
        }
        .archiver-field-label {
          font-size: 12px;
          font-weight: 600;
          color: var(--text-secondary);
        }
        .archiver-input,
        .archiver-select {
          width: 100%;
          min-height: 42px;
          padding: 9px 12px;
          border: 1px solid var(--border-primary);
          border-radius: 8px;
          background: var(--bg-primary);
          color: var(--text-primary);
          font-size: 13px;
          box-sizing: border-box;
          outline: none;
        }
        .archiver-input:focus,
        .archiver-select:focus {
          border-color: var(--accent-primary);
        }
        .archiver-actions {
          display: flex;
          gap: 12px;
          margin-top: 16px;
          flex-wrap: wrap;
        }
        .archiver-btn {
          padding: 10px 20px;
          border: none;
          border-radius: 8px;
          font-size: 14px;
          font-weight: 500;
          cursor: pointer;
          display: flex;
          align-items: center;
          gap: 8px;
          transition: all 0.2s;
        }
        .archiver-btn:disabled {
          opacity: 0.5;
          cursor: not-allowed;
        }
        .archiver-btn-primary {
          background: var(--accent-primary);
          color: white;
        }
        .archiver-btn-primary:hover:not(:disabled) {
          opacity: 0.9;
          transform: translateY(-1px);
        }
        .archiver-btn-secondary {
          background: var(--bg-secondary);
          color: var(--text-primary);
          border: 1px solid var(--border-primary);
        }
        .archiver-results {
          margin-top: 24px;
          padding: 20px;
          background: var(--bg-secondary);
          border-radius: 12px;
          border: 1px solid var(--border-primary);
        }
        .archiver-results h3 {
          font-size: 15px;
          font-weight: 600;
          color: var(--text-primary);
          margin: 0 0 16px 0;
          display: flex;
          align-items: center;
          gap: 8px;
        }
        .archiver-stat-grid {
          display: grid;
          grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
          gap: 12px;
        }
        .archiver-stat-card {
          padding: 14px;
          background: var(--bg-primary);
          border-radius: 8px;
          border: 1px solid var(--border-primary);
        }
        .archiver-stat-label {
          font-size: 12px;
          color: var(--text-tertiary);
          margin-bottom: 4px;
        }
        .archiver-stat-value {
          font-size: 20px;
          font-weight: 700;
          color: var(--text-primary);
        }
        .archiver-stat-value.good {
          color: #10b981;
        }
        .archiver-stat-value.warn {
          color: #f59e0b;
        }
        .archiver-error {
          margin-top: 16px;
          padding: 12px 16px;
          background: #fef2f2;
          border: 1px solid #fecaca;
          border-radius: 8px;
          color: #dc2626;
          font-size: 14px;
          display: flex;
          align-items: center;
          gap: 8px;
        }
        .archiver-output {
          margin-top: 16px;
        }
        .archiver-output-label {
          font-size: 13px;
          font-weight: 500;
          color: var(--text-secondary);
          margin-bottom: 8px;
        }
        .archiver-output-text {
          padding: 12px;
          background: var(--bg-primary);
          border: 1px solid var(--border-primary);
          border-radius: 8px;
          font-family: 'SF Mono', 'Fira Code', monospace;
          font-size: 13px;
          color: var(--text-primary);
          max-height: 200px;
          overflow-y: auto;
          word-break: break-all;
        }
        .archiver-log {
          white-space: pre-wrap;
          word-break: break-word;
          max-height: 320px;
          margin: 0;
        }
        .archiver-path-list {
          display: grid;
          gap: 6px;
          margin-top: 12px;
        }
        .archiver-path-row {
          display: grid;
          grid-template-columns: 88px minmax(0, 1fr) 90px;
          gap: 8px;
          align-items: center;
          font-size: 12px;
          color: var(--text-secondary);
        }
        .archiver-path-row code {
          overflow-wrap: anywhere;
          color: var(--text-primary);
        }
        .archiver-spinner {
          animation: archiver-spin 1s linear infinite;
        }
        @keyframes archiver-spin {
          to { transform: rotate(360deg); }
        }
        .archiver-info {
          margin-top: 20px;
          padding: 16px;
          background: linear-gradient(135deg, var(--bg-secondary), var(--bg-primary));
          border-radius: 12px;
          border: 1px solid var(--border-primary);
        }
        .archiver-info p {
          margin: 6px 0;
          font-size: 13px;
          color: var(--text-secondary);
          line-height: 1.5;
        }
        .archiver-info strong {
          color: var(--text-primary);
        }
        @media (max-width: 760px) {
          .archiver-tab {
            padding: 14px 12px 24px;
          }
          .archiver-header h2 {
            font-size: 22px;
          }
          .archiver-mode-btn {
            min-height: 42px;
            font-size: 15px;
          }
          .archiver-textarea {
            font-size: 15px;
            min-height: 180px;
          }
          .archiver-actions {
            flex-direction: column;
          }
          .archiver-btn {
            width: 100%;
            justify-content: center;
            min-height: 44px;
          }
          .archiver-stat-grid {
            grid-template-columns: 1fr;
          }
          .archiver-project-grid {
            grid-template-columns: 1fr;
          }
          .archiver-path-row {
            grid-template-columns: 1fr;
          }
        }
      `}</style>

      {/* Заголовок */}
      <div className="archiver-header">
        <div className="archiver-header-icon">
          <Archive size={20} />
        </div>
        <div>
          <h2>Колибри Архиватор</h2>
          <span style={{ fontSize: '13px', color: 'var(--text-tertiary)' }}>
            Предиктивное сжатие на основе формул
          </span>
        </div>
      </div>

      {/* Переключатель режима */}
      <div className="archiver-mode-toggle">
        <button
          className={`archiver-mode-btn ${mode === 'compress' ? 'active' : ''}`}
          onClick={() => setMode('compress')}
        >
          <Upload size={16} />
          Сжатие
        </button>
        <button
          className={`archiver-mode-btn ${mode === 'decompress' ? 'active' : ''}`}
          onClick={() => setMode('decompress')}
        >
          <Download size={16} />
          Распаковка
        </button>
        <button
          className={`archiver-mode-btn ${mode === 'project' ? 'active' : ''}`}
          onClick={() => setMode('project')}
        >
          <Archive size={16} />
          Проект
        </button>
      </div>

      {/* Ввод */}
      {mode === 'compress' ? (
        <>
          <textarea
            className="archiver-textarea"
            value={inputText}
            onChange={(e) => setInputText(e.target.value)}
            placeholder="Введите текст для сжатия..."
          />
          <div style={{ fontSize: '12px', color: 'var(--text-tertiary)', marginTop: 4 }}>
            <FileText size={12} style={{ display: 'inline', verticalAlign: 'middle' }} />
            {' '}{inputText.length} символов · {new Blob([inputText]).size} байт
          </div>
          <div className="archiver-actions">
            <button
              className="archiver-btn archiver-btn-primary"
              onClick={handleCompress}
              disabled={loading || !inputText.trim()}
            >
              {loading ? (
                <Loader2 size={16} className="archiver-spinner" />
              ) : (
                <Zap size={16} />
              )}
              Сжать
            </button>
            <button
              className="archiver-btn archiver-btn-secondary"
              onClick={() => { setInputText(''); setLastResult(null); setCompressedData(null); setError(null); }}
            >
              Очистить
            </button>
            <button
              className="archiver-btn archiver-btn-secondary"
              onClick={fetchStats}
            >
              <BarChart3 size={16} />
              Статус
            </button>
          </div>
        </>
      ) : mode === 'decompress' ? (
        <>
          <textarea
            className="archiver-textarea"
            value={compressedData || ''}
            onChange={(e) => setCompressedData(e.target.value)}
            placeholder="Вставьте сжатые данные (base64)..."
          />
          <div className="archiver-actions">
            <button
              className="archiver-btn archiver-btn-primary"
              onClick={handleDecompress}
              disabled={loading || !compressedData}
            >
              {loading ? (
                <Loader2 size={16} className="archiver-spinner" />
              ) : (
                <Download size={16} />
              )}
              Распаковать
            </button>
          </div>
        </>
      ) : (
        <>
          <div className="archiver-project-grid">
            <div className="archiver-field full">
              <label className="archiver-field-label">Источник</label>
              <input
                className="archiver-input"
                value={projectSource}
                onChange={(e) => setProjectSource(e.target.value)}
              />
            </div>
            <div className="archiver-field">
              <label className="archiver-field-label">Seed</label>
              <input
                className="archiver-input"
                value={projectSeed}
                onChange={(e) => setProjectSeed(e.target.value)}
              />
            </div>
            <div className="archiver-field">
              <label className="archiver-field-label">Стратегия .klb</label>
              <select
                className="archiver-select"
                value={vaultStrategy}
                onChange={(e) => setVaultStrategy(e.target.value as typeof vaultStrategy)}
              >
                <option value="auto">auto</option>
                <option value="embedded_world_model">embedded_world_model</option>
                <option value="materialized_atoms">materialized_atoms</option>
              </select>
            </div>
            <div className="archiver-field full">
              <label className="archiver-field-label">Выход seed+bin</label>
              <input
                className="archiver-input"
                value={projectOutput}
                onChange={(e) => setProjectOutput(e.target.value)}
              />
            </div>
            <div className="archiver-field full">
              <label className="archiver-field-label">Восстановить в</label>
              <input
                className="archiver-input"
                value={projectRestore}
                onChange={(e) => setProjectRestore(e.target.value)}
              />
            </div>
            <div className="archiver-field full">
              <label className="archiver-field-label">Seed-файл</label>
              <input
                className="archiver-input"
                value={pairSeedPath}
                onChange={(e) => setPairSeedPath(e.target.value)}
                placeholder="/tmp/kolibri_archives/kolibri_project/kolibri_project.seed"
              />
            </div>
            <div className="archiver-field full">
              <label className="archiver-field-label">Bin-файл</label>
              <input
                className="archiver-input"
                value={pairBinPath}
                onChange={(e) => setPairBinPath(e.target.value)}
                placeholder="/tmp/kolibri_archives/kolibri_project/kolibri_project.bin"
              />
            </div>
            <div className="archiver-field full">
              <label className="archiver-field-label">Vault .klb</label>
              <input
                className="archiver-input"
                value={vaultPath}
                onChange={(e) => setVaultPath(e.target.value)}
                placeholder="/tmp/kolibri_archives/kolibri_project/kolibri_project.klb"
              />
            </div>
          </div>

          <div className="archiver-actions">
            <button
              className="archiver-btn archiver-btn-primary"
              onClick={handleProjectPair}
              disabled={loading || !projectSource.trim() || !projectSeed.trim()}
            >
              {loading && projectOperation === 'pair' ? (
                <Loader2 size={16} className="archiver-spinner" />
              ) : (
                <Archive size={16} />
              )}
              Сжать проект
            </button>
            <button
              className="archiver-btn archiver-btn-secondary"
              onClick={handleRestorePair}
              disabled={loading || !pairSeedPath.trim() || !pairBinPath.trim() || !projectRestore.trim()}
            >
              {loading && projectOperation === 'restore-pair' ? (
                <Loader2 size={16} className="archiver-spinner" />
              ) : (
                <Download size={16} />
              )}
              Восстановить seed+bin
            </button>
            <button
              className="archiver-btn archiver-btn-secondary"
              onClick={handleVaultCreate}
              disabled={loading || !projectSource.trim() || !projectSeed.trim()}
            >
              {loading && projectOperation === 'vault' ? (
                <Loader2 size={16} className="archiver-spinner" />
              ) : (
                <Upload size={16} />
              )}
              Создать .klb
            </button>
            <button
              className="archiver-btn archiver-btn-secondary"
              onClick={handleVaultExtract}
              disabled={loading || !vaultPath.trim() || !projectRestore.trim()}
            >
              {loading && projectOperation === 'extract-vault' ? (
                <Loader2 size={16} className="archiver-spinner" />
              ) : (
                <Download size={16} />
              )}
              Распаковать .klb
            </button>
          </div>
        </>
      )}

      {/* Ошибка */}
      {error && (
        <div className="archiver-error">
          <AlertCircle size={16} />
          {error}
        </div>
      )}

      {/* Результаты сжатия */}
      {lastResult && lastResult.success && (
        <div className="archiver-results">
          <h3>
            <CheckCircle size={16} style={{ color: '#10b981' }} />
            Результат сжатия
          </h3>
          <div className="archiver-stat-grid">
            <div className="archiver-stat-card">
              <div className="archiver-stat-label">Оригинал</div>
              <div className="archiver-stat-value">
                {formatBytes(lastResult.original_size)}
              </div>
            </div>
            <div className="archiver-stat-card">
              <div className="archiver-stat-label">Сжато</div>
              <div className="archiver-stat-value">
                {formatBytes(lastResult.compressed_size)}
              </div>
            </div>
            <div className="archiver-stat-card">
              <div className="archiver-stat-label">Коэффициент</div>
              <div className={`archiver-stat-value ${lastResult.ratio > 1 ? 'good' : 'warn'}`}>
                {lastResult.ratio.toFixed(2)}x
              </div>
            </div>
            <div className="archiver-stat-card">
              <div className="archiver-stat-label">Метод</div>
              <div className="archiver-stat-value" style={{ fontSize: '16px' }}>
                {lastResult.method}
              </div>
            </div>
          </div>
        </div>
      )}

      {/* Распакованный текст */}
      {decompressedText && mode === 'decompress' && (
        <div className="archiver-output">
          <div className="archiver-output-label">Распакованный текст:</div>
          <div className="archiver-output-text">{decompressedText}</div>
        </div>
      )}

      {/* Отчет Super-Hybrid */}
      {projectReport && mode === 'project' && (
        <div className="archiver-results">
          <h3>
            {projectReport.success ? (
              <CheckCircle size={16} style={{ color: '#10b981' }} />
            ) : (
              <AlertCircle size={16} style={{ color: '#dc2626' }} />
            )}
            Отчет Super-Hybrid
          </h3>
          <div className="archiver-stat-grid">
            {reportNumber(projectReport.report, 'original_size') !== null && (
              <div className="archiver-stat-card">
                <div className="archiver-stat-label">Исходный размер</div>
                <div className="archiver-stat-value">
                  {formatBytes(reportNumber(projectReport.report, 'original_size') || 0)}
                </div>
              </div>
            )}
            {reportNumber(projectReport.report, 'seed_size') !== null && (
              <div className="archiver-stat-card">
                <div className="archiver-stat-label">Seed</div>
                <div className="archiver-stat-value">
                  {formatBytes(reportNumber(projectReport.report, 'seed_size') || 0)}
                </div>
              </div>
            )}
            {(reportNumber(projectReport.report, 'bin_size') !== null || reportNumber(projectReport.report, 'vault_size') !== null) && (
              <div className="archiver-stat-card">
                <div className="archiver-stat-label">Bin / Vault</div>
                <div className="archiver-stat-value">
                  {formatBytes(
                    reportNumber(projectReport.report, 'bin_size')
                    || reportNumber(projectReport.report, 'vault_size')
                    || 0
                  )}
                </div>
              </div>
            )}
            {reportNumber(projectReport.report, 'ratio') !== null && (
              <div className="archiver-stat-card">
                <div className="archiver-stat-label">Коэффициент</div>
                <div className={`archiver-stat-value ${(reportNumber(projectReport.report, 'ratio') || 0) > 1 ? 'good' : 'warn'}`}>
                  {(reportNumber(projectReport.report, 'ratio') || 0).toFixed(2)}x
                </div>
              </div>
            )}
            {reportNumber(projectReport.report, 'files') !== null && (
              <div className="archiver-stat-card">
                <div className="archiver-stat-label">Файлы</div>
                <div className="archiver-stat-value">
                  {reportNumber(projectReport.report, 'files')}
                </div>
              </div>
            )}
            {reportBool(projectReport.report, 'bit_exact') !== null && (
              <div className="archiver-stat-card">
                <div className="archiver-stat-label">Bit-exact</div>
                <div className={`archiver-stat-value ${reportBool(projectReport.report, 'bit_exact') ? 'good' : 'warn'}`}>
                  {reportBool(projectReport.report, 'bit_exact') ? 'Да' : 'Нет'}
                </div>
              </div>
            )}
            {reportNumber(projectReport.report, 'pure_formula_bytes') !== null && (
              <div className="archiver-stat-card">
                <div className="archiver-stat-label">Чистая формула</div>
                <div className="archiver-stat-value">
                  {formatBytes(reportNumber(projectReport.report, 'pure_formula_bytes') || 0)}
                </div>
              </div>
            )}
            {reportNumber(projectReport.report, 'residual_bytes') !== null && (
              <div className="archiver-stat-card">
                <div className="archiver-stat-label">Residual</div>
                <div className="archiver-stat-value">
                  {formatBytes(reportNumber(projectReport.report, 'residual_bytes') || 0)}
                </div>
              </div>
            )}
          </div>

          {projectReport.artifacts && (
            <div className="archiver-path-list">
              {Object.entries(projectReport.artifacts).map(([name, artifact]) => {
                if (!artifact) return null;
                return (
                  <div className="archiver-path-row" key={name}>
                    <span>{name}</span>
                    <code>{artifact.path}</code>
                    <span>{formatBytes(artifact.size)}</span>
                  </div>
                );
              })}
            </div>
          )}

          {(projectReport.stdout || projectReport.stderr || projectReport.verify?.stdout || projectReport.verify?.stderr) && (
            <div className="archiver-output">
              <div className="archiver-output-label">Лог:</div>
              <pre className="archiver-output-text archiver-log">
                {[
                  projectReport.stdout,
                  projectReport.stderr,
                  projectReport.verify?.stdout,
                  projectReport.verify?.stderr,
                ].filter(Boolean).join('\n')}
              </pre>
            </div>
          )}
        </div>
      )}

      {/* Статистика сервера */}
      {stats && (
        <div className="archiver-results" style={{ marginTop: 16 }}>
          <h3>
            <BarChart3 size={16} style={{ color: 'var(--accent-primary)' }} />
            Статус архиватора
          </h3>
          <div className="archiver-stat-grid">
            <div className="archiver-stat-card">
              <div className="archiver-stat-label">Метод</div>
              <div className="archiver-stat-value" style={{ fontSize: '16px' }}>
                {stats.method}
              </div>
            </div>
            <div className="archiver-stat-card">
              <div className="archiver-stat-label">Обучен</div>
              <div className={`archiver-stat-value ${stats.trained ? 'good' : 'warn'}`}>
                {stats.trained ? 'Да' : 'Нет'}
              </div>
            </div>
            <div className="archiver-stat-card">
              <div className="archiver-stat-label">Native KPC</div>
              <div className={`archiver-stat-value ${stats.native_available ? 'good' : 'warn'}`}>
                {stats.native_available ? 'Доступен' : 'Fallback'}
              </div>
            </div>
            {stats.super_hybrid && (
              <div className="archiver-stat-card">
                <div className="archiver-stat-label">Seed+bin</div>
                <div className={`archiver-stat-value ${stats.super_hybrid.seed_bin_available ? 'good' : 'warn'}`}>
                  {stats.super_hybrid.seed_bin_available ? 'Готов' : 'Нет'}
                </div>
              </div>
            )}
            {stats.super_hybrid && (
              <div className="archiver-stat-card">
                <div className="archiver-stat-label">Vault .klb</div>
                <div className={`archiver-stat-value ${stats.super_hybrid.vault_available ? 'good' : 'warn'}`}>
                  {stats.super_hybrid.vault_available ? 'Готов' : 'Нет'}
                </div>
              </div>
            )}
          </div>
        </div>
      )}

      {/* Информация */}
      <div className="archiver-info">
        <p><strong>Колибри Predictive Compression (KPC)</strong></p>
        <p>
          Предиктивное сжатие использует эволюционирующие MLP-формулы для предсказания 
          следующего байта. Арифметическое кодирование конвертирует предсказания в компактный 
          битовый поток.
        </p>
        <p>
          <strong>Методы:</strong> KPC (нативный C) → zlib (Python fallback)
        </p>
        <p>
          <strong>Super-Hybrid:</strong> seed+bin и .klb Vault используют meta-formula корпус с bit-exact проверкой.
        </p>
      </div>
    </div>
  );
};
