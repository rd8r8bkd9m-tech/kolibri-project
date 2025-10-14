import { AlertTriangle, Database, ListTree, Sparkles, Timer } from "lucide-react";
import type { ConversationMetrics } from "../core/useKolibriChat";
import type { KnowledgeStatus } from "../core/knowledge";
import type { ChatMessage } from "../types/chat";

interface InspectorPanelProps {
  status: KnowledgeStatus | null;
  error?: string;
  isLoading: boolean;
  metrics: ConversationMetrics;
  latestAssistantMessage?: ChatMessage;
  onRefresh?: () => void;
}

const formatDateTime = (iso?: string): string => {
  if (!iso) {
    return "—";
  }
  try {
    return new Date(iso).toLocaleString("ru-RU", {
      hour: "2-digit",
      minute: "2-digit",
      day: "2-digit",
      month: "short",
    });
  } catch {
    return "—";
  }
};

const StatCard = ({
  icon: Icon,
  label,
  value,
}: {
  icon: typeof Database;
  label: string;
  value: string;
}) => (
  <article className="flex items-center gap-3 rounded-2xl border border-border-strong bg-background-input/80 px-4 py-3">
    <span className="flex h-9 w-9 items-center justify-center rounded-xl bg-background-card/70 text-primary">
      <Icon className="h-4 w-4" />
    </span>
    <div>
      <p className="text-[0.7rem] uppercase tracking-wide text-text-secondary">{label}</p>
      <p className="text-sm font-semibold text-text-primary">{value}</p>
    </div>
  </article>
);

const InspectorPanel = ({ status, error, isLoading, metrics, latestAssistantMessage, onRefresh }: InspectorPanelProps) => {
  const context = latestAssistantMessage?.context;

  return (
    <section className="flex h-full flex-col gap-4 rounded-3xl border border-border-strong bg-background-card/80 p-6 backdrop-blur">
      <header className="flex items-center justify-between">
        <div>
          <p className="text-xs uppercase tracking-[0.35em] text-text-secondary">Мониторинг</p>
          <h2 className="mt-2 text-lg font-semibold text-text-primary">Пульс Колибри</h2>
        </div>
        {onRefresh && (
          <button
            type="button"
            onClick={onRefresh}
            disabled={isLoading}
            className="rounded-xl border border-border-strong bg-background-input/70 px-3 py-2 text-xs font-semibold text-text-secondary transition-colors hover:text-text-primary disabled:cursor-not-allowed disabled:opacity-50"
          >
            Обновить
          </button>
        )}
      </header>

      <div className="space-y-3">
        <StatCard
          icon={Database}
          label="Документов в геноме"
          value={status ? String(status.documents) : "—"}
        />
        <StatCard icon={Sparkles} label="Ответов с контекстом" value={String(metrics.knowledgeReferences)} />
        <StatCard icon={ListTree} label="Сообщений в беседе" value={String(metrics.userMessages + metrics.assistantMessages)} />
        <StatCard icon={Timer} label="Последний ответ" value={formatDateTime(metrics.lastUpdatedIso)} />
      </div>

      <div className="space-y-3">
        <h3 className="text-xs uppercase tracking-[0.35em] text-text-secondary">Контекст последнего ответа</h3>
        {context && context.length ? (
          <div className="space-y-3">
            {context.map((snippet, index) => (
              <article key={snippet.id} className="rounded-2xl border border-border-strong bg-background-input/80 p-3">
                <p className="flex items-center justify-between text-[0.7rem] font-semibold text-text-secondary">
                  <span className="uppercase tracking-wide text-text-primary">Источник {index + 1}</span>
                  <span>Релевантность: {snippet.score.toFixed(2)}</span>
                </p>
                <p className="mt-2 text-sm font-semibold text-text-primary">{snippet.title}</p>
                <p className="mt-2 whitespace-pre-line text-sm text-text-secondary">{snippet.content}</p>
                {snippet.source && (
                  <p className="mt-2 text-[0.65rem] uppercase tracking-wide text-primary/80">{snippet.source}</p>
                )}
              </article>
            ))}
          </div>
        ) : (
          <p className="rounded-2xl border border-dashed border-border-strong px-3 py-4 text-xs text-text-secondary">
            Колибри ещё не использовал внешние знания в этой беседе.
          </p>
        )}
      </div>

      <div className="mt-auto space-y-3">
        <h3 className="text-xs uppercase tracking-[0.35em] text-text-secondary">Диагностика</h3>
        <article className="rounded-2xl border border-border-strong bg-background-input/80 p-3 text-xs text-text-secondary">
          <p>
            Статус сервиса: <span className="font-semibold text-text-primary">{status ? status.status : "unknown"}</span>
          </p>
          {status?.timestamp && (
            <p className="mt-1">Снимок от: {formatDateTime(status.timestamp)}</p>
          )}
        </article>
        {error && (
          <p className="flex items-start gap-2 rounded-2xl border border-red-500/40 bg-red-500/10 p-3 text-xs text-red-200">
            <AlertTriangle className="mt-0.5 h-4 w-4" />
            {error}
          </p>
        )}
      </div>
    </section>
  );
};

export default InspectorPanel;
