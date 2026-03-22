import { Laptop, Moon, PanelRightOpen, Sparkles, Sun } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Switch } from "@/components/ui/switch";
import { Sheet, SheetContent } from "@/components/ui/sheet";
import { useModelStatusQuery } from "@/features/workspace/query";
import { AppPanel, SectionTitle } from "@/features/ui-system/surface";
import { useChatStore, type AssistantPersona, type ThemeMode } from "@/store/useChatStore";
import { useShellStore } from "@/store/useShellStore";

const themeOptions: Array<{ key: ThemeMode; label: string; icon: typeof Laptop }> = [
  { key: "system", label: "Система", icon: Laptop },
  { key: "light", label: "Светлая", icon: Sun },
  { key: "dark", label: "Тёмная", icon: Moon },
];

const personaOptions: Array<{ key: AssistantPersona; label: string; description: string }> = [
  { key: "assistant", label: "Assistant", description: "Спокойный рабочий тон." },
  { key: "romantic", label: "Romantic", description: "Более тёплый и мягкий стиль." },
  { key: "storyteller", label: "Storyteller", description: "Развёрнутые ответы и примеры." },
];

export function SettingsDrawerV3() {
  const open = useShellStore((s) => s.settingsOpen);
  const closeSettings = useShellStore((s) => s.closeSettings);
  const openWorkspace = useShellStore((s) => s.openWorkspace);
  const theme = useChatStore((s) => s.theme);
  const setTheme = useChatStore((s) => s.setTheme);
  const model = useChatStore((s) => s.model);
  const persona = useChatStore((s) => s.persona);
  const setPersona = useChatStore((s) => s.setPersona);
  const memoryEnabled = useChatStore((s) => s.memoryEnabled);
  const setMemoryEnabled = useChatStore((s) => s.setMemoryEnabled);
  const modelStatus = useModelStatusQuery();

  return (
    <Sheet open={open} onOpenChange={(next) => (!next ? closeSettings() : undefined)}>
      <SheetContent
        title="Настройки"
        description="Внешний вид, память, персона и инструменты"
        className="left-auto right-0 z-50 w-full max-w-[31rem] border-l border-r-0 bg-background px-5 pb-5 pt-5 max-lg:inset-x-0 max-lg:left-0 max-lg:right-0 max-lg:top-auto max-lg:h-[88dvh] max-lg:max-w-none max-lg:rounded-t-[1.9rem] max-lg:border-l-0 max-lg:border-t max-lg:pb-[calc(env(safe-area-inset-bottom)+1rem)]"
      >
        <div className="flex h-full min-h-0 flex-col gap-5">
          <div className="flex items-start justify-between gap-4">
            <SectionTitle
              eyebrow="Настройки"
              title="Профиль и предпочтения"
              description="Единый secondary drawer вместо отдельного профиля как primary surface."
            />
            <Button type="button" variant="ghost" size="icon" className="h-10 w-10 rounded-full" onClick={closeSettings}>
              ×
            </Button>
          </div>

          <div className="min-h-0 flex-1 space-y-4 overflow-y-auto pr-1">
            <AppPanel className="px-4 py-4">
              <SectionTitle eyebrow="Внешний вид" title="Тема" description="Одна спокойная визуальная система для mobile и desktop." />
              <div className="mt-4 grid grid-cols-3 gap-2">
                {themeOptions.map((option) => {
                  const Icon = option.icon;
                  return (
                    <button
                      key={option.key}
                      type="button"
                      onClick={() => setTheme(option.key)}
                      className={`rounded-[1.2rem] border px-3 py-3 text-left transition ${
                        theme === option.key ? "v3-accent-surface text-foreground" : "border-foreground/8 bg-background text-muted"
                      }`}
                    >
                      <Icon className="h-4.5 w-4.5" />
                      <span className="mt-3 block text-sm font-semibold">{option.label}</span>
                    </button>
                  );
                })}
              </div>
            </AppPanel>

            <AppPanel className="px-4 py-4">
              <SectionTitle eyebrow="Диалог" title="Персона и память" description="Пользовательские настройки остаются вторичными по отношению к диалогу." />
              <div className="mt-4 space-y-2">
                {personaOptions.map((option) => (
                  <button
                    key={option.key}
                    type="button"
                    onClick={() => setPersona(option.key)}
                    className={`w-full rounded-[1.2rem] border px-4 py-3 text-left transition ${
                      persona === option.key ? "v3-accent-surface" : "border-foreground/8 bg-background"
                    }`}
                  >
                    <span className="block text-sm font-semibold text-foreground">
                      {option.key === "assistant" ? "Ассистент" : option.key === "romantic" ? "Романтик" : "Рассказчик"}
                    </span>
                    <span className="mt-1 block text-xs text-muted">{option.description}</span>
                  </button>
                ))}
              </div>
              <div className="mt-4 flex items-center justify-between rounded-[1.2rem] border border-foreground/8 bg-background px-4 py-3">
                <div>
                  <p className="text-sm font-semibold text-foreground">Память</p>
                  <p className="text-xs text-muted">Локальная память диалога и client identity.</p>
                </div>
                <Switch checked={memoryEnabled} onCheckedChange={setMemoryEnabled} />
              </div>
            </AppPanel>

            <AppPanel className="px-4 py-4">
              <SectionTitle eyebrow="Runtime" title="Текущая модель" description="Backend contracts остаются теми же, меняется только orchestration и shell." />
              <div className="mt-4 rounded-[1.2rem] border border-foreground/8 bg-background px-4 py-3">
                <p className="text-sm font-semibold text-foreground">{model}</p>
                <p className="mt-1 text-xs text-muted">
                  {modelStatus.data?.primary_model ?? "runtime status загружается…"}
                </p>
              </div>
              <div className="mt-3 grid grid-cols-2 gap-3 text-sm">
                <div className="rounded-[1.2rem] border border-foreground/8 bg-background px-3 py-3">
                  <p className="text-muted">Документы</p>
                  <p className="mt-1 font-semibold">{modelStatus.data?.documents ?? "—"}</p>
                </div>
                <div className="rounded-[1.2rem] border border-foreground/8 bg-background px-3 py-3">
                  <p className="text-muted">Formula gen</p>
                  <p className="mt-1 font-semibold">{modelStatus.data?.formula_generation ?? "—"}</p>
                </div>
              </div>
            </AppPanel>

            <AppPanel className="px-4 py-4">
              <SectionTitle eyebrow="Инструменты" title="Быстрые переходы" description="Рой, пакеты знаний и quality живут в едином workspace drawer." />
              <div className="mt-4 grid gap-2">
                <Button type="button" variant="outline" className="justify-start rounded-2xl" onClick={() => openWorkspace("swarm")}>
                  <Sparkles className="mr-2 h-4 w-4" />
                  Открыть монитор роя
                </Button>
                <Button type="button" variant="outline" className="justify-start rounded-2xl" onClick={() => openWorkspace("quality")}>
                  <PanelRightOpen className="mr-2 h-4 w-4" />
                  Открыть качество и benchmark
                </Button>
              </div>
            </AppPanel>
          </div>
        </div>
      </SheetContent>
    </Sheet>
  );
}
