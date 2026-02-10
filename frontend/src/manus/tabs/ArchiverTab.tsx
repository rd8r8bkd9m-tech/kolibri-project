/**
 * ArchiverTab.tsx
 * 
 * Вкладка "Архиватор" — сжатие/распаковка текста через Kolibri Predictive Compression.
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
}

const API_BASE = '/api/archiver';

export const ArchiverTab = () => {
  const [inputText, setInputText] = useState('');
  const [compressedData, setCompressedData] = useState<string | null>(null);
  const [decompressedText, setDecompressedText] = useState<string | null>(null);
  const [lastResult, setLastResult] = useState<CompressResult | null>(null);
  const [stats, setStats] = useState<ArchiverStats | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [mode, setMode] = useState<'compress' | 'decompress'>('compress');

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
        .archiver-actions {
          display: flex;
          gap: 12px;
          margin-top: 16px;
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
      `}</style>

      {/* Заголовок */}
      <div className="archiver-header">
        <div className="archiver-header-icon">
          <Archive size={20} />
        </div>
        <div>
          <h2>Kolibri Архиватор</h2>
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
      ) : (
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
          </div>
        </div>
      )}

      {/* Информация */}
      <div className="archiver-info">
        <p><strong>Kolibri Predictive Compression (KPC)</strong></p>
        <p>
          Предиктивное сжатие использует эволюционирующие MLP-формулы для предсказания 
          следующего байта. Арифметическое кодирование конвертирует предсказания в компактный 
          битовый поток.
        </p>
        <p>
          <strong>Методы:</strong> KPC (нативный C) → zlib (Python fallback)
        </p>
      </div>
    </div>
  );
};
