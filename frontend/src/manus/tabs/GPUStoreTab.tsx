/**
 * tabs/GPUStoreTab.tsx
 *
 * Управление локальным GPU/vector store: загрузка документов,
 * генерация embeddings на backend и поиск по текстовому запросу.
 */

import { useCallback, useEffect, useState } from 'react';
import {
  Activity,
  Database,
  Download,
  FileText,
  HardDrive,
  Loader2,
  RefreshCw,
  Search,
  Send,
  Upload,
} from 'lucide-react';

interface GpuStatus {
  status: string;
  db_path: string;
  documents: number;
  embeddings: number;
  size_bytes: number;
}

interface DocumentResponse {
  doc_id: number;
  embedding_id: number;
  sha256: string;
  bytes: number;
  entropy: number;
  dims: number;
  embedding_preview: number[];
}

interface SearchHit {
  doc_id: number;
  score: number;
  path: string;
  sha256: string;
  class: string;
  bytes: number;
  snippet: string;
}

interface BatchItem {
  path: string;
  doc_id?: number | null;
  embedding_id?: number | null;
  bytes: number;
  status: string;
  detail?: string | null;
}

interface BatchResponse {
  status: string;
  indexed: number;
  skipped: number;
  bytes: number;
  items: BatchItem[];
}

interface PortableCorpusResponse {
  status: string;
  path: string;
  documents: number;
  embeddings: number;
  bytes: number;
  sha256: string;
}

const formatInt = (value: number): string => Math.round(value || 0).toLocaleString('ru-RU');
const formatBytes = (value: number): string => {
  if (value >= 1024 ** 2) return `${(value / 1024 ** 2).toFixed(2)} МБ`;
  if (value >= 1024) return `${(value / 1024).toFixed(1)} КБ`;
  return `${Math.round(value || 0)} Б`;
};

const readJson = async <T,>(response: Response): Promise<T> => {
  const data = await response.json().catch(() => null);
  if (!response.ok) {
    const detail = data && typeof data.detail === 'string' ? data.detail : response.statusText;
    throw new Error(detail);
  }
  return data as T;
};

export const GPUStoreTab = () => {
  const [status, setStatus] = useState<GpuStatus | null>(null);
  const [path, setPath] = useState('');
  const [docClass, setDocClass] = useState('document');
  const [content, setContent] = useState('');
  const [dims, setDims] = useState(64);
  const [query, setQuery] = useState('');
  const [limit, setLimit] = useState(8);
  const [serverPath, setServerPath] = useState('');
  const [exportPath, setExportPath] = useState('');
  const [importPath, setImportPath] = useState('');
  const [uploadResult, setUploadResult] = useState<DocumentResponse | null>(null);
  const [batchResult, setBatchResult] = useState<BatchResponse | null>(null);
  const [corpusResult, setCorpusResult] = useState<PortableCorpusResponse | null>(null);
  const [hits, setHits] = useState<SearchHit[]>([]);
  const [loading, setLoading] = useState(false);
  const [batchLoading, setBatchLoading] = useState(false);
  const [pathLoading, setPathLoading] = useState(false);
  const [corpusLoading, setCorpusLoading] = useState(false);
  const [searching, setSearching] = useState(false);
  const [error, setError] = useState('');

  const refreshStatus = useCallback(async () => {
    try {
      const response = await fetch('/api/gpu/status');
      setStatus(await readJson<GpuStatus>(response));
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }, []);

  useEffect(() => {
    void refreshStatus();
  }, [refreshStatus]);

  const uploadDocument = async () => {
    if (!content.trim()) return;
    setLoading(true);
    setError('');
    try {
      const response = await fetch('/api/gpu/document', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          path: path.trim() || 'docs/untitled.txt',
          class: docClass.trim() || 'document',
          content,
          dims,
          replace_embeddings: true,
        }),
      });
      const result = await readJson<DocumentResponse>(response);
      setUploadResult(result);
      await refreshStatus();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setLoading(false);
    }
  };

  const searchText = async () => {
    if (!query.trim()) return;
    setSearching(true);
    setError('');
    try {
      const response = await fetch('/api/gpu/search/text', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          query,
          limit,
          dims,
        }),
      });
      setHits(await readJson<SearchHit[]>(response));
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setSearching(false);
    }
  };

  const uploadFiles = async (files: FileList | null) => {
    if (!files || files.length === 0) return;
    setBatchLoading(true);
    setError('');
    try {
      const documents = await Promise.all(
        Array.from(files).map(async (file) => {
          return {
            path: file.webkitRelativePath || file.name,
            class: 'file',
            content: await file.text(),
            dims,
          };
        }),
      );
      const response = await fetch('/api/gpu/batch', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ documents, replace_embeddings: true }),
      });
      setBatchResult(await readJson<BatchResponse>(response));
      await refreshStatus();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setBatchLoading(false);
    }
  };

  const indexServerPath = async () => {
    if (!serverPath.trim()) return;
    setPathLoading(true);
    setError('');
    try {
      const response = await fetch('/api/gpu/index-path', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          path: serverPath,
          recursive: true,
          max_files: 2000,
          max_file_bytes: 1_000_000,
          class: 'file',
          dims,
          replace_embeddings: true,
        }),
      });
      setBatchResult(await readJson<BatchResponse>(response));
      await refreshStatus();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setPathLoading(false);
    }
  };

  const exportCorpus = async () => {
    setCorpusLoading(true);
    setError('');
    try {
      const response = await fetch('/api/gpu/export', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ path: exportPath.trim() || null }),
      });
      setCorpusResult(await readJson<PortableCorpusResponse>(response));
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setCorpusLoading(false);
    }
  };

  const importCorpus = async () => {
    if (!importPath.trim()) return;
    setCorpusLoading(true);
    setError('');
    try {
      const response = await fetch('/api/gpu/import', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ path: importPath.trim(), clear_existing: false, replace_embeddings: true }),
      });
      setCorpusResult(await readJson<PortableCorpusResponse>(response));
      await refreshStatus();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setCorpusLoading(false);
    }
  };

  return (
    <div className="gpu-tab">
      <header className="gpu-header">
        <div>
          <div className="gpu-kicker">Vector Memory</div>
          <h1 className="gpu-title">GPU Store</h1>
          <p className="gpu-subtitle">Документы превращаются в embeddings на backend, сохраняются в SQLite и ищутся по cosine similarity.</p>
        </div>
        <button type="button" className="gpu-refresh" onClick={() => void refreshStatus()}>
          <RefreshCw size={18} />
          <span>Обновить</span>
        </button>
      </header>

      <section className="gpu-stats" aria-label="Статус GPU Store">
        <div>
          <Database size={19} />
          <strong>{formatInt(status?.documents ?? 0)}</strong>
          <span>документов</span>
        </div>
        <div>
          <Activity size={19} />
          <strong>{formatInt(status?.embeddings ?? 0)}</strong>
          <span>embeddings</span>
        </div>
        <div>
          <HardDrive size={19} />
          <strong>{formatBytes(status?.size_bytes ?? 0)}</strong>
          <span>{status?.db_path ?? '.kolibri/knowledge/kolibri.db'}</span>
        </div>
      </section>

      {error && <div className="gpu-error">{error}</div>}

      <main className="gpu-main">
        <section className="gpu-panel">
          <div className="gpu-panel-head">
            <Upload size={18} />
            <h2>Загрузка документа</h2>
          </div>

          <div className="gpu-form-grid">
            <label>
              <span>Путь</span>
              <input value={path} onChange={(event) => setPath(event.target.value)} />
            </label>
            <label>
              <span>Класс</span>
              <input value={docClass} onChange={(event) => setDocClass(event.target.value)} />
            </label>
            <label>
              <span>Dims</span>
              <input type="number" min={8} max={512} value={dims} onChange={(event) => setDims(Number(event.target.value))} />
            </label>
          </div>

          <label className="gpu-textarea-wrap">
            <span>Текст документа</span>
            <textarea value={content} onChange={(event) => setContent(event.target.value)} />
          </label>

          <button type="button" className="gpu-primary" onClick={() => void uploadDocument()} disabled={loading || !content.trim()}>
            {loading ? <Loader2 size={17} className="gpu-spin" /> : <Send size={17} />}
            <span>Сохранить и построить embedding</span>
          </button>

          {uploadResult && (
            <div className="gpu-result">
              <div><strong>doc_id</strong><span>{uploadResult.doc_id}</span></div>
              <div><strong>embedding_id</strong><span>{uploadResult.embedding_id}</span></div>
              <div><strong>bytes</strong><span>{formatBytes(uploadResult.bytes)}</span></div>
              <div><strong>entropy</strong><span>{uploadResult.entropy.toFixed(4)}</span></div>
              <div className="is-wide">
                <strong>sha256</strong>
                <span>{uploadResult.sha256}</span>
              </div>
              <div className="is-wide">
                <strong>embedding preview</strong>
                <span>{uploadResult.embedding_preview.map((value) => value.toFixed(4)).join(', ')}</span>
              </div>
            </div>
          )}
        </section>

        <section className="gpu-panel">
          <div className="gpu-panel-head">
            <Search size={18} />
            <h2>Поиск</h2>
          </div>

          <div className="gpu-search-row">
            <input value={query} onChange={(event) => setQuery(event.target.value)} placeholder="Запрос для поиска" />
            <input type="number" min={1} max={100} value={limit} onChange={(event) => setLimit(Number(event.target.value))} />
            <button type="button" onClick={() => void searchText()} disabled={searching || !query.trim()}>
              {searching ? <Loader2 size={17} className="gpu-spin" /> : <Search size={17} />}
              <span>Искать</span>
            </button>
          </div>

          {hits.length === 0 ? (
            <div className="gpu-empty">
              <FileText size={24} />
              <span>Результаты появятся после поиска.</span>
            </div>
          ) : (
            <div className="gpu-hits">
              {hits.map((hit) => (
                <article key={`${hit.doc_id}-${hit.sha256}`} className="gpu-hit">
                  <div className="gpu-hit-top">
                    <strong>{hit.path}</strong>
                    <span>{hit.score.toFixed(4)}</span>
                  </div>
                  <p>{hit.snippet || 'Нет сохраненного текста.'}</p>
                  <div className="gpu-hit-meta">
                    <span>{hit.class || 'document'}</span>
                    <span>{formatBytes(hit.bytes)}</span>
                    <span>{hit.sha256.slice(0, 16)}</span>
                  </div>
                </article>
              ))}
            </div>
          )}
        </section>
      </main>

      <section className="gpu-ops">
        <div className="gpu-panel">
          <div className="gpu-panel-head">
            <Upload size={18} />
            <h2>Batch ingest</h2>
          </div>

          <div className="gpu-batch-grid">
            <label className="gpu-file-pick">
              <Upload size={17} />
              <span>{batchLoading ? 'Индексирую файлы...' : 'Выбрать файлы'}</span>
              <input
                type="file"
                multiple
                disabled={batchLoading}
                onChange={(event) => void uploadFiles(event.currentTarget.files)}
              />
            </label>

            <label>
              <span>Локальный путь или папка</span>
              <input value={serverPath} onChange={(event) => setServerPath(event.target.value)} placeholder="/Users/kolibri/Projects/..." />
            </label>

            <button type="button" onClick={() => void indexServerPath()} disabled={pathLoading || !serverPath.trim()}>
              {pathLoading ? <Loader2 size={17} className="gpu-spin" /> : <HardDrive size={17} />}
              <span>Индексировать путь</span>
            </button>
          </div>

          {batchResult && (
            <div className="gpu-batch-result">
              <strong>{batchResult.indexed} indexed</strong>
              <span>{batchResult.skipped} skipped</span>
              <span>{formatBytes(batchResult.bytes)}</span>
              <div>
                {batchResult.items.slice(0, 8).map((item) => (
                  <p key={`${item.path}-${item.status}`}>
                    <span>{item.status}</span> {item.path}{item.detail ? ` — ${item.detail}` : ''}
                  </p>
                ))}
              </div>
            </div>
          )}
        </div>

        <div className="gpu-panel">
          <div className="gpu-panel-head">
            <Download size={18} />
            <h2>Portable corpus</h2>
          </div>

          <div className="gpu-corpus-grid">
            <label>
              <span>Export path</span>
              <input value={exportPath} onChange={(event) => setExportPath(event.target.value)} placeholder=".kolibri/exports/gpu-store.kgpu" />
            </label>
            <button type="button" onClick={() => void exportCorpus()} disabled={corpusLoading}>
              {corpusLoading ? <Loader2 size={17} className="gpu-spin" /> : <Download size={17} />}
              <span>Экспорт .kgpu</span>
            </button>
            <label>
              <span>Import path</span>
              <input value={importPath} onChange={(event) => setImportPath(event.target.value)} placeholder="/Users/kolibri/.../gpu-store.kgpu" />
            </label>
            <button type="button" onClick={() => void importCorpus()} disabled={corpusLoading || !importPath.trim()}>
              {corpusLoading ? <Loader2 size={17} className="gpu-spin" /> : <Upload size={17} />}
              <span>Импорт .kgpu</span>
            </button>
          </div>

          {corpusResult && (
            <div className="gpu-result">
              <div><strong>documents</strong><span>{formatInt(corpusResult.documents)}</span></div>
              <div><strong>embeddings</strong><span>{formatInt(corpusResult.embeddings)}</span></div>
              <div><strong>bytes</strong><span>{formatBytes(corpusResult.bytes)}</span></div>
              <div className="is-wide"><strong>path</strong><span>{corpusResult.path}</span></div>
              <div className="is-wide"><strong>sha256</strong><span>{corpusResult.sha256}</span></div>
            </div>
          )}
        </div>
      </section>

      <style>{`
        .gpu-tab { height: 100%; overflow: auto; padding: 24px; color: var(--text-primary); }
        .gpu-header { display: flex; justify-content: space-between; gap: 16px; align-items: flex-start; margin-bottom: 18px; }
        .gpu-kicker { color: var(--accent-primary); font-size: 12px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.08em; margin-bottom: 6px; }
        .gpu-title { margin: 0; font-size: 30px; font-weight: 700; letter-spacing: 0; }
        .gpu-subtitle { margin: 8px 0 0; max-width: 720px; color: var(--text-muted); font-size: 14px; line-height: 1.45; }
        .gpu-refresh, .gpu-primary, .gpu-search-row button, .gpu-batch-grid button, .gpu-corpus-grid button, .gpu-file-pick {
          min-height: 40px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-tertiary); color: var(--text-secondary);
          display: inline-flex; align-items: center; justify-content: center; gap: 8px; padding: 0 12px; cursor: pointer; font-weight: 600; font-size: 13px;
        }
        .gpu-primary { background: var(--accent-bg); color: var(--accent-primary); border-color: var(--border-accent); }
        .gpu-refresh:hover, .gpu-search-row button:hover, .gpu-primary:hover, .gpu-batch-grid button:hover, .gpu-corpus-grid button:hover, .gpu-file-pick:hover { color: var(--text-primary); border-color: var(--border-accent); }
        .gpu-primary:disabled, .gpu-search-row button:disabled, .gpu-batch-grid button:disabled, .gpu-corpus-grid button:disabled { opacity: 0.55; cursor: not-allowed; }
        .gpu-stats { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 10px; margin-bottom: 14px; }
        .gpu-stats div { border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-card); min-height: 76px; padding: 14px; display: grid; grid-template-columns: auto 1fr; column-gap: 10px; row-gap: 3px; align-items: center; }
        .gpu-stats strong { font-size: 24px; }
        .gpu-stats span { grid-column: 2; color: var(--text-muted); font-size: 12px; overflow-wrap: anywhere; }
        .gpu-error { border: 1px solid color-mix(in srgb, var(--error) 45%, var(--border-primary)); border-radius: 8px; color: var(--error); padding: 10px 12px; margin-bottom: 12px; background: var(--bg-card); }
        .gpu-main { display: grid; grid-template-columns: minmax(0, 1fr) minmax(320px, .8fr); gap: 14px; align-items: start; }
        .gpu-panel { border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-card); padding: 16px; }
        .gpu-panel-head { display: flex; align-items: center; gap: 8px; margin-bottom: 14px; }
        .gpu-panel-head h2 { margin: 0; font-size: 17px; letter-spacing: 0; }
        .gpu-form-grid { display: grid; grid-template-columns: minmax(0, 1fr) 160px 110px; gap: 10px; }
        .gpu-form-grid label, .gpu-textarea-wrap { display: grid; gap: 6px; color: var(--text-muted); font-size: 12px; font-weight: 600; }
        .gpu-form-grid input, .gpu-textarea-wrap textarea, .gpu-search-row input, .gpu-batch-grid input:not([type="file"]), .gpu-corpus-grid input {
          border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-input); color: var(--text-primary); outline: none; padding: 0 11px;
        }
        .gpu-form-grid input, .gpu-search-row input, .gpu-batch-grid input:not([type="file"]), .gpu-corpus-grid input { min-height: 40px; }
        .gpu-form-grid input:focus, .gpu-textarea-wrap textarea:focus, .gpu-search-row input:focus, .gpu-batch-grid input:focus, .gpu-corpus-grid input:focus { border-color: var(--border-accent); }
        .gpu-textarea-wrap { margin-top: 12px; }
        .gpu-textarea-wrap textarea { min-height: 220px; padding: 12px; resize: vertical; line-height: 1.45; }
        .gpu-primary { margin-top: 12px; width: 100%; }
        .gpu-result { margin-top: 12px; display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 8px; }
        .gpu-result div { border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-tertiary); padding: 9px; display: grid; gap: 4px; min-width: 0; }
        .gpu-result .is-wide { grid-column: 1 / -1; }
        .gpu-result strong { color: var(--text-muted); font-size: 11px; }
        .gpu-result span { color: var(--text-secondary); font-size: 12px; overflow-wrap: anywhere; }
        .gpu-search-row { display: grid; grid-template-columns: minmax(0, 1fr) 84px auto; gap: 8px; margin-bottom: 12px; }
        .gpu-empty { min-height: 180px; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 8px; color: var(--text-muted); text-align: center; }
        .gpu-hits { display: grid; gap: 10px; }
        .gpu-hit { border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-tertiary); padding: 12px; }
        .gpu-hit-top { display: flex; justify-content: space-between; gap: 12px; align-items: flex-start; }
        .gpu-hit-top strong { overflow-wrap: anywhere; }
        .gpu-hit-top span { color: var(--accent-primary); font-weight: 700; }
        .gpu-hit p { margin: 8px 0; color: var(--text-secondary); font-size: 13px; line-height: 1.45; }
        .gpu-hit-meta { display: flex; gap: 8px; flex-wrap: wrap; color: var(--text-muted); font-size: 11px; }
        .gpu-hit-meta span { border: 1px solid var(--border-primary); border-radius: 999px; padding: 3px 8px; }
        .gpu-ops { margin-top: 14px; display: grid; grid-template-columns: minmax(0, 1fr) minmax(320px, .8fr); gap: 14px; align-items: start; }
        .gpu-batch-grid, .gpu-corpus-grid { display: grid; grid-template-columns: minmax(0, 180px) minmax(0, 1fr) auto; gap: 10px; align-items: end; }
        .gpu-corpus-grid { grid-template-columns: minmax(0, 1fr) auto; }
        .gpu-batch-grid label:not(.gpu-file-pick), .gpu-corpus-grid label { display: grid; gap: 6px; color: var(--text-muted); font-size: 12px; font-weight: 600; }
        .gpu-file-pick { position: relative; overflow: hidden; }
        .gpu-file-pick input { position: absolute; inset: 0; opacity: 0; cursor: pointer; }
        .gpu-batch-result { margin-top: 12px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-tertiary); padding: 10px; display: flex; gap: 10px; flex-wrap: wrap; color: var(--text-secondary); font-size: 12px; }
        .gpu-batch-result div { flex-basis: 100%; display: grid; gap: 4px; max-height: 180px; overflow: auto; }
        .gpu-batch-result p { margin: 0; overflow-wrap: anywhere; }
        .gpu-batch-result p span { color: var(--accent-primary); font-weight: 700; }
        .gpu-spin { animation: gpu-spin 1s linear infinite; }
        @keyframes gpu-spin { to { transform: rotate(360deg); } }
        @media (max-width: 1050px) {
          .gpu-main, .gpu-stats, .gpu-form-grid, .gpu-search-row, .gpu-ops, .gpu-batch-grid, .gpu-corpus-grid { grid-template-columns: 1fr; }
          .gpu-tab { padding: 16px; }
          .gpu-header { flex-direction: column; }
          .gpu-refresh { width: 100%; }
        }
      `}</style>
    </div>
  );
};
