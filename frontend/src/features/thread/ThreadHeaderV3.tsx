import { ChevronLeft, PanelRightOpen, Plus, Settings2 } from "lucide-react";
import { Button } from "@/components/ui/button";
import { useChatStore } from "@/store/useChatStore";
import { useShellStore } from "@/store/useShellStore";

export function ThreadHeaderV3({ mobile = false }: { mobile?: boolean }) {
  const sessions = useChatStore((s) => s.sessions);
  const currentSessionId = useChatStore((s) => s.currentSessionId);
  const messages = useChatStore((s) => s.messages);
  const model = useChatStore((s) => s.model);
  const addSession = useChatStore((s) => s.addSession);
  const primarySurface = useShellStore((s) => s.primarySurface);
  const setPrimarySurface = useShellStore((s) => s.setPrimarySurface);
  const openWorkspace = useShellStore((s) => s.openWorkspace);
  const openSettings = useShellStore((s) => s.openSettings);

  const activeSession = sessions.find((session) => session.id === currentSessionId);
  const messageCount = (messages[currentSessionId] ?? []).length;

  const handleAddSession = () => {
    addSession();
    if (mobile) {
      setPrimarySurface("thread");
    }
  };

  return (
    <header
      className={mobile
        ? "flex shrink-0 items-center justify-between gap-3 border-b border-foreground/6 bg-background px-4 py-3"
        : "sticky top-0 z-10 flex shrink-0 items-center justify-between gap-3 border-b border-foreground/6 bg-card/96 px-6 py-4 supports-[backdrop-filter]:bg-card/88 supports-[backdrop-filter]:backdrop-blur-xl"}
    >
      <div className="min-w-0">
        {mobile && primarySurface === "thread" ? (
          <Button
            type="button"
            variant="ghost"
            className="-ml-3 mb-1 h-8 rounded-full px-3 text-xs text-muted hover:bg-foreground/5"
            onClick={() => setPrimarySurface("chats")}
          >
            <ChevronLeft className="mr-1 h-4 w-4" />
            Чаты
          </Button>
        ) : null}
        <p className="text-[11px] font-semibold uppercase tracking-[0.18em] text-muted">
          {mobile ? (primarySurface === "chats" ? "Чаты" : "Диалог") : "Kolibri"}
        </p>
        <h1 className="truncate text-[1rem] font-semibold tracking-[-0.04em] text-foreground lg:text-[1.15rem]">
          {primarySurface === "chats" && mobile ? "Kolibri" : activeSession?.title || "Новый диалог"}
        </h1>
        <p className="truncate text-[13px] text-muted">
          {primarySurface === "chats" && mobile
            ? `${sessions.length} диалогов`
            : messageCount
              ? `${messageCount} сообщ. • ${model}`
              : `${model}`}
        </p>
      </div>
      <div className="flex items-center gap-1.5 lg:gap-2">
        <Button type="button" variant="ghost" size="icon" className="h-10 w-10 rounded-full hover:bg-foreground/5 lg:h-9 lg:w-9" onClick={handleAddSession}>
          <Plus className="h-4.5 w-4.5" />
        </Button>
        <Button
          type="button"
          variant="ghost"
          size="icon"
          className="h-10 w-10 rounded-full hover:bg-foreground/5 lg:h-9 lg:w-9"
          onClick={() => openWorkspace("swarm")}
        >
          <PanelRightOpen className="h-4.5 w-4.5" />
        </Button>
        <Button type="button" variant="ghost" size="icon" className="h-10 w-10 rounded-full hover:bg-foreground/5 lg:h-9 lg:w-9" onClick={openSettings}>
          <Settings2 className="h-4.5 w-4.5" />
        </Button>
      </div>
    </header>
  );
}
