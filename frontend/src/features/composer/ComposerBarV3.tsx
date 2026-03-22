import { ChangeEvent, ClipboardEvent, FormEvent, KeyboardEvent as ReactKeyboardEvent, useEffect, useRef, useState } from "react";
import TextareaAutosize from "react-textarea-autosize";
import {
  BrainCircuit,
  Camera,
  FileText,
  ImagePlus,
  Mic,
  PackageOpen,
  Paperclip,
  SendHorizontal,
  Sparkles,
  Wand2,
} from "lucide-react";
import { Button } from "@/components/ui/button";
import { Sheet, SheetContent } from "@/components/ui/sheet";
import { cn } from "@/lib/utils";
import { useStreaming } from "@/hooks/useStreaming";
import { useChatStore } from "@/store/useChatStore";
import { useComposerStore } from "@/store/useComposerStore";
import { useShellStore } from "@/store/useShellStore";

export function ComposerBarV3() {
  const currentSessionId = useChatStore((s) => s.currentSessionId);
  const model = useChatStore((s) => s.model);
  const persona = useChatStore((s) => s.persona);
  const setVoiceMode = useChatStore((s) => s.setVoiceMode);
  const setImagineOpen = useChatStore((s) => s.setImagineOpen);
  const setPrimarySurface = useShellStore((s) => s.setPrimarySurface);
  const draft = useComposerStore((s) => s.drafts[currentSessionId] ?? "");
  const setDraft = useComposerStore((s) => s.setDraft);
  const clearDraft = useComposerStore((s) => s.clearDraft);
  const activeAction = useComposerStore((s) => s.activeAction);
  const openAction = useComposerStore((s) => s.openAction);
  const closeAction = useComposerStore((s) => s.closeAction);
  const setFocused = useComposerStore((s) => s.setFocused);
  const { analyzeAttachment, exportKnowledgePack, importKnowledgePack, learnFromTextDemo, send } = useStreaming();
  const [learnTitle, setLearnTitle] = useState("");
  const [learnCategory, setLearnCategory] = useState("manual");
  const [learnQuestion, setLearnQuestion] = useState("что это за знание");
  const [learnText, setLearnText] = useState("");
  const [learnLoading, setLearnLoading] = useState(false);
  const [kpackTitle, setKpackTitle] = useState("Kolibri Knowledge Pack");
  const [kpackDomain, setKpackDomain] = useState("");
  const [kpackLoading, setKpackLoading] = useState(false);
  const fileInputRef = useRef<HTMLInputElement | null>(null);
  const cameraInputRef = useRef<HTMLInputElement | null>(null);
  const textInputRef = useRef<HTMLInputElement | null>(null);
  const kpackInputRef = useRef<HTMLInputElement | null>(null);
  const composerRef = useRef<HTMLTextAreaElement | null>(null);

  useEffect(() => {
    const handleKeydown = (event: globalThis.KeyboardEvent) => {
      const target = event.target as HTMLElement | null;
      const isEditable =
        target instanceof HTMLInputElement ||
        target instanceof HTMLTextAreaElement ||
        target?.isContentEditable;

      if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "k") {
        event.preventDefault();
        composerRef.current?.focus();
        return;
      }

      if (!isEditable && !event.metaKey && !event.ctrlKey && !event.altKey && event.key === "/") {
        event.preventDefault();
        composerRef.current?.focus();
      }
    };

    window.addEventListener("keydown", handleKeydown);
    return () => window.removeEventListener("keydown", handleKeydown);
  }, []);

  const submit = async () => {
    const prompt = draft.trim();
    if (!prompt) return;
    clearDraft(currentSessionId);
    setPrimarySurface("thread");
    await send(prompt);
  };

  const onSubmit = async (event: FormEvent) => {
    event.preventDefault();
    await submit();
  };

  const onKeyDown = (event: ReactKeyboardEvent<HTMLTextAreaElement>) => {
    if (event.key === "Enter" && !event.shiftKey) {
      event.preventDefault();
      void submit();
    }
  };

  const onFileChange = async (event: ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    if (!file) return;
    try {
      closeAction();
      await analyzeAttachment(file);
    } finally {
      event.target.value = "";
    }
  };

  const onPaste = async (event: ClipboardEvent<HTMLTextAreaElement>) => {
    const imageItem = Array.from(event.clipboardData.items).find((item) => item.type.startsWith("image/"));
    if (!imageItem) return;
    const file = imageItem.getAsFile();
    if (!file) return;
    event.preventDefault();
    await analyzeAttachment(file);
  };

  const submitLearnDemo = async () => {
    if (learnText.trim().length < 10 || !learnQuestion.trim()) return;
    setLearnLoading(true);
    try {
      closeAction();
      await learnFromTextDemo({
        title: learnTitle.trim(),
        category: learnCategory.trim() || "manual",
        question: learnQuestion.trim(),
        text: learnText.trim(),
      });
      setLearnTitle("");
      setLearnCategory("manual");
      setLearnQuestion("что это за знание");
      setLearnText("");
    } finally {
      setLearnLoading(false);
    }
  };

  const submitExport = async () => {
    if (!kpackTitle.trim()) return;
    setKpackLoading(true);
    try {
      closeAction();
      await exportKnowledgePack({ title: kpackTitle.trim(), domain: kpackDomain.trim() || undefined });
    } finally {
      setKpackLoading(false);
    }
  };

  const onKpackImportChange = async (event: ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    if (!file) return;
    setKpackLoading(true);
    try {
      closeAction();
      await importKnowledgePack(file);
    } finally {
      event.target.value = "";
      setKpackLoading(false);
    }
  };

  const openVoiceFlow = () => {
    closeAction();
    setVoiceMode(true);
    setPrimarySurface("thread");
  };

  const openImagineFlow = () => {
    closeAction();
    setImagineOpen(true);
    setPrimarySurface("thread");
  };

  return (
    <>
      <form onSubmit={onSubmit} className="shrink-0 border-t border-foreground/6 bg-background/96 px-4 pb-[calc(env(safe-area-inset-bottom)+0.75rem)] pt-3 backdrop-blur-sm lg:px-6 lg:pb-5">
        <div className="mx-auto max-w-3xl">
          <div className="v3-composer-shell">
            <div className="flex items-center gap-2 border-b border-foreground/6 px-3 pb-2.5 pt-1">
              <Button type="button" variant="ghost" size="icon" aria-label="Вложения" className="h-9 w-9 rounded-full" onClick={() => openAction("attach")}>
                <Paperclip className="h-4.5 w-4.5" />
              </Button>
              <div className="min-w-0 flex-1">
                <div className="flex items-center gap-2 overflow-x-auto whitespace-nowrap text-[11px] text-muted">
                  <span className="inline-flex items-center gap-1 rounded-full border border-foreground/8 bg-background px-2.5 py-1">
                    <Sparkles className="v3-accent-text h-3.5 w-3.5" />
                    {model}
                  </span>
                  <span className="inline-flex rounded-full border border-foreground/8 bg-background px-2.5 py-1">
                    {persona}
                  </span>
                </div>
              </div>
              <Button type="button" variant="ghost" size="icon" aria-label="Голосовой режим" className="h-9 w-9 rounded-full" onClick={() => openAction("voice")}>
                <Mic className="h-4.5 w-4.5" />
              </Button>
            </div>

            <div className="flex items-end gap-3 px-2 pb-2 pt-2">
              <TextareaAutosize
                ref={composerRef}
                minRows={1}
                maxRows={8}
                value={draft}
                onChange={(event) => setDraft(currentSessionId, event.target.value)}
                onFocus={() => setFocused(true)}
                onBlur={() => setFocused(false)}
                onKeyDown={onKeyDown}
                onPaste={(event) => void onPaste(event)}
                placeholder="Спроси Kolibri"
                aria-label="Поле ввода запроса"
                className="max-h-56 min-h-[2.75rem] w-full resize-none bg-transparent px-3 py-2 text-[16px] leading-[1.45] text-foreground outline-none placeholder:text-muted"
              />
              <Button
                type="submit"
                size="icon"
                aria-label="Отправить сообщение"
                className={cn(
                  "h-11 w-11 shrink-0 rounded-full",
                  draft.trim() ? "v3-accent-solid hover:opacity-95" : "bg-foreground/10 text-muted shadow-none",
                )}
              >
                <SendHorizontal className="h-4.5 w-4.5" />
              </Button>
            </div>
          </div>
        </div>

        <input
          ref={fileInputRef}
          type="file"
          accept="image/*,.txt,.md,.json,.js,.ts,.tsx,.py,.csv,.log,text/*"
          className="hidden"
          onChange={(event) => void onFileChange(event)}
        />
        <input
          ref={cameraInputRef}
          type="file"
          accept="image/*"
          capture="environment"
          className="hidden"
          onChange={(event) => void onFileChange(event)}
        />
        <input
          ref={textInputRef}
          type="file"
          accept=".txt,.md,.json,.js,.ts,.tsx,.py,.csv,.log,text/*"
          className="hidden"
          onChange={(event) => void onFileChange(event)}
        />
        <input
          ref={kpackInputRef}
          type="file"
          accept=".kpack,application/zip"
          className="hidden"
          onChange={(event) => void onKpackImportChange(event)}
        />
      </form>

      <Sheet open={activeAction === "attach"} onOpenChange={(open) => (open ? openAction("attach") : closeAction())}>
        <SheetContent
          title="Вложения"
          description="Фото, документы и быстрый анализ"
          className="left-0 right-0 top-auto z-50 h-auto max-w-none rounded-t-[1.75rem] border-r-0 border-t bg-background px-4 pb-[calc(env(safe-area-inset-bottom)+1rem)] pt-4 lg:left-auto lg:right-6 lg:top-24 lg:h-auto lg:w-[24rem] lg:max-w-[24rem] lg:rounded-[1.5rem] lg:border lg:border-foreground/8 lg:pb-4"
        >
          <div className="space-y-2">
            <button type="button" onClick={() => fileInputRef.current?.click()} className="v3-action-row">
              <Paperclip className="v3-accent-text h-5 w-5" />
              <span>
                <span className="block text-sm font-semibold">Фото или файл</span>
                <span className="block text-xs text-muted">Открыть галерею или выбрать документ</span>
              </span>
            </button>
            <button type="button" onClick={() => cameraInputRef.current?.click()} className="v3-action-row">
              <Camera className="h-5 w-5 text-emerald-500" />
              <span>
                <span className="block text-sm font-semibold">Снять фото</span>
                <span className="block text-xs text-muted">Открыть камеру и сразу отправить в анализ</span>
              </span>
            </button>
            <button type="button" onClick={() => textInputRef.current?.click()} className="v3-action-row">
              <FileText className="h-5 w-5 text-violet-500" />
              <span>
                <span className="block text-sm font-semibold">Текст и код</span>
                <span className="block text-xs text-muted">TXT, Markdown, JSON, TS, Python, CSV и логи</span>
              </span>
            </button>
            <button type="button" onClick={() => openAction("teach")} className="v3-action-row">
              <BrainCircuit className="h-5 w-5 text-sky-500" />
              <span>
                <span className="block text-sm font-semibold">Teach</span>
                <span className="block text-xs text-muted">Добавить знание и сразу пересчитать рой</span>
              </span>
            </button>
            <button type="button" onClick={() => openAction("pack")} className="v3-action-row">
              <PackageOpen className="h-5 w-5 text-amber-500" />
              <span>
                <span className="block text-sm font-semibold">Knowledge Pack</span>
                <span className="block text-xs text-muted">Экспортировать или импортировать .kpack</span>
              </span>
            </button>
            <button type="button" onClick={() => openAction("imagine")} className="v3-action-row">
              <Sparkles className="v3-accent-text h-5 w-5" />
              <span>
                <span className="block text-sm font-semibold">Imagine</span>
                <span className="block text-xs text-muted">Открыть image-generation flow как вторичный composer action</span>
              </span>
            </button>
          </div>
        </SheetContent>
      </Sheet>

      <Sheet open={activeAction === "voice"} onOpenChange={(open) => (open ? openAction("voice") : closeAction())}>
        <SheetContent
          title="Voice"
          description="Голосовой сценарий из composer"
          className="left-0 right-0 top-auto z-50 h-auto max-w-none rounded-t-[1.75rem] border-r-0 border-t bg-background px-4 pb-[calc(env(safe-area-inset-bottom)+1rem)] pt-4 lg:left-auto lg:right-6 lg:top-24 lg:w-[24rem] lg:max-w-[24rem] lg:rounded-[1.5rem] lg:border lg:border-foreground/8 lg:pb-4"
        >
          <div className="space-y-2">
            <button type="button" onClick={openVoiceFlow} className="v3-action-row">
              <Mic className="v3-accent-text h-5 w-5" />
              <span>
                <span className="block text-sm font-semibold">Надиктовать сообщение</span>
                <span className="block text-xs text-muted">Откроется полноэкранный voice flow</span>
              </span>
            </button>
            <button
              type="button"
              onClick={() => {
                closeAction();
                setDraft(
                  currentSessionId,
                  draft
                    ? `${draft}\n\nСформулируй ответ как короткое голосовое сообщение.`
                    : "Сформулируй ответ как короткое голосовое сообщение.",
                );
              }}
              className="v3-action-row"
            >
              <Wand2 className="h-5 w-5 text-amber-500" />
              <span>
                <span className="block text-sm font-semibold">Подготовить voice-reply</span>
                <span className="block text-xs text-muted">Добавить в запрос инструкцию на короткий голосовой ответ</span>
              </span>
            </button>
          </div>
        </SheetContent>
      </Sheet>

      <Sheet open={activeAction === "teach"} onOpenChange={(open) => (open ? openAction("teach") : closeAction())}>
        <SheetContent
          title="Teach"
          description="Быстрое обучение роя"
          className="left-0 right-0 top-auto z-50 h-auto max-w-none rounded-t-[1.75rem] border-r-0 border-t bg-background px-4 pb-[calc(env(safe-area-inset-bottom)+1rem)] pt-4 lg:left-auto lg:right-6 lg:top-20 lg:h-[calc(100dvh-8rem)] lg:w-[30rem] lg:max-w-[30rem] lg:overflow-y-auto lg:rounded-[1.5rem] lg:border lg:border-foreground/8 lg:pb-4"
        >
          <div className="space-y-3">
            <div className="space-y-1">
              <label className="text-xs font-semibold uppercase tracking-[0.16em] text-muted">Название</label>
              <input value={learnTitle} onChange={(event) => setLearnTitle(event.target.value)} className="v3-input" placeholder="Название материала" />
            </div>
            <div className="space-y-1">
              <label className="text-xs font-semibold uppercase tracking-[0.16em] text-muted">Категория</label>
              <input value={learnCategory} onChange={(event) => setLearnCategory(event.target.value)} className="v3-input" placeholder="manual" />
            </div>
            <div className="space-y-1">
              <label className="text-xs font-semibold uppercase tracking-[0.16em] text-muted">Контрольный вопрос</label>
              <input value={learnQuestion} onChange={(event) => setLearnQuestion(event.target.value)} className="v3-input" placeholder="Что это за знание?" />
            </div>
            <div className="space-y-1">
              <label className="text-xs font-semibold uppercase tracking-[0.16em] text-muted">Текст</label>
              <textarea
                value={learnText}
                onChange={(event) => setLearnText(event.target.value)}
                className="v3-input min-h-[13rem] resize-y"
                placeholder="Вставь знание, которое нужно сохранить в формульной памяти."
              />
            </div>
            <Button type="button" className="w-full rounded-2xl" disabled={learnLoading || learnText.trim().length < 10} onClick={() => void submitLearnDemo()}>
              {learnLoading ? "Обучаю…" : "Научить рой"}
            </Button>
          </div>
        </SheetContent>
      </Sheet>

      <Sheet open={activeAction === "pack"} onOpenChange={(open) => (open ? openAction("pack") : closeAction())}>
        <SheetContent
          title="Knowledge Pack"
          description="Экспорт и импорт пакетированных знаний"
          className="left-0 right-0 top-auto z-50 h-auto max-w-none rounded-t-[1.75rem] border-r-0 border-t bg-background px-4 pb-[calc(env(safe-area-inset-bottom)+1rem)] pt-4 lg:left-auto lg:right-6 lg:top-24 lg:w-[28rem] lg:max-w-[28rem] lg:rounded-[1.5rem] lg:border lg:border-foreground/8 lg:pb-4"
        >
          <div className="space-y-5">
            <div className="space-y-3">
              <p className="text-sm font-semibold tracking-[-0.02em] text-foreground">Экспорт</p>
              <input value={kpackTitle} onChange={(event) => setKpackTitle(event.target.value)} className="v3-input" placeholder="Название пакета" />
              <input value={kpackDomain} onChange={(event) => setKpackDomain(event.target.value)} className="v3-input" placeholder="Домен, например law" />
              <Button type="button" className="w-full rounded-2xl" disabled={kpackLoading} onClick={() => void submitExport()}>
                {kpackLoading ? "Собираю…" : "Экспортировать .kpack"}
              </Button>
            </div>
            <div className="space-y-3 border-t border-foreground/6 pt-4">
              <p className="text-sm font-semibold tracking-[-0.02em] text-foreground">Импорт</p>
              <button type="button" onClick={() => kpackInputRef.current?.click()} className="v3-action-row">
                <ImagePlus className="v3-accent-text h-5 w-5" />
                <span>
                  <span className="block text-sm font-semibold">Загрузить .kpack</span>
                  <span className="block text-xs text-muted">Импортировать пакет в live-memory и обновить рой</span>
                </span>
              </button>
            </div>
          </div>
        </SheetContent>
      </Sheet>

      <Sheet open={activeAction === "imagine"} onOpenChange={(open) => (open ? openAction("imagine") : closeAction())}>
        <SheetContent
          title="Imagine"
          description="Переход к image flow"
          className="left-0 right-0 top-auto z-50 h-auto max-w-none rounded-t-[1.75rem] border-r-0 border-t bg-background px-4 pb-[calc(env(safe-area-inset-bottom)+1rem)] pt-4 lg:left-auto lg:right-6 lg:top-24 lg:w-[24rem] lg:max-w-[24rem] lg:rounded-[1.5rem] lg:border lg:border-foreground/8 lg:pb-4"
        >
          <button type="button" onClick={openImagineFlow} className="v3-action-row">
            <Sparkles className="v3-accent-text h-5 w-5" />
            <span>
              <span className="block text-sm font-semibold">Открыть Imagine</span>
              <span className="block text-xs text-muted">Перейти в image-generation flow, не покидая главный чатовый shell</span>
            </span>
          </button>
        </SheetContent>
      </Sheet>
    </>
  );
}
