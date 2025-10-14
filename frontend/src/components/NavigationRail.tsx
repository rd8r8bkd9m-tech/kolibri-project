import { Activity, Compass, Network, Plus, Settings, Sparkles } from "lucide-react";
import type { ConversationMetrics } from "../core/useKolibriChat";

const navigationItems = [
  { icon: Sparkles, label: "Диалог" },
  { icon: Compass, label: "Знания" },
  { icon: Network, label: "Рой" },
  { icon: Activity, label: "Аналитика" },
];

interface NavigationRailProps {
  onCreateConversation: () => void;
  isBusy: boolean;
  metrics: ConversationMetrics;
}

const NavigationRail = ({ onCreateConversation, isBusy, metrics }: NavigationRailProps) => (
  <div className="flex h-full w-20 flex-col items-center justify-between rounded-[2.5rem] border border-border-strong bg-background-panel/70 p-4 backdrop-blur">
    <div className="flex flex-col items-center gap-5">
      <div className="flex h-12 w-12 items-center justify-center rounded-3xl bg-primary/15 text-lg font-semibold text-primary">
        К
      </div>
      <button
        type="button"
        onClick={onCreateConversation}
        disabled={isBusy}
        className="flex h-12 w-12 items-center justify-center rounded-2xl border border-border-strong bg-background-card/80 text-text-secondary transition-colors hover:text-text-primary disabled:cursor-not-allowed disabled:opacity-50"
        aria-label="Новая беседа"
      >
        <Plus className="h-5 w-5" />
      </button>
      <nav className="flex flex-col items-center gap-3">
        {navigationItems.map((item) => (
          <button
            key={item.label}
            type="button"
            className="flex h-11 w-11 items-center justify-center rounded-2xl border border-transparent text-text-secondary transition-colors hover:border-primary/40 hover:text-text-primary"
            aria-label={item.label}
          >
            <item.icon className="h-5 w-5" />
          </button>
        ))}
      </nav>
    </div>
    <div className="space-y-3 text-center text-[0.65rem] text-text-secondary">
      <div className="rounded-2xl border border-border-strong bg-background-card/70 px-2 py-2">
        <p className="font-semibold text-text-primary">{metrics.userMessages + metrics.assistantMessages}</p>
        <p>сообщений</p>
      </div>
      <button
        type="button"
        className="flex h-11 w-11 items-center justify-center rounded-2xl border border-border-strong bg-background-card/80 text-text-secondary transition-colors hover:text-text-primary"
        aria-label="Настройки"
      >
        <Settings className="h-5 w-5" />
      </button>
    </div>
  </div>
);

export default NavigationRail;
