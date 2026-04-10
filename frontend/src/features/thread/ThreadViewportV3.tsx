import { useEffect, useMemo, useRef, useState } from "react";
import { MessageBubble } from "@/components/layout/MessageBubble";
import { ThinkingIndicator } from "@/components/layout/ThinkingIndicator";
import { useConversationTurnsQuery } from "@/features/account/query";
import { useAutoScroll } from "@/hooks/useAutoScroll";
import { useStreaming } from "@/hooks/useStreaming";
import { ComposerBarV3 } from "@/features/composer/ComposerBarV3";
import { ThreadHeaderV3 } from "@/features/thread/ThreadHeaderV3";
import { normalizeConversationMessages, useChatStore } from "@/store/useChatStore";
import type { ChatMessage } from "@/types";
import { Brain, Calculator, BookOpen, Zap, MessageCircle } from "lucide-react";
import { cn } from "@/lib/utils";

const quickPrompts = [
  { text: "Составь смету на ремонт квартиры 60 м²", icon: Calculator },
  { text: "Распиши этапы проекта ремонта офиса", icon: BookOpen },
  { text: "Объясни архитектуру Kolibri простым языком", icon: Brain },
  { text: "Что ты уже умеешь", icon: Zap },
  { text: "Научи рой новому знанию", icon: MessageCircle },
];

const timedPrompts = [
  { text: "Расскажи интересные факты о науке", hour: 8 }, // утро
  { text: "Столица Австралии?", hour: 12 }, // полдень
  { text: "Кто написал Войну и мир?", hour: 14 },
  { text: "Сколько планет в Солнечной системе?", hour: 16 },
  { text: "Объясни теорию относительности", hour: 18 }, // вечер
  { text: "Какая сегодня дата и день недели?", hour: 22 }, // ночь
];

function getTimedPrompt(): { text: string; icon: React.ElementType } {
  const hour = new Date().getHours();
  const closest = timedPrompts.reduce((prev, curr) =>
    Math.abs(curr.hour - hour) < Math.abs(prev.hour - hour) ? curr : prev
  );
  const icons = [Brain, BookOpen, Zap, MessageCircle, Calculator];
  return {
    text: closest.text,
    icon: icons[Math.floor(Math.random() * icons.length)],
  };
}

function EmptyState({ mobile, send }: { mobile: boolean; send: (text: string) => void }) {
  const [rotatingPrompt, setRotatingPrompt] = useState(0);
  const timedPrompt = getTimedPrompt();

  const allPrompts = useMemo(() => {
    const base = mobile ? quickPrompts.slice(0, 2) : quickPrompts;
    return [...base, { text: timedPrompt.text, icon: timedPrompt.icon }];
  }, [mobile, timedPrompt]);

  useEffect(() => {
    const interval = setInterval(() => {
      setRotatingPrompt((prev) => (prev + 1) % allPrompts.length);
    }, 8000);
    return () => clearInterval(interval);
  }, [allPrompts.length]);

  const emptyTitle = mobile ? "Чат, в котором удобно работать" : "О чём поговорим?";
  const emptyDescription = mobile
    ? "Основной сценарий здесь один: диалог. Обучение, рой, голос, пакеты знаний и качество убраны во вторичные инструменты и не ломают работу с чатом."
    : "Kolibri отвечает в чате, запоминает контекст диалога и открывает инструменты только тогда, когда они действительно нужны.";

  return (
    <div className="min-h-0 flex-1 overflow-y-auto overscroll-contain px-4 py-6 [scrollbar-gutter:stable] lg:px-8 lg:py-8">
      {/* Subtle animated background */}
      <div className="pointer-events-none absolute inset-0 overflow-hidden">
        <div className="kolibri-nebula opacity-30" />
      </div>

      <div className="relative mx-auto flex min-h-full w-full max-w-3xl flex-col justify-center text-center lg:max-w-[52rem] lg:justify-end lg:pb-14">
        <p className="text-[11px] font-semibold uppercase tracking-[0.22em] text-muted">Kolibri V3</p>
        <h2 className="mt-3 text-[1.55rem] font-semibold tracking-[-0.06em] text-foreground lg:text-[2rem]">
          {emptyTitle}
        </h2>
        <p className="mx-auto mt-3 max-w-xl text-sm leading-7 text-muted lg:text-base">
          {emptyDescription}
        </p>

        <div className="mt-6 grid gap-3 sm:grid-cols-2 lg:mx-auto lg:max-w-[46rem]">
          {allPrompts.map((prompt, index) => {
            const Icon = prompt.icon;
            const isRotating = index === rotatingPrompt;
            return (
              <button
                key={`${prompt.text}-${index}`}
                type="button"
                className={cn(
                  "group relative flex items-start gap-3 rounded-[1.1rem] border bg-background px-4 py-3.5 text-left text-sm font-medium text-foreground transition-all duration-300",
                  "border-foreground/8 hover:border-foreground/14 hover:bg-card hover:shadow-[0_8px_24px_rgba(15,23,42,0.08)]",
                  isRotating && "border-accent/20 bg-accent/5 shadow-[0_8px_24px_rgba(99,102,241,0.12)]"
                )}
                onClick={() => void send(prompt.text)}
              >
                <Icon className="mt-0.5 h-4 w-4 shrink-0 text-muted transition-colors group-hover:text-foreground" />
                <span>{prompt.text}</span>
              </button>
            );
          })}
        </div>

        <div className="mt-6 flex items-center justify-center gap-4 text-[11px] text-muted">
          <span className="inline-flex items-center gap-1">
            <kbd className="rounded border border-foreground/10 bg-background px-1.5 py-0.5 text-[10px] font-mono">
              ⌘K
            </kbd>
            фокус
          </span>
          <span className="inline-flex items-center gap-1">
            <kbd className="rounded border border-foreground/10 bg-background px-1.5 py-0.5 text-[10px] font-mono">
              N
            </kbd>
            новый чат
          </span>
          <span className="inline-flex items-center gap-1">
            <kbd className="rounded border border-foreground/10 bg-background px-1.5 py-0.5 text-[10px] font-mono">
              /
            </kbd>
            ввод
          </span>
        </div>
      </div>
    </div>
  );
}

export function ThreadViewportV3({ mobile = false }: { mobile?: boolean }) {
  const viewportRef = useRef<HTMLDivElement>(null);
  const currentSessionId = useChatStore((s) => s.currentSessionId);
  const messagesBySession = useChatStore((s) => s.messages);
  const thinking = useChatStore((s) => s.thinking);
  const hydrateMessages = useChatStore((s) => s.hydrateMessages);
  const adoptSessionId = useChatStore((s) => s.adoptSessionId);
  const { send } = useStreaming();
  const conversationTurnsQuery = useConversationTurnsQuery(currentSessionId, 160);

  const messages = useMemo(() => messagesBySession[currentSessionId] ?? [], [currentSessionId, messagesBySession]);
  const visibleQuickPrompts = mobile ? quickPrompts.slice(0, 2) : quickPrompts;
  const emptyTitle = mobile ? "Чат, в котором удобно работать" : "О чём поговорим?";
  const emptyDescription = mobile
    ? "Основной сценарий здесь один: диалог. Обучение, рой, голос, пакеты знаний и качество убраны во вторичные инструменты и не ломают работу с чатом."
    : "Kolibri отвечает в чате, запоминает контекст диалога и открывает инструменты только тогда, когда они действительно нужны.";
  const lastContentLength = messages[messages.length - 1]?.content.length ?? 0;
  const waitingForAssistant = thinking && (!messages.length || messages[messages.length - 1]?.role === "user");

  useEffect(() => {
    const payload = conversationTurnsQuery.data;
    if (!payload || thinking) return;
    if (!payload.items || !payload.items.length) return;

    const resolvedSessionId = payload.conversation_id || currentSessionId;
    const currentMessages = messagesBySession[currentSessionId] ?? [];
    const canReplaceLocal =
      resolvedSessionId !== currentSessionId ||
      currentMessages.length === 0 ||
      payload.items.length >= currentMessages.length;
    if (!canReplaceLocal) return;

    if (resolvedSessionId && resolvedSessionId !== currentSessionId) {
      adoptSessionId(currentSessionId, resolvedSessionId);
    }

    const hydratedMessages = normalizeConversationMessages(
      payload.items
        .filter((item): item is { role: "user" | "assistant"; content: string; created_at: number } =>
          item.role === "user" || item.role === "assistant",
        )
        .map((item, index) => ({
          id: `${resolvedSessionId || currentSessionId}:turn:${index}:${Math.round(item.created_at * 1000)}`,
          role: item.role,
          content: item.content,
          createdAt: Math.round(item.created_at * 1000),
        })) as ChatMessage[],
    );

    const localMessages = (messagesBySession[resolvedSessionId || currentSessionId] ?? []).filter(
      (message) => message.role === "user" || message.role === "assistant",
    );
    const alreadyHydrated =
      localMessages.length === hydratedMessages.length &&
      localMessages.every((message, index) => {
        const target = hydratedMessages[index];
        return (
          target &&
          message.role === target.role &&
          message.content === target.content &&
          message.createdAt === target.createdAt
        );
      });
    if (alreadyHydrated) return;

    hydrateMessages(resolvedSessionId || currentSessionId, hydratedMessages);
  }, [
    adoptSessionId,
    conversationTurnsQuery.data,
    currentSessionId,
    hydrateMessages,
    messagesBySession,
    thinking,
  ]);

  useAutoScroll(viewportRef, [messages.length, lastContentLength, thinking]);

  return (
    <section
      className={
        mobile
          ? "flex h-full min-h-0 min-w-0 flex-1 flex-col overflow-hidden bg-background"
          : "flex h-full min-h-0 min-w-0 flex-1 flex-col overflow-hidden rounded-[1.75rem] border border-foreground/8 bg-card shadow-[0_24px_60px_rgba(15,23,42,0.08)]"
      }
    >
      <ThreadHeaderV3 mobile={mobile} />
      {!messages.length ? (
        <EmptyState mobile={mobile} send={send} />
      ) : (
        <div
          ref={viewportRef}
          className="min-h-0 flex-1 overflow-y-auto overscroll-contain px-4 py-5 [scrollbar-gutter:stable] lg:px-8 lg:py-7"
        >
          <div className="mx-auto flex min-h-full max-w-[52rem] flex-col justify-end scroll-pb-28 lg:scroll-pb-36">
            <div className="pb-1">
              <div className="mb-5 flex justify-center">
                <span className="rounded-full border border-foreground/8 bg-card px-3 py-1 text-[11px] text-muted">
                  {new Date().toLocaleDateString("ru-RU", { day: "numeric", month: "long" })}
                </span>
              </div>
              {messages.map((message, index) => {
                const previousUserPrompt = message.role === "assistant"
                  ? [...messages.slice(0, index)].reverse().find((item) => item.role === "user")?.content
                  : undefined;
                return (
                  <MessageBubble
                    key={message.id}
                    message={message}
                    previousUserPrompt={previousUserPrompt}
                  />
                );
              })}
              {waitingForAssistant ? <ThinkingIndicator /> : null}
            </div>
          </div>
        </div>
      )}
      <ComposerBarV3 mobile={mobile} />
    </section>
  );
}
