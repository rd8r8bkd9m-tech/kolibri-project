import { useMemo, useRef } from "react";
import { MessageBubble } from "@/components/layout/MessageBubble";
import { ThinkingIndicator } from "@/components/layout/ThinkingIndicator";
import { useAutoScroll } from "@/hooks/useAutoScroll";
import { useStreaming } from "@/hooks/useStreaming";
import { ComposerBarV3 } from "@/features/composer/ComposerBarV3";
import { ThreadHeaderV3 } from "@/features/thread/ThreadHeaderV3";
import { useChatStore } from "@/store/useChatStore";

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
  const { send } = useStreaming();

  const messages = useMemo(() => messagesBySession[currentSessionId] ?? [], [currentSessionId, messagesBySession]);
  const visibleQuickPrompts = mobile ? quickPrompts.slice(0, 2) : quickPrompts;
  const emptyTitle = mobile ? "Чат, в котором удобно работать" : "О чём поговорим?";
  const emptyDescription = mobile
    ? "Основной сценарий здесь один: диалог. Обучение, рой, голос, пакеты знаний и качество убраны во вторичные инструменты и не ломают работу с чатом."
    : "Kolibri отвечает в чате, запоминает контекст диалога и открывает инструменты только тогда, когда они действительно нужны.";
  const lastContentLength = messages[messages.length - 1]?.content.length ?? 0;
  const waitingForAssistant = thinking && (!messages.length || messages[messages.length - 1]?.role === "user");

  useAutoScroll(viewportRef, [messages.length, lastContentLength, thinking]);

  return (
    <section className="grid h-full min-h-0 min-w-0 flex-1 grid-rows-[auto_minmax(0,1fr)_auto] overflow-hidden bg-background">
      <ThreadHeaderV3 mobile={mobile} />
      {!messages.length ? (
        <div className="flex min-h-0 flex-1 items-center justify-center overflow-y-auto px-4 py-6 lg:px-8 lg:py-10">
          <div className="mx-auto w-full max-w-2xl text-center lg:max-w-3xl">
            <p className="text-[11px] font-semibold uppercase tracking-[0.22em] text-muted">Kolibri V3</p>
            <h2 className="mt-3 text-[1.55rem] font-semibold tracking-[-0.06em] text-foreground lg:text-[2.2rem]">
              {emptyTitle}
            </h2>
            <p className="mx-auto mt-3 max-w-xl text-sm leading-7 text-muted lg:text-base">
              {emptyDescription}
            </p>
            <div className="mt-6 grid gap-3 sm:grid-cols-2 lg:mx-auto lg:max-w-2xl">
              {visibleQuickPrompts.map((prompt) => (
                <button
                  key={prompt}
                  type="button"
                  className="rounded-[1.1rem] border border-foreground/8 bg-card px-4 py-3.5 text-left text-sm font-medium text-foreground transition hover:border-foreground/12 hover:bg-card"
                  onClick={() => void send(prompt)}
                >
                  {prompt}
                </button>
              ))}
            </div>
          </div>
        </div>
      ) : (
        <div ref={viewportRef} className="min-h-0 flex-1 overflow-y-auto px-4 py-5 lg:px-6 lg:py-6">
          <div className="mx-auto max-w-4xl">
            <div className="mb-5 flex justify-center">
              <span className="rounded-full border border-foreground/8 bg-card px-3 py-1 text-[11px] text-muted">
                {new Date().toLocaleDateString("ru-RU", { day: "numeric", month: "long" })}
              </span>
            </div>
            {messages.map((message) => (
              <MessageBubble key={message.id} message={message} />
            ))}
            {waitingForAssistant ? <ThinkingIndicator /> : null}
          </div>
        </div>
      )}
      <ComposerBarV3 />
    </section>
  );
}
