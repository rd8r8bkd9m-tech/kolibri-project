import { useEffect, useMemo, useRef } from "react";
import { MessageBubble } from "@/components/layout/MessageBubble";
import { ThinkingIndicator } from "@/components/layout/ThinkingIndicator";
import { useConversationTurnsQuery } from "@/features/account/query";
import { useAutoScroll } from "@/hooks/useAutoScroll";
import { useStreaming } from "@/hooks/useStreaming";
import { ComposerBarV3 } from "@/features/composer/ComposerBarV3";
import { ThreadHeaderV3 } from "@/features/thread/ThreadHeaderV3";
import { normalizeConversationMessages, useChatStore } from "@/store/useChatStore";
import type { ChatMessage } from "@/types";

const quickPrompts = [
  "Объясни архитектуру Kolibri простым языком",
  "Что ты уже умеешь",
  "Сравни право и мораль",
  "Научи рой новому знанию",
];

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
    if (!payload.items.length) return;

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
        <div className="min-h-0 flex-1 overflow-y-auto overscroll-contain px-4 py-6 [scrollbar-gutter:stable] lg:px-8 lg:py-8">
          <div className="mx-auto flex min-h-full w-full max-w-3xl flex-col justify-center text-center lg:max-w-[52rem] lg:justify-end lg:pb-14">
            <p className="text-[11px] font-semibold uppercase tracking-[0.22em] text-muted">Kolibri V3</p>
            <h2 className="mt-3 text-[1.55rem] font-semibold tracking-[-0.06em] text-foreground lg:text-[2rem]">
              {emptyTitle}
            </h2>
            <p className="mx-auto mt-3 max-w-xl text-sm leading-7 text-muted lg:text-base">
              {emptyDescription}
            </p>
            <div className="mt-6 grid gap-3 sm:grid-cols-2 lg:mx-auto lg:max-w-[46rem]">
              {visibleQuickPrompts.map((prompt) => (
                <button
                  key={prompt}
                  type="button"
                  className="rounded-[1.1rem] border border-foreground/8 bg-background px-4 py-3.5 text-left text-sm font-medium text-foreground transition hover:border-foreground/14 hover:bg-card"
                  onClick={() => void send(prompt)}
                >
                  {prompt}
                </button>
              ))}
            </div>
          </div>
        </div>
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
