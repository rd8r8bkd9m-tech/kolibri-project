import { startTransition, useDeferredValue, useEffect, useMemo, useRef, useState } from "react";
import { MessageSquareText, MoreHorizontal, Pin, Plus, Search, Settings2, Trash2 } from "lucide-react";
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu";
import { Button } from "@/components/ui/button";
import { Sheet, SheetContent } from "@/components/ui/sheet";
import { AppPanel, SectionTitle } from "@/features/ui-system/surface";
import { cn } from "@/lib/utils";
import { useChatStore } from "@/store/useChatStore";
import { useShellStore } from "@/store/useShellStore";
import type { ChatSession } from "@/types";

function formatTimeMeta(updatedAt: number) {
  return new Intl.DateTimeFormat("ru-RU", {
    hour: "2-digit",
    minute: "2-digit",
  }).format(new Date(updatedAt));
}

function formatRelativeDate(updatedAt: number) {
  const now = new Date();
  const date = new Date(updatedAt);
  const startOfToday = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime();
  const target = new Date(date.getFullYear(), date.getMonth(), date.getDate()).getTime();
  const diff = Math.round((startOfToday - target) / 86_400_000);
  if (diff <= 0) return "Сегодня";
  if (diff === 1) return "Вчера";
  if (diff <= 7) return "Неделя";
  return "Ранее";
}

function SessionRow({
  session,
  preview,
  active,
  editing,
  draftTitle,
  onSelect,
  onStartEdit,
  onCommitEdit,
  onDraftChange,
  onPin,
  onDelete,
}: {
  session: ChatSession;
  preview: string;
  active: boolean;
  editing: boolean;
  draftTitle: string;
  onSelect: () => void;
  onStartEdit: () => void;
  onCommitEdit: () => void;
  onDraftChange: (value: string) => void;
  onPin: () => void;
  onDelete: () => void;
}) {
  return (
    <div
      className={cn(
        "group flex items-start gap-3 rounded-[1.25rem] border px-3 py-3 transition-colors",
        active
          ? "border-foreground/10 bg-card shadow-[0_10px_30px_rgba(15,23,42,0.06)]"
          : "border-transparent bg-transparent hover:border-foreground/6 hover:bg-card/70",
      )}
    >
      <button type="button" onClick={onSelect} className="flex min-w-0 flex-1 items-start gap-3 text-left">
        <span
          className={cn(
            "inline-flex h-11 w-11 shrink-0 items-center justify-center rounded-full border text-[12px] font-semibold",
            active
              ? "v3-accent-surface v3-accent-text"
              : "border-foreground/8 bg-background text-muted",
          )}
        >
          {session.title.trim().slice(0, 1).toUpperCase() || "K"}
        </span>
        <span className="min-w-0 flex-1">
          {editing ? (
            <input
              autoFocus
              value={draftTitle}
              onChange={(event) => onDraftChange(event.target.value)}
              onBlur={onCommitEdit}
              onKeyDown={(event) => {
                if (event.key === "Enter") {
                  event.preventDefault();
                  onCommitEdit();
                }
                if (event.key === "Escape") {
                  event.preventDefault();
                  onDraftChange(session.title);
                  onCommitEdit();
                }
              }}
              className="w-full rounded-xl border border-foreground/10 bg-background px-3 py-2 text-sm font-semibold outline-none"
            />
          ) : (
            <>
              <span className="flex items-start justify-between gap-3">
                <span className="min-w-0 truncate text-[14px] font-semibold tracking-[-0.02em] text-foreground">
                  {session.title || "Новый чат"}
                </span>
                <span className="shrink-0 text-[11px] text-muted">{formatTimeMeta(session.updatedAt)}</span>
              </span>
              <span className="mt-1.5 flex items-center gap-1.5 text-[12px] leading-5 text-muted">
                {session.pinned ? <Pin className="h-3.5 w-3.5" /> : <MessageSquareText className="h-3.5 w-3.5" />}
                <span className="truncate">{preview || "Пустой диалог"}</span>
              </span>
            </>
          )}
        </span>
      </button>
      {!editing ? (
        <DropdownMenu>
          <DropdownMenuTrigger asChild>
            <Button
              variant="ghost"
              size="icon"
              className={cn(
                "mt-0.5 h-8 w-8 rounded-full text-muted transition-opacity",
                active ? "opacity-100" : "opacity-100 md:opacity-0 md:group-hover:opacity-100",
              )}
              aria-label={`Действия чата ${session.title || "Новый чат"}`}
            >
              <MoreHorizontal className="h-4 w-4" />
            </Button>
          </DropdownMenuTrigger>
          <DropdownMenuContent align="end">
            <DropdownMenuItem onClick={onStartEdit}>Переименовать</DropdownMenuItem>
            <DropdownMenuItem onClick={onPin}>{session.pinned ? "Открепить" : "Закрепить"}</DropdownMenuItem>
            <DropdownMenuItem onClick={onDelete}>
              <Trash2 className="mr-2 h-4 w-4" />
              Удалить
            </DropdownMenuItem>
          </DropdownMenuContent>
        </DropdownMenu>
      ) : null}
    </div>
  );
}

export function ThreadSidebarV3({ mobile = false }: { mobile?: boolean }) {
  const sessions = useChatStore((s) => s.sessions);
  const messages = useChatStore((s) => s.messages);
  const currentSessionId = useChatStore((s) => s.currentSessionId);
  const addSession = useChatStore((s) => s.addSession);
  const selectSession = useChatStore((s) => s.selectSession);
  const renameSession = useChatStore((s) => s.renameSession);
  const pinSession = useChatStore((s) => s.pinSession);
  const deleteSession = useChatStore((s) => s.deleteSession);
  const setPrimarySurface = useShellStore((s) => s.setPrimarySurface);
  const openSettings = useShellStore((s) => s.openSettings);
  const [query, setQuery] = useState("");
  const [editingId, setEditingId] = useState<string | null>(null);
  const [draftTitle, setDraftTitle] = useState("");
  const [deleteTarget, setDeleteTarget] = useState<ChatSession | null>(null);
  const searchRef = useRef<HTMLInputElement | null>(null);
  const deferredQuery = useDeferredValue(query.trim().toLowerCase());

  useEffect(() => {
    const handleKeydown = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement | null;
      const isEditable =
        target instanceof HTMLInputElement ||
        target instanceof HTMLTextAreaElement ||
        target?.isContentEditable;

      if ((event.metaKey || event.ctrlKey) && event.shiftKey && event.key.toLowerCase() === "f") {
        event.preventDefault();
        searchRef.current?.focus();
        searchRef.current?.select();
        return;
      }

      if (!mobile && !isEditable && !event.metaKey && !event.ctrlKey && !event.altKey && event.key.toLowerCase() === "n") {
        event.preventDefault();
        handleCreate();
      }
    };

    window.addEventListener("keydown", handleKeydown);
    return () => window.removeEventListener("keydown", handleKeydown);
  }, [mobile, sessions.length]);

  const sortedSessions = useMemo(() => {
    const base = [...sessions].sort((left, right) => {
      if (Boolean(right.pinned) !== Boolean(left.pinned)) {
        return Number(Boolean(right.pinned)) - Number(Boolean(left.pinned));
      }
      return right.updatedAt - left.updatedAt;
    });

    if (!deferredQuery) return base;

    return base.filter((session) => {
      const preview = (messages[session.id] ?? [])
        .map((message) => message.content)
        .join(" ")
        .toLowerCase();
      return `${session.title} ${preview}`.toLowerCase().includes(deferredQuery);
    });
  }, [deferredQuery, messages, sessions]);

  const pinned = sortedSessions.filter((session) => session.pinned);
  const recentGroups = useMemo(() => {
    const grouped = new Map<string, ChatSession[]>();
    for (const session of sortedSessions.filter((item) => !item.pinned)) {
      const label = formatRelativeDate(session.updatedAt);
      const list = grouped.get(label) ?? [];
      list.push(session);
      grouped.set(label, list);
    }
    return Array.from(grouped.entries());
  }, [sortedSessions]);

  const handleSelect = (id: string) => {
    selectSession(id);
    if (mobile) {
      startTransition(() => setPrimarySurface("thread"));
    }
  };

  const handleCreate = () => {
    addSession();
    if (mobile) {
      startTransition(() => setPrimarySurface("thread"));
    }
  };

  const renderRow = (session: ChatSession) => {
    const preview = (messages[session.id] ?? []).at(-1)?.content ?? "Новый диалог";
    return (
      <SessionRow
        key={session.id}
        session={session}
        preview={preview}
        active={session.id === currentSessionId}
        editing={editingId === session.id}
        draftTitle={draftTitle}
        onSelect={() => handleSelect(session.id)}
        onStartEdit={() => {
          setEditingId(session.id);
          setDraftTitle(session.title || "Новый чат");
        }}
        onCommitEdit={() => {
          if (editingId === session.id) {
            renameSession(session.id, draftTitle.trim() || "Новый чат");
          }
          setEditingId(null);
          setDraftTitle("");
        }}
        onDraftChange={setDraftTitle}
        onPin={() => pinSession(session.id)}
        onDelete={() => setDeleteTarget(session)}
      />
    );
  };

  const body = (
    <div className="flex h-full min-h-0 flex-col gap-4">
      <AppPanel
        className={cn(
          "px-4 py-4",
          !mobile && "rounded-none border-x-0 border-t-0 border-b border-foreground/6 bg-transparent px-4 py-4 shadow-none",
        )}
      >
        <div className="flex items-start justify-between gap-3">
          <SectionTitle
            eyebrow="Kolibri"
            title="Чаты"
            description={mobile ? "Главный вход в диалог. Память, обучение и инструменты живут во вторичных surfaces." : undefined}
          />
          <div className="flex items-center gap-2">
            <Button type="button" variant="ghost" size="icon" className="h-10 w-10 rounded-full" onClick={openSettings}>
              <Settings2 className="h-4.5 w-4.5" />
            </Button>
            <Button type="button" size="icon" className="h-10 w-10 rounded-full" onClick={handleCreate}>
              <Plus className="h-4.5 w-4.5" />
            </Button>
          </div>
        </div>
        <div className={cn("flex items-center gap-3 rounded-[1.2rem] border border-foreground/8 bg-background px-3 py-3", mobile ? "mt-4" : "mt-3")}>
          <Search className="h-4 w-4 text-muted" />
          <input
            ref={searchRef}
            value={query}
            onChange={(event) => setQuery(event.target.value)}
            placeholder="Поиск по чатам"
            aria-label="Поиск по чатам"
            className="min-w-0 flex-1 bg-transparent text-sm text-foreground outline-none placeholder:text-muted"
          />
        </div>
      </AppPanel>

      <div className={cn("min-h-0 flex-1 overflow-y-auto pb-5", mobile ? "px-3" : "px-2.5 pt-3")}>
        <div className="space-y-5">
          {pinned.length ? (
            <div className="space-y-2">
              <p className="px-1 text-[11px] font-semibold uppercase tracking-[0.18em] text-muted">Закреплённые</p>
              <div className="space-y-2">{pinned.map(renderRow)}</div>
            </div>
          ) : null}

          {recentGroups.map(([label, list]) => (
            <div key={label} className="space-y-2">
              <p className="px-1 text-[11px] font-semibold uppercase tracking-[0.18em] text-muted">{label}</p>
              <div className="space-y-2">{list.map(renderRow)}</div>
            </div>
          ))}

          {!sortedSessions.length ? (
            <AppPanel className="px-4 py-5 text-center">
              <p className="text-sm font-medium text-foreground">Ничего не найдено</p>
              <p className="mt-1 text-sm text-muted">Сбрось поиск или создай новый диалог.</p>
            </AppPanel>
          ) : null}
        </div>
      </div>
    </div>
  );

  const deleteConfirmation = (
    <Sheet open={Boolean(deleteTarget)} onOpenChange={(open) => (!open ? setDeleteTarget(null) : undefined)}>
      <SheetContent
        title="Удаление чата"
        description="Подтверждение удаления диалога"
        className={
          mobile
            ? "left-0 right-0 top-auto z-50 h-auto max-w-none rounded-t-[1.75rem] border-r-0 border-t bg-background px-4 pb-[calc(env(safe-area-inset-bottom)+1rem)] pt-4"
            : "left-auto right-6 top-24 z-50 h-auto w-[24rem] max-w-[24rem] rounded-[1.5rem] border border-foreground/8 bg-background px-4 pb-4 pt-4"
        }
      >
        <div className="space-y-4">
          <div>
            <p className="text-base font-semibold text-foreground">Удалить чат?</p>
            <p className="mt-1 text-sm text-muted">
              {deleteTarget ? `Диалог «${deleteTarget.title || "Новый чат"}» будет удалён без возможности восстановления.` : ""}
            </p>
          </div>
          <div className="grid grid-cols-2 gap-3">
            <Button type="button" variant="outline" className="rounded-2xl" onClick={() => setDeleteTarget(null)}>
              Отмена
            </Button>
            <Button
              type="button"
              className="rounded-2xl bg-red-600 text-white hover:bg-red-500"
              onClick={() => {
                if (!deleteTarget) return;
                deleteSession(deleteTarget.id);
                setDeleteTarget(null);
              }}
            >
              Удалить
            </Button>
          </div>
        </div>
      </SheetContent>
    </Sheet>
  );

  return mobile ? (
    <section className="flex min-h-0 flex-1 flex-col overflow-hidden bg-background">
      {body}
      {deleteConfirmation}
    </section>
  ) : (
    <>
      <aside className="hidden min-h-0 w-[18.75rem] shrink-0 border-r border-foreground/6 bg-sidebar lg:flex lg:flex-col">
        {body}
      </aside>
      {deleteConfirmation}
    </>
  );
}
