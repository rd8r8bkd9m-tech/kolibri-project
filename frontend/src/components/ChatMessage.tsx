
import { Check, Copy, Link2 } from "lucide-react";
import { useCallback, useMemo, useState } from "react";
import type { ChatMessage as ChatMessageModel } from "../types/chat";

const formatScore = (value: number): string => {
  if (!Number.isFinite(value)) {
    return "—";
  }
  return value.toFixed(2);
};

interface ChatMessageProps {
  message: ChatMessageModel;
  conversationId: string;
  latestUserMessage?: ChatMessageModel;
}

const ChatMessage = ({ message, conversationId, latestUserMessage }: ChatMessageProps) => {
  const isUser = message.role === "user";
  const [isContextExpanded, setIsContextExpanded] = useState(false);
  const [isCopied, setIsCopied] = useState(false);
  const hasContext = !isUser && Boolean(message.context?.length);
  const contextCount = message.context?.length ?? 0;

  const isoDate = useMemo(() => {
    if (!message.isoTimestamp) {
      return null;
    }
    try {
      return new Date(message.isoTimestamp).toLocaleTimeString("ru-RU", {
        hour: "2-digit",
        minute: "2-digit",
        second: "2-digit",
      });
    } catch {
      return null;
    }
  }, [message.isoTimestamp]);

  const handleCopy = useCallback(async () => {
    if (!navigator?.clipboard) {
      return;
    }
    try {
      await navigator.clipboard.writeText(message.content);
      setIsCopied(true);
      setTimeout(() => setIsCopied(false), 1500);
    } catch {
      setIsCopied(false);
    }
  }, [message.content]);

  return (
    <div className="flex items-start gap-4">
      <div
        className={`flex h-10 w-10 items-center justify-center rounded-full text-sm font-semibold ${
          isUser ? "bg-primary/20 text-primary" : "bg-accent/20 text-accent"
        }`}
      >
        {isUser ? "Я" : "К"}
      </div>
      <div className="relative max-w-3xl rounded-2xl border border-border-strong bg-background-input/90 p-5">
        <div className="absolute right-4 top-4 flex items-center gap-2">
          <button
            type="button"
            onClick={handleCopy}
            className="flex items-center gap-2 rounded-xl border border-border-strong bg-background-card/80 px-3 py-1 text-[0.7rem] font-semibold text-text-secondary transition-colors hover:text-text-primary"
          >
            {isCopied ? <Check className="h-3.5 w-3.5" /> : <Copy className="h-3.5 w-3.5" />}
            {isCopied ? "Скопировано" : "Копировать"}
          </button>
        </div>
        <p className="whitespace-pre-line text-sm leading-relaxed text-text-primary">{message.content}</p>
        <div className="mt-3 flex flex-wrap items-center gap-3 text-xs text-text-secondary">
          <span>{isoDate ?? message.timestamp}</span>
          {!isUser && message.modeLabel && (
            <span className="rounded-lg border border-border-strong bg-background-card/70 px-2 py-1 uppercase tracking-wide text-[0.65rem] text-primary">
              {message.modeLabel}
            </span>
          )}
          {latestUserMessage && !isUser && (
            <span className="rounded-lg border border-border-strong bg-background-card/70 px-2 py-1 text-[0.65rem] text-text-secondary">
              ↪ На вопрос: {latestUserMessage.content.slice(0, 60)}{latestUserMessage.content.length > 60 ? "…" : ""}
            </span>
          )}
        </div>

        {!isUser && (hasContext || message.contextError) && (
          <div className="mt-3 space-y-3 border-t border-dashed border-border-strong pt-3 text-xs text-text-secondary">
            {hasContext && (
              <div>
                <button
                  type="button"
                  onClick={() => setIsContextExpanded((prev) => !prev)}
                  className="rounded-lg border border-primary/40 bg-background-input/80 px-3 py-1 font-semibold text-primary transition-colors hover:border-primary"
                >
                  {isContextExpanded ? "Скрыть контекст" : "Показать контекст"} ({contextCount})
                </button>
                {isContextExpanded && (
                  <div className="mt-2 space-y-2">
                    {message.context?.map((snippet, index) => (
                      <article
                        key={snippet.id}
                        className="rounded-xl border border-border-strong bg-background-card/70 p-3"
                        aria-label={`Источник ${index + 1}`}
                      >
                        <div className="flex items-center justify-between text-[0.7rem] font-semibold text-text-secondary">
                          <span className="uppercase tracking-wide text-text-primary">Источник {index + 1}</span>
                          <span className="text-text-secondary">Релевантность: {formatScore(snippet.score)}</span>
                        </div>
                        <p className="mt-1 text-sm font-semibold text-text-primary">{snippet.title}</p>
                        <p className="mt-2 whitespace-pre-line text-[0.85rem] leading-relaxed text-text-secondary">
                          {snippet.content}
                        </p>
                        {snippet.source && (
                          <p className="mt-2 flex items-center gap-1 text-[0.7rem] uppercase tracking-wide text-primary/80">
                            <Link2 className="h-3.5 w-3.5" /> {snippet.source}
                          </p>
                        )}
                      </article>
                    ))}
                  </div>
                )}
              </div>
            )}
            {message.contextError && (
              <p className="rounded-lg bg-accent/10 px-3 py-2 text-[0.75rem] text-accent">
                Контекст недоступен: {message.contextError}
              </p>
            )}
          </div>

        )}
      </div>
    </div>
  );
};

export default ChatMessage;
