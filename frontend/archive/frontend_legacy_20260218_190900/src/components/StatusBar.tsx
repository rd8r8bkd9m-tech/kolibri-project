import { RefreshCcw, ShieldCheck, Sparkles } from "lucide-react";
import type { KnowledgeStatus } from "../core/knowledge";

interface StatusBarProps {
  status: KnowledgeStatus | null;
  error?: string;
  isLoading?: boolean;
  onRefresh?: () => void;
}

const StatusBar = ({ status, error, isLoading = false, onRefresh }: StatusBarProps) => (
  <header className="flex flex-col gap-4 rounded-3xl border border-border-strong bg-background-card/70 p-6 backdrop-blur">
    <div className="flex items-center justify-between">
      <div>
        <p className="text-xs uppercase tracking-widest text-text-secondary">Цифровой геном Колибри</p>
        <h2 className="mt-1 text-2xl font-semibold text-text-primary">Живая память знаний</h2>
      </div>
      <button
        type="button"
        onClick={onRefresh}
        disabled={isLoading}
        className="flex items-center gap-2 rounded-xl border border-border-strong px-3 py-2 text-xs font-semibold text-text-secondary transition-colors hover:text-text-primary disabled:opacity-60"
      >
        <RefreshCcw className="h-4 w-4" />
        Обновить
      </button>
    </div>
    <div className="flex flex-wrap gap-3 text-sm text-text-secondary">
      <article className="flex items-center gap-2 rounded-xl border border-border-strong bg-background-input/80 px-4 py-3">
        <Sparkles className="h-4 w-4 text-primary" />
        <div>
          <p className="text-xs uppercase tracking-wide text-text-secondary">Документов</p>
          <p className="text-sm font-semibold text-text-primary">{status ? status.documents : "—"}</p>
        </div>
      </article>
      <article className="flex items-center gap-2 rounded-xl border border-border-strong bg-background-input/80 px-4 py-3">
        <ShieldCheck className="h-4 w-4 text-accent" />
        <div>
          <p className="text-xs uppercase tracking-wide text-text-secondary">Статус
            </p>
          <p className="text-sm font-semibold text-text-primary">{status ? status.status : "unknown"}</p>
        </div>
      </article>
      {error && (
        <article className="rounded-xl border border-red-500/40 bg-red-500/10 px-4 py-3 text-sm text-red-200">
          {error}
        </article>
      )}
      {status?.timestamp && (
        <article className="rounded-xl border border-border-strong bg-background-input/80 px-4 py-3 text-xs text-text-secondary">
          Обновлено: {status.timestamp}
        </article>
      )}
    </div>
  </header>
);

export default StatusBar;
