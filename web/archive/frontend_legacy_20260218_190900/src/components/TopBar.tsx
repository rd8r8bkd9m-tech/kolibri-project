import { CheckCircle2, CircleDashed, MessageSquare, RefreshCcw, Sparkles } from "lucide-react";
import { useId } from "react";
import type { ConversationMetrics } from "../core/useKolibriChat";
import type { KnowledgeStatus } from "../core/knowledge";

interface TopBarProps {
  title: string;
  onTitleChange: (value: string) => void;
  isProcessing: boolean;
  bridgeReady: boolean;
  knowledgeStatus: KnowledgeStatus | null;
  metrics: ConversationMetrics;
  onRefreshKnowledge: () => void;
  isKnowledgeLoading: boolean;
}

const formatIsoTime = (iso?: string): string => {
  if (!iso) {
    return "—";
  }
  try {
    return new Date(iso).toLocaleTimeString("ru-RU", { hour: "2-digit", minute: "2-digit" });
  } catch {
    return "—";
  }
};

const StatusBadge = ({ icon: Icon, label, value }: { icon: typeof MessageSquare; label: string; value: string }) => (
  <div className="flex items-center gap-2 rounded-2xl border border-border-strong bg-background-card/80 px-4 py-2">
    <span className="flex h-8 w-8 items-center justify-center rounded-xl bg-background-input/60 text-primary">
      <Icon className="h-4 w-4" />
    </span>
    <div className="flex flex-col text-left">
      <span className="text-[0.65rem] uppercase tracking-wide text-text-secondary">{label}</span>
      <span className="text-sm font-semibold text-text-primary">{value}</span>
    </div>
  </div>
);

const TopBar = ({
  title,
  onTitleChange,
  isProcessing,
  bridgeReady,
  knowledgeStatus,
  metrics,
  onRefreshKnowledge,
  isKnowledgeLoading,
}: TopBarProps) => {
  const inputId = useId();

  const connectionBadge = bridgeReady
    ? { icon: CheckCircle2, label: "Статус ядра", value: "Подключено" }
    : { icon: CircleDashed, label: "Статус ядра", value: "Ожидание" };

  const knowledgeBadge = {
    icon: Sparkles,
    label: "Документов",
    value: knowledgeStatus ? `${knowledgeStatus.documents}` : "—",
  };

  const activityBadge = {
    icon: MessageSquare,
    label: "Диалог",
    value: `${metrics.userMessages + metrics.assistantMessages} сообщений`,
  };

  return (
    <header className="rounded-3xl border border-border-strong bg-background-card/80 p-6 backdrop-blur">
      <div className="flex flex-col gap-4 lg:flex-row lg:items-center lg:justify-between">
        <div className="flex flex-1 flex-col gap-3">
          <label htmlFor={inputId} className="text-xs uppercase tracking-[0.35em] text-text-secondary">
            Название беседы
          </label>
          <input
            id={inputId}
            value={title}
            onChange={(event) => onTitleChange(event.target.value)}
            placeholder="Назови беседу"
            className="w-full rounded-2xl border border-border-strong bg-background-input/80 px-4 py-3 text-lg font-semibold text-text-primary focus:border-primary focus:outline-none"
          />
          <p className="text-xs text-text-secondary">
            Последнее обновление: {formatIsoTime(metrics.lastUpdatedIso)}
          </p>
        </div>
        <div className="flex flex-1 flex-wrap items-center justify-end gap-3">
          <StatusBadge {...connectionBadge} />
          <StatusBadge {...knowledgeBadge} />
          <StatusBadge {...activityBadge} />
          <button
            type="button"
            onClick={onRefreshKnowledge}
            disabled={isKnowledgeLoading || isProcessing}
            className="flex items-center gap-2 rounded-2xl border border-border-strong bg-background-input/80 px-4 py-2 text-sm font-semibold text-text-secondary transition-colors hover:text-text-primary disabled:cursor-not-allowed disabled:opacity-50"
          >
            <RefreshCcw className="h-4 w-4" />
            Обновить память
          </button>
        </div>
      </div>
    </header>
  );
};

export default TopBar;
