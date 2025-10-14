import { useEffect, useMemo, useRef } from "react";
import type { ChatMessage } from "../types/chat";
import ChatMessageView from "./ChatMessage";

interface ChatViewProps {
  messages: ChatMessage[];
  isLoading: boolean;
  conversationId: string;
}

const ChatView = ({ messages, isLoading, conversationId }: ChatViewProps) => {
  const containerRef = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    const container = containerRef.current;

    if (!container) {
      return;
    }

    container.scrollTop = container.scrollHeight;
  }, [messages, isLoading]);

  const renderedMessages = useMemo(() => {
    const items: Array<JSX.Element> = [];
    let lastUserMessage: ChatMessage | undefined;
    let lastDateKey: string | undefined;

    messages.forEach((message, index) => {
      const contextUserMessage = message.role === "assistant" ? lastUserMessage : undefined;
      if (message.role === "user") {
        lastUserMessage = message;
      }

      const dateKey = message.isoTimestamp ? new Date(message.isoTimestamp).toDateString() : undefined;
      if (dateKey && dateKey !== lastDateKey) {
        lastDateKey = dateKey;
        const formattedDate = new Date(message.isoTimestamp!).toLocaleDateString("ru-RU", {
          day: "2-digit",
          month: "long",
        });
        items.push(
          <div
            key={`divider-${dateKey}-${index}`}
            className="flex items-center gap-3 text-xs uppercase tracking-[0.35em] text-text-secondary"
          >
            <span className="h-px flex-1 bg-border-strong/60" />
            <span>{formattedDate}</span>
            <span className="h-px flex-1 bg-border-strong/60" />
          </div>,
        );
      }

      items.push(
        <ChatMessageView
          key={message.id}
          message={message}
          conversationId={conversationId}
          latestUserMessage={contextUserMessage}
        />,
      );
    });

    return items;
  }, [conversationId, messages]);

  return (
    <section className="flex h-full flex-col rounded-3xl border border-border-strong bg-background-card/80 p-8 backdrop-blur">
      <header className="mb-4 flex items-center justify-between text-xs uppercase tracking-[0.35em] text-text-secondary">
        <span>Диалог</span>
        <span>Идентификатор: {conversationId.slice(0, 8)}</span>
      </header>
      <div className="flex-1 space-y-6 overflow-y-auto pr-2" ref={containerRef}>
        {renderedMessages}
        {isLoading && (
          <div className="flex items-center gap-3 text-sm text-text-secondary">
            <span className="h-2 w-2 animate-pulse rounded-full bg-primary" />
            Колибри формирует ответ...
          </div>
        )}
        {!messages.length && !isLoading && (
          <div className="rounded-2xl border border-border-strong bg-background-input/80 p-6 text-sm text-text-secondary">
            Отправь сообщение, чтобы начать диалог с Колибри.
          </div>
        )}
      </div>
    </section>
  );
};

export default ChatView;
