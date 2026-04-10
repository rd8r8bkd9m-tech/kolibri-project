import { memo, useEffect, useRef, useState } from "react";
import TextareaAutosize from "react-textarea-autosize";
import { cva } from "class-variance-authority";
import { Check, Copy, MoreHorizontal, PencilLine, RotateCcw, SendHorizontal, RefreshCcw, ThumbsUp, ThumbsDown } from "lucide-react";
import { Avatar, AvatarFallback } from "@/components/ui/avatar";
import { Button } from "@/components/ui/button";
import { DropdownMenu, DropdownMenuContent, DropdownMenuItem, DropdownMenuTrigger } from "@/components/ui/dropdown-menu";
import { sanitizeAssistantText } from "@/lib/answerSanitizer";
import { cn, formatTime } from "@/lib/utils";
import { renderMarkdown } from "@/lib/markdown";
import { useStreaming } from "@/hooks/useStreaming";
import { Sheet, SheetContent } from "@/components/ui/sheet";
import type { ChatMessage } from "@/types";

const bubbleVariants = cva("rounded-[1.35rem] px-4 py-3 text-[15px] leading-[1.55]", {
  variants: {
    role: {
      user:
        "ml-auto max-w-[84%] border border-foreground/8 bg-foreground text-background shadow-[0_8px_22px_rgba(15,23,42,0.12)] lg:max-w-[78%]",
      assistant:
        "mr-auto flex max-w-[98%] gap-3 border border-foreground/6 bg-card shadow-[0_8px_22px_rgba(15,23,42,0.05)] dark:border-white/8 md:max-w-[88%] lg:max-w-[84%]",
    }
  },
});

function CodeBlock({ code, language }: { code: string; language?: string }) {
  const [copied, setCopied] = useState(false);

  const handleCopy = async () => {
    try {
      await navigator.clipboard.writeText(code);
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    } catch {
      // noop
    }
  };

  return (
    <div className="group/code relative overflow-hidden rounded-xl border border-border/10 bg-card/95">
      <div className="flex items-center justify-between border-b border-border/10 bg-muted/30 px-3 py-2">
        <span className="text-[11px] font-medium text-muted uppercase tracking-wide">
          {language || "code"}
        </span>
        <button
          type="button"
          onClick={handleCopy}
          className={cn(
            "inline-flex items-center gap-1.5 rounded-lg px-2 py-1 text-[11px] font-medium transition-all",
            copied
              ? "bg-emerald-500/15 text-emerald-600 dark:text-emerald-400"
              : "bg-foreground/5 text-muted hover:text-foreground hover:bg-foreground/10"
          )}
        >
          {copied ? (
            <>
              <Check className="h-3.5 w-3.5" />
              Скопировано
            </>
          ) : (
            <>
              <Copy className="h-3.5 w-3.5" />
              Копировать
            </>
          )}
        </button>
      </div>
      <pre className="overflow-x-auto p-3">
        <code className="font-mono text-sm text-foreground">{code}</code>
      </pre>
    </div>
  );
}

function renderCodeBlocks(content: string) {
  const blocks = content.split(/```/g);
  if (blocks.length < 2) {
    return <div className="markdown" dangerouslySetInnerHTML={{ __html: renderMarkdown(content) }} />;
  }

  return (
    <div className="space-y-3">
      {blocks.map((part, index) => {
        const isCode = index % 2 === 1;
        if (!isCode) {
          return <div key={index} className="markdown" dangerouslySetInnerHTML={{ __html: renderMarkdown(part) }} />;
        }
        const [lang, ...rest] = part.split("\n");
        const codeContent = rest.join("\n").trim();
        return <CodeBlock key={index} code={codeContent} language={lang?.trim() || undefined} />;
      })}
    </div>
  );
}

function formatEstimateStage(stage?: string): string {
  switch (stage) {
    case "collecting_inputs":
      return "Сбор вводных";
    case "draft_ready":
      return "Черновая смета";
    case "project_plan":
      return "План проекта";
    case "materials_scope":
      return "Материалы";
    default:
      return stage || "";
  }
}

function MessageReactions({ messageId }: { messageId: string }) {
  const [feedback, setFeedback] = useState<"up" | "down" | null>(null);

  const handleFeedback = (type: "up" | "down") => {
    if (feedback === type) {
      setFeedback(null); // toggle off
      return;
    }
    setFeedback(type);
    // TODO: Send feedback to backend for quality tracking
    console.log(`[${messageId}] Feedback: ${type}`);
  };

  return (
    <div className="mt-2 flex items-center gap-1.5">
      <button
        type="button"
        onClick={() => handleFeedback("up")}
        className={cn(
          "inline-flex items-center gap-1 rounded-full border px-2.5 py-1 text-[11px] font-medium transition-all",
          feedback === "up"
            ? "border-emerald-500/30 bg-emerald-500/10 text-emerald-600 dark:text-emerald-400"
            : "border-foreground/8 bg-transparent text-muted hover:text-foreground hover:bg-foreground/5"
        )}
      >
        <ThumbsUp className="h-3 w-3" />
        {feedback === "up" ? "1" : "0"}
      </button>
      <button
        type="button"
        onClick={() => handleFeedback("down")}
        className={cn(
          "inline-flex items-center gap-1 rounded-full border px-2.5 py-1 text-[11px] font-medium transition-all",
          feedback === "down"
            ? "border-red-500/30 bg-red-500/10 text-red-600 dark:text-red-400"
            : "border-foreground/8 bg-transparent text-muted hover:text-foreground hover:bg-foreground/5"
        )}
      >
        <ThumbsDown className="h-3 w-3" />
        {feedback === "down" ? "1" : "0"}
      </button>
    </div>
  );
}

export const MessageBubble = memo(function MessageBubble({
  message,
  previousUserPrompt,
}: {
  message: ChatMessage;
  previousUserPrompt?: string;
}) {
  const { resendEditedMessage, send } = useStreaming();
  const [editing, setEditing] = useState(false);
  const [saving, setSaving] = useState(false);
  const [sheetOpen, setSheetOpen] = useState(false);
  const [copied, setCopied] = useState(false);
  const [draft, setDraft] = useState(message.content);
  const holdTimerRef = useRef<number | null>(null);
  const canEdit = message.role === "user" && !message.streaming && !message.imageUrl;
  const canRetry = message.role === "assistant" && !message.streaming && Boolean(previousUserPrompt?.trim());
  const hasImage = Boolean(message.imageUrl);
  const productMeta = message.productMeta;
  const showEstimatorMeta =
    message.role === "assistant" && (productMeta?.productMode === "estimator" || productMeta?.projectActive);
  const normalizedContent = message.role === "assistant" ? sanitizeAssistantText(message.content) : message.content;
  const hasContent = normalizedContent.trim().length > 0;
  const displayContent = hasContent ? normalizedContent : (message.streaming ? "Думаю..." : "");

  useEffect(() => {
    if (!editing) setDraft(message.content);
  }, [editing, message.content]);

  useEffect(() => {
    if (!copied) return undefined;
    const timer = window.setTimeout(() => setCopied(false), 1600);
    return () => window.clearTimeout(timer);
  }, [copied]);

  const submitEdit = async () => {
    const next = draft.trim();
    if (!next || next === message.content.trim()) {
      setEditing(false);
      setDraft(message.content);
      return;
    }
    setSaving(true);
    try {
      await resendEditedMessage(message.id, next);
      setEditing(false);
    } finally {
      setSaving(false);
    }
  };

  const copyMessage = async () => {
    try {
      await navigator.clipboard.writeText(normalizedContent);
      setCopied(true);
    } catch {
      // noop
    }
    setSheetOpen(false);
  };

  const retryMessage = async () => {
    if (!previousUserPrompt?.trim()) return;
    await send(previousUserPrompt.trim());
    setSheetOpen(false);
  };

  const clearHold = () => {
    if (holdTimerRef.current !== null) {
      window.clearTimeout(holdTimerRef.current);
      holdTimerRef.current = null;
    }
  };

  return (
    <article
      className={cn("group mb-3.5 w-full", message.role === "user" ? "justify-end" : "justify-start")}
      onContextMenu={(event) => {
        event.preventDefault();
        if (!editing) setSheetOpen(true);
      }}
      onPointerDown={() => {
        if (editing) return;
        clearHold();
        holdTimerRef.current = window.setTimeout(() => {
          setSheetOpen(true);
        }, 420);
      }}
      onPointerUp={clearHold}
      onPointerCancel={clearHold}
      onPointerLeave={clearHold}
    >
      <div className={bubbleVariants({ role: message.role })}>
        {message.role === "assistant" ? (
          <Avatar className="hidden h-8 w-8 shrink-0 border border-foreground/8 bg-background text-foreground shadow-[0_6px_14px_rgba(15,23,42,0.04)] dark:border-white/8 md:flex">
            <AvatarFallback className="bg-transparent text-[12px] font-semibold">К</AvatarFallback>
          </Avatar>
        ) : null}
        <div className="min-w-0 flex-1">
          {editing ? (
            <div className="space-y-2">
              <TextareaAutosize
                value={draft}
                onChange={(event) => setDraft(event.target.value)}
                minRows={2}
                maxRows={8}
                className="w-full resize-none rounded-2xl border border-foreground/10 bg-background px-3 py-2 text-[14px] leading-[1.5] text-foreground outline-none"
              />
              <div className="flex items-center justify-end gap-2 text-[11px]">
                <button
                  type="button"
                  onClick={() => {
                    setEditing(false);
                    setDraft(message.content);
                  }}
                  className="inline-flex items-center gap-1 rounded-full border border-foreground/10 px-3 py-1.5 text-muted"
                >
                  <RotateCcw className="h-3.5 w-3.5" />
                  Отмена
                </button>
                <button
                  type="button"
                  disabled={saving}
                  onClick={() => void submitEdit()}
                  className="inline-flex items-center gap-1 rounded-full bg-foreground px-3 py-1.5 font-semibold text-background disabled:opacity-60"
                >
                  {saving ? <SendHorizontal className="h-3.5 w-3.5 animate-pulse" /> : <Check className="h-3.5 w-3.5" />}
                  Сохранить и отправить
                </button>
              </div>
            </div>
          ) : displayContent ? renderCodeBlocks(displayContent) : null}
          {message.streaming ? <span className="ml-1 inline-block h-4 w-0.5 animate-pulse bg-foreground/70 align-middle" /> : null}
          {hasImage ? (
            <a href={message.imageUrl} target="_blank" rel="noreferrer" className="group mt-3 block overflow-hidden rounded-xl border border-border/15 bg-card/60">
              <img src={message.imageUrl} alt="Сгенерированное изображение" className="max-h-[38rem] w-full object-cover transition duration-300 group-hover:scale-[1.01]" loading="lazy" />
            </a>
          ) : null}
          {showEstimatorMeta ? (
            <div className="mt-3 flex flex-wrap gap-2 text-[11px] text-muted">
              <span className="rounded-full border border-amber-300/35 bg-amber-500/10 px-2.5 py-1 font-medium text-amber-700 dark:text-amber-200">
                Сметчик
              </span>
              {productMeta?.projectKind ? (
                <span className="rounded-full border border-foreground/10 bg-background/70 px-2.5 py-1">
                  {productMeta.projectKind}
                </span>
              ) : null}
              {productMeta?.estimateStage ? (
                <span className="rounded-full border border-foreground/10 bg-background/70 px-2.5 py-1">
                  {formatEstimateStage(productMeta.estimateStage)}
                </span>
              ) : null}
              {typeof productMeta?.projectAreaM2 === "number" ? (
                <span className="rounded-full border border-foreground/10 bg-background/70 px-2.5 py-1">
                  {productMeta.projectAreaM2.toFixed(1)} м²
                </span>
              ) : null}
            </div>
          ) : null}
          {!message.streaming && message.role === "assistant" && hasContent ? (
            <MessageReactions messageId={message.id} />
          ) : null}
          <div className={cn("mt-2 flex items-center gap-2 text-[11px]", message.role === "user" ? "text-background/70" : "text-muted")}>
            <span>{formatTime(message.createdAt)}</span>
            {message.editedAt ? <span className="opacity-75">изменено</span> : null}
            {copied ? <span className="opacity-75">скопировано</span> : null}
            {!editing ? (
              <DropdownMenu>
                <DropdownMenuTrigger asChild>
                  <Button
                    type="button"
                    variant="ghost"
                    size="icon"
                    aria-label="Действия с сообщением"
                    className={cn(
                      "ml-auto hidden h-7 w-7 rounded-full md:inline-flex",
                      message.role === "user"
                        ? "text-background/70 hover:bg-background/10 hover:text-background"
                        : "text-muted hover:text-foreground",
                    )}
                  >
                    <MoreHorizontal className="h-4 w-4" />
                  </Button>
                </DropdownMenuTrigger>
                <DropdownMenuContent align="end">
                  {canRetry ? (
                    <DropdownMenuItem onClick={() => void retryMessage()}>
                      <RefreshCcw className="mr-2 h-4 w-4" />
                      Повторить запрос
                    </DropdownMenuItem>
                  ) : null}
                  {canEdit ? (
                    <DropdownMenuItem
                      onClick={() => {
                        setEditing(true);
                      }}
                    >
                      <PencilLine className="mr-2 h-4 w-4" />
                      Изменить и переспросить
                    </DropdownMenuItem>
                  ) : null}
                  <DropdownMenuItem onClick={() => void copyMessage()}>
                    <Copy className="mr-2 h-4 w-4" />
                    Копировать текст
                  </DropdownMenuItem>
                </DropdownMenuContent>
              </DropdownMenu>
            ) : null}
          </div>
        </div>
      </div>

      <Sheet open={sheetOpen} onOpenChange={setSheetOpen}>
        <SheetContent
          title="Действия"
          description="Быстрые действия с сообщением"
          className="inset-x-0 left-0 right-0 top-auto z-50 h-auto max-w-none rounded-t-[2rem] border-r-0 border-t border-border/10 bg-background/95 px-4 pb-[calc(env(safe-area-inset-bottom)+1rem)] pt-4"
        >
          <div className="mx-auto w-full max-w-xl">
            <div className="mx-auto mb-3 h-1.5 w-14 rounded-full bg-border/30" />
            <p className="text-base font-semibold">Действия с сообщением</p>
            <div className="mt-4 grid gap-2">
              {canRetry ? (
                <button
                  type="button"
                  onClick={() => void retryMessage()}
                  className="flex items-center gap-3 rounded-[1.2rem] border border-border/10 bg-card/70 px-4 py-4 text-left"
                >
                  <RefreshCcw className="h-5 w-5 text-sky-500" />
                  <span>
                    <span className="block text-sm font-semibold">Повторить запрос</span>
                    <span className="block text-xs text-muted">Снова отправить последний пользовательский запрос</span>
                  </span>
                </button>
              ) : null}
              {canEdit ? (
                <button
                  type="button"
                  onClick={() => {
                    setSheetOpen(false);
                    setEditing(true);
                  }}
                  className="flex items-center gap-3 rounded-[1.2rem] border border-border/10 bg-card/70 px-4 py-4 text-left"
                >
                  <PencilLine className="h-5 w-5 text-cyan-400" />
                  <span>
                    <span className="block text-sm font-semibold">Изменить и переспросить</span>
                    <span className="block text-xs text-muted">Перепишет запрос и отправит новую ветку ответа</span>
                  </span>
                </button>
              ) : null}
              <button
                type="button"
                onClick={() => void copyMessage()}
                className="flex items-center gap-3 rounded-[1.2rem] border border-border/10 bg-card/70 px-4 py-4 text-left"
              >
                <Copy className="h-5 w-5 text-emerald-400" />
                <span>
                  <span className="block text-sm font-semibold">Копировать текст</span>
                  <span className="block text-xs text-muted">Скопировать сообщение в буфер обмена</span>
                </span>
              </button>
            </div>
          </div>
        </SheetContent>
      </Sheet>
    </article>
  );
});
