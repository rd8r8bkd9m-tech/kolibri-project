import { useEffect, useMemo, useState } from "react";
import { useQueryClient } from "@tanstack/react-query";
import {
  KeyRound,
  Laptop,
  LogIn,
  LogOut,
  Moon,
  PanelRightOpen,
  Save,
  Sparkles,
  Sun,
  UserRound,
  X,
} from "lucide-react";
import { Button } from "@/components/ui/button";
import { Switch } from "@/components/ui/switch";
import { Sheet, SheetContent } from "@/components/ui/sheet";
import {
  loginAccount,
  logoutAccount,
  updateAccountPreferences,
  updateAccountProfile,
} from "@/api";
import {
  accountQueryKeys,
} from "@/features/account/query";
import { useAccountBootstrap } from "@/features/account/useAccountBootstrap";
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
  { key: "assistant", label: "Ассистент", description: "Спокойный рабочий тон." },
  { key: "romantic", label: "Романтик", description: "Более тёплый и мягкий стиль." },
  { key: "storyteller", label: "Рассказчик", description: "Развёрнутые ответы и примеры." },
];

export function SettingsDrawerV3() {
  const queryClient = useQueryClient();
  const open = useShellStore((s) => s.settingsOpen);
  const closeSettings = useShellStore((s) => s.closeSettings);
  const openWorkspace = useShellStore((s) => s.openWorkspace);
  const theme = useChatStore((s) => s.theme);
  const setTheme = useChatStore((s) => s.setTheme);
  const model = useChatStore((s) => s.model);
  const setModel = useChatStore((s) => s.setModel);
  const persona = useChatStore((s) => s.persona);
  const setPersona = useChatStore((s) => s.setPersona);
  const memoryEnabled = useChatStore((s) => s.memoryEnabled);
  const setMemoryEnabled = useChatStore((s) => s.setMemoryEnabled);
  const apiToken = useChatStore((s) => s.apiToken);
  const setApiToken = useChatStore((s) => s.setApiToken);

  const { authStatus, profileQuery } = useAccountBootstrap();
  const modelStatus = useModelStatusQuery();

  const [loginUsername, setLoginUsername] = useState("admin");
  const [loginPassword, setLoginPassword] = useState("");
  const [loginLoading, setLoginLoading] = useState(false);
  const [profileName, setProfileName] = useState("");
  const [profileFacts, setProfileFacts] = useState("");
  const [profileSaving, setProfileSaving] = useState(false);
  const [preferenceSaving, setPreferenceSaving] = useState<null | "theme" | "persona" | "memory" | "model">(null);
  const [authError, setAuthError] = useState("");

  useEffect(() => {
    if (!profileQuery.data) return;
    setProfileName(profileQuery.data.name || "");
    setProfileFacts((profileQuery.data.facts || []).join("\n"));
  }, [profileQuery.data?.facts, profileQuery.data?.name]);

  const loginVisible = useMemo(
    () => Boolean(authStatus.data?.auth_enabled && !authStatus.data?.authenticated),
    [authStatus.data?.auth_enabled, authStatus.data?.authenticated],
  );

  const invalidateAccountState = async () => {
    await Promise.all([
      queryClient.invalidateQueries({ queryKey: accountQueryKeys.authStatus }),
      queryClient.invalidateQueries({ queryKey: accountQueryKeys.accountProfile }),
      queryClient.invalidateQueries({ queryKey: accountQueryKeys.accountPreferences }),
      queryClient.invalidateQueries({ queryKey: accountQueryKeys.conversationSessions }),
    ]);
  };

  const submitLogin = async () => {
    if (!loginUsername.trim() || !loginPassword.trim()) return;
    setLoginLoading(true);
    setAuthError("");
    try {
      const result = await loginAccount({
        username: loginUsername.trim(),
        password: loginPassword,
      });
      setApiToken(result.access_token);
      setLoginPassword("");
      await invalidateAccountState();
    } catch (error) {
      setAuthError(error instanceof Error ? error.message : "Не удалось выполнить вход.");
    } finally {
      setLoginLoading(false);
    }
  };

  const submitLogout = async () => {
    setApiToken("");
    setAuthError("");
    await logoutAccount();
    await invalidateAccountState();
  };

  const saveProfile = async () => {
    setProfileSaving(true);
    try {
      await updateAccountProfile({
        name: profileName.trim(),
        facts: profileFacts
          .split("\n")
          .map((item) => item.trim())
          .filter(Boolean),
      });
      await invalidateAccountState();
    } finally {
      setProfileSaving(false);
    }
  };

  const savePreferences = async (payload: {
    theme?: ThemeMode;
    persona?: AssistantPersona;
    memory_enabled?: boolean;
    model?: string;
  }, kind: "theme" | "persona" | "memory" | "model") => {
    setPreferenceSaving(kind);
    try {
      await updateAccountPreferences(payload);
      await invalidateAccountState();
    } finally {
      setPreferenceSaving(null);
    }
  };

  return (
    <Sheet open={open} onOpenChange={(next) => (!next ? closeSettings() : undefined)}>
      <SheetContent
        title="Настройки"
        description="Аккаунт, профиль, предпочтения и рабочие поверхности"
        className="left-auto right-0 z-50 w-full max-w-[32rem] border-l border-r-0 bg-background px-5 pb-5 pt-5 max-lg:inset-x-0 max-lg:left-0 max-lg:right-0 max-lg:top-auto max-lg:h-[88dvh] max-lg:max-h-[88svh] max-lg:max-w-none max-lg:rounded-t-[1.9rem] max-lg:border-l-0 max-lg:border-t max-lg:px-4 max-lg:pb-[calc(env(safe-area-inset-bottom)+1rem)]"
      >
        <div className="flex h-full min-h-0 flex-col gap-5">
          <div className="flex items-start justify-between gap-4 border-b border-foreground/6 pb-4">
            <SectionTitle
              eyebrow="Настройки"
              title="Аккаунт и предпочтения"
              description="V3 shell поднимает аккаунт, профиль и предпочтения из серверного состояния сразу при загрузке."
            />
            <Button type="button" variant="ghost" size="icon" className="h-10 w-10 rounded-full" onClick={closeSettings}>
              <X className="h-4.5 w-4.5" />
            </Button>
          </div>

          <div className="min-h-0 flex-1 space-y-4 overflow-y-auto pr-1">
            <AppPanel className="px-4 py-4">
              <SectionTitle eyebrow="Аккаунт" title="Статус доступа" description="Логин, роль и серверная идентичность для чатов, профиля и настроек." />
              <div className="mt-4 space-y-3">
                <div className="rounded-[1.2rem] border border-foreground/8 bg-background px-4 py-3">
                  <div className="flex items-center justify-between gap-3">
                    <div>
                      <p className="text-sm font-semibold text-foreground">
                        {authStatus.data?.authenticated
                          ? authStatus.data?.user || "Авторизован"
                          : authStatus.data?.auth_enabled
                            ? "Нужен вход"
                            : "Локальный режим"}
                      </p>
                      <p className="mt-1 text-xs text-muted">
                        {authStatus.isLoading
                          ? "Проверяю статус…"
                          : authStatus.data?.authenticated
                            ? `Роль: ${authStatus.data?.role || "user"} • account_id: ${authStatus.data?.account_id || "—"}`
                            : authStatus.data?.auth_enabled
                              ? "Без входа сервер всё равно хранит профиль и настройки по client_id браузера."
                              : `Авторизация отключена • account_id: ${authStatus.data?.account_id || "global"}`}
                      </p>
                    </div>
                    <span className="rounded-full border border-foreground/8 bg-card px-3 py-1 text-[11px] font-semibold uppercase tracking-[0.16em] text-muted">
                      {authStatus.data?.authenticated ? "в сети" : authStatus.data?.auth_enabled ? "гость" : "локально"}
                    </span>
                  </div>
                </div>

                {loginVisible ? (
                  <div className="space-y-3 rounded-[1.2rem] border border-foreground/8 bg-background px-4 py-4">
                    <div className="flex items-center gap-2 text-sm font-semibold text-foreground">
                      <KeyRound className="h-4 w-4" />
                      Войти в аккаунт
                    </div>
                    <input
                      value={loginUsername}
                      onChange={(event) => setLoginUsername(event.target.value)}
                      className="v3-input"
                      placeholder="Логин"
                    />
                    <input
                      value={loginPassword}
                      onChange={(event) => setLoginPassword(event.target.value)}
                      className="v3-input"
                      type="password"
                      placeholder="Пароль"
                    />
                    {authError ? <p className="text-sm text-red-500">{authError}</p> : null}
                    <Button type="button" className="w-full rounded-2xl" disabled={loginLoading} onClick={() => void submitLogin()}>
                      <LogIn className="mr-2 h-4 w-4" />
                      {loginLoading ? "Выполняю вход…" : "Войти"}
                    </Button>
                  </div>
                ) : authStatus.data?.authenticated ? (
                  <Button type="button" variant="outline" className="w-full justify-start rounded-2xl" onClick={() => void submitLogout()}>
                    <LogOut className="mr-2 h-4 w-4" />
                    Выйти из аккаунта
                  </Button>
                ) : null}
              </div>
            </AppPanel>

            <AppPanel className="px-4 py-4">
              <SectionTitle eyebrow="Профиль" title="Имя и факты" description="Профиль пользователя хранится на сервере и используется в памяти диалога." />
              <div className="mt-4 space-y-3">
                <input
                  value={profileName}
                  onChange={(event) => setProfileName(event.target.value)}
                  className="v3-input"
                  placeholder="Как вас называть"
                />
                <textarea
                  value={profileFacts}
                  onChange={(event) => setProfileFacts(event.target.value)}
                  className="v3-input min-h-[8rem] resize-y"
                  placeholder="Каждый факт с новой строки. Например: Меня зовут Владислав"
                />
                <div className="flex items-center justify-between rounded-[1.2rem] border border-foreground/8 bg-background px-4 py-3 text-sm">
                  <div>
                    <p className="font-semibold text-foreground">Документы в памяти</p>
                    <p className="mt-1 text-xs text-muted">Факты и обучающие материалы, связанные с текущим account_id.</p>
                  </div>
                  <span className="text-base font-semibold text-foreground">{profileQuery.data?.documents_count ?? "—"}</span>
                </div>
                <Button type="button" className="w-full rounded-2xl" disabled={profileSaving} onClick={() => void saveProfile()}>
                  <Save className="mr-2 h-4 w-4" />
                  {profileSaving ? "Сохраняю профиль…" : "Сохранить профиль"}
                </Button>
              </div>
            </AppPanel>

            <AppPanel className="px-4 py-4">
              <SectionTitle eyebrow="Внешний вид" title="Тема" description="Настройки темы сохраняются на сервере и переживают перезагрузку страницы и перезапуск backend." />
              <div className="mt-4 grid grid-cols-3 gap-2">
                {themeOptions.map((option) => {
                  const Icon = option.icon;
                  return (
                    <button
                      key={option.key}
                      type="button"
                      disabled={preferenceSaving === "theme"}
                      onClick={() => {
                        setTheme(option.key);
                        void savePreferences({ theme: option.key }, "theme");
                      }}
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
              <SectionTitle eyebrow="Диалог" title="Персона и память" description="Предпочтения синхронизируются с серверным runtime и влияют на обычный чат." />
              <div className="mt-4 space-y-2">
                {personaOptions.map((option) => (
                  <button
                    key={option.key}
                    type="button"
                    disabled={preferenceSaving === "persona"}
                    onClick={() => {
                      setPersona(option.key);
                      void savePreferences({ persona: option.key }, "persona");
                    }}
                    className={`w-full rounded-[1.2rem] border px-4 py-3 text-left transition ${
                      persona === option.key ? "v3-accent-surface" : "border-foreground/8 bg-background"
                    }`}
                  >
                    <span className="block text-sm font-semibold text-foreground">{option.label}</span>
                    <span className="mt-1 block text-xs text-muted">{option.description}</span>
                  </button>
                ))}
              </div>
              <div className="mt-4 flex items-center justify-between rounded-[1.2rem] border border-foreground/8 bg-background px-4 py-3">
                <div>
                  <p className="text-sm font-semibold text-foreground">Память</p>
                  <p className="text-xs text-muted">Фоновая персональная память для обычного чат-контура.</p>
                </div>
                <Switch
                  checked={memoryEnabled}
                  onCheckedChange={(value) => {
                    setMemoryEnabled(value);
                    void savePreferences({ memory_enabled: value }, "memory");
                  }}
                />
              </div>
            </AppPanel>

            <AppPanel className="px-4 py-4">
              <SectionTitle eyebrow="Контур" title="Текущая модель" description="Статус модели приходит с сервера, а UI только отображает его и сохраняет пользовательское предпочтение." />
              <div className="mt-4 rounded-[1.2rem] border border-foreground/8 bg-background px-4 py-3">
                <p className="text-sm font-semibold text-foreground">{model}</p>
                <p className="mt-1 text-xs text-muted">{modelStatus.data?.primary_model ?? "Статус модели загружается…"}</p>
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
              <SectionTitle eyebrow="Инструменты" title="Быстрые переходы" description="Рой, пакеты знаний и quality остаются вторичными рабочими поверхностями." />
              <div className="mt-4 grid gap-2">
                <Button type="button" variant="outline" className="justify-start rounded-2xl" onClick={() => openWorkspace("swarm")}>
                  <Sparkles className="mr-2 h-4 w-4" />
                  Открыть монитор роя
                </Button>
                <Button type="button" variant="outline" className="justify-start rounded-2xl" onClick={() => openWorkspace("quality")}>
                  <PanelRightOpen className="mr-2 h-4 w-4" />
                  Открыть качество и benchmark
                </Button>
                <Button type="button" variant="outline" className="justify-start rounded-2xl" onClick={() => openWorkspace("packs")}>
                  <UserRound className="mr-2 h-4 w-4" />
                  Открыть пакеты знаний
                </Button>
              </div>
            </AppPanel>
          </div>
        </div>
      </SheetContent>
    </Sheet>
  );
}
