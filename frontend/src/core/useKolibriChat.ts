import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import type { ChatMessage } from "../types/chat";
import type { KnowledgeSnippet } from "../types/knowledge";
import { fetchKnowledgeStatus, searchKnowledge } from "./knowledge";
import kolibriBridge from "./kolibri-bridge";
import { findModeLabel, MODE_OPTIONS } from "./modes";

export interface ConversationMetrics {
  userMessages: number;
  assistantMessages: number;
  knowledgeReferences: number;
  lastUpdatedIso?: string;
}

const DEFAULT_TITLE = "Новая беседа";

const formatPromptWithContext = (question: string, context: KnowledgeSnippet[]): string => {
  if (!context.length) {
    return question;
  }

  const contextBlocks = context.map((snippet, index) => {
    const title = snippet.title ? ` (${snippet.title})` : "";
    return [`Источник ${index + 1}${title}:`, snippet.content].join("\n");
  });

  return [`Контекст:`, ...contextBlocks, "", `Вопрос пользователя: ${question}`].join("\n");
};

const deriveTitleFromMessages = (messages: ChatMessage[]): string => {
  const firstUserMessage = messages.find((message) => message.role === "user");
  if (!firstUserMessage) {
    return DEFAULT_TITLE;
  }

  const words = firstUserMessage.content.trim().split(/\s+/).slice(0, 8);
  if (!words.length) {
    return DEFAULT_TITLE;
  }

  const title = words.join(" ");
  return title.length > 60 ? `${title.slice(0, 57)}…` : title;
};

const nowPair = () => {
  const now = new Date();
  return {
    display: now.toLocaleTimeString("ru-RU", { hour: "2-digit", minute: "2-digit" }),
    iso: now.toISOString(),
  };
};

interface UseKolibriChatResult {
  messages: ChatMessage[];
  draft: string;
  mode: string;
  isProcessing: boolean;
  bridgeReady: boolean;
  conversationId: string;
  conversationTitle: string;
  knowledgeStatus: Awaited<ReturnType<typeof fetchKnowledgeStatus>> | null;
  knowledgeError?: string;
  statusLoading: boolean;
  latestAssistantMessage?: ChatMessage;
  metrics: ConversationMetrics;
  setDraft: (value: string) => void;
  setMode: (mode: string) => void;
  renameConversation: (title: string) => void;
  sendMessage: () => Promise<void>;
  resetConversation: () => Promise<void>;
  refreshKnowledgeStatus: () => Promise<void>;
}

export const useKolibriChat = (): UseKolibriChatResult => {
  const [messages, setMessages] = useState<ChatMessage[]>([]);
  const [draft, setDraft] = useState("");
  const [mode, setMode] = useState(MODE_OPTIONS[0]?.value ?? "neutral");
  const [isProcessing, setIsProcessing] = useState(false);
  const [bridgeReady, setBridgeReady] = useState(false);
  const [conversationId, setConversationId] = useState(() => crypto.randomUUID());
  const [conversationTitle, setConversationTitle] = useState<string>(DEFAULT_TITLE);
  const [knowledgeStatus, setKnowledgeStatus] = useState<Awaited<ReturnType<typeof fetchKnowledgeStatus>> | null>(null);
  const [knowledgeError, setKnowledgeError] = useState<string | undefined>();
  const [statusLoading, setStatusLoading] = useState(false);

  const knowledgeSearchAbortRef = useRef<AbortController | null>(null);

  useEffect(() => {
    let cancelled = false;
    kolibriBridge.ready
      .then(() => {
        if (!cancelled) {
          setBridgeReady(true);
        }
      })
      .catch((error) => {
        if (cancelled) {
          return;
        }
        const moment = nowPair();
        const assistantMessage: ChatMessage = {
          id: crypto.randomUUID(),
          role: "assistant",
          content:
            error instanceof Error
              ? `Не удалось инициализировать KolibriScript: ${error.message}`
              : "Не удалось инициализировать KolibriScript.",
          timestamp: moment.display,
          isoTimestamp: moment.iso,
        };
        setMessages((prev) => [...prev, assistantMessage]);
      });

    return () => {
      cancelled = true;
    };
  }, []);

  const refreshKnowledgeStatus = useCallback(async () => {
    setStatusLoading(true);
    try {
      const status = await fetchKnowledgeStatus();
      setKnowledgeStatus(status);
      setKnowledgeError(undefined);
    } catch (error) {
      setKnowledgeError(
        error instanceof Error && error.message ? error.message : "Не удалось получить состояние знаний.",
      );
    } finally {
      setStatusLoading(false);
    }
  }, []);

  useEffect(() => {
    void refreshKnowledgeStatus();
  }, [refreshKnowledgeStatus]);

  useEffect(() => {
    if (conversationTitle !== DEFAULT_TITLE) {
      return;
    }
    setConversationTitle(deriveTitleFromMessages(messages));
  }, [conversationTitle, messages]);

  const resetConversation = useCallback(async () => {
    knowledgeSearchAbortRef.current?.abort();
    knowledgeSearchAbortRef.current = null;

    if (!bridgeReady) {
      setMessages([]);
      setDraft("");
      setMode(MODE_OPTIONS[0]?.value ?? "neutral");
      setConversationId(crypto.randomUUID());
      setConversationTitle(DEFAULT_TITLE);
      setIsProcessing(false);
      return;
    }

    try {
      await kolibriBridge.reset();
      setMessages([]);
      setDraft("");
      setMode(MODE_OPTIONS[0]?.value ?? "neutral");
      setConversationId(crypto.randomUUID());
      setConversationTitle(DEFAULT_TITLE);
    } catch (error) {
      const moment = nowPair();
      const assistantMessage: ChatMessage = {
        id: crypto.randomUUID(),
        role: "assistant",
        content:
          error instanceof Error
            ? `Не удалось сбросить KolibriScript: ${error.message}`
            : "Не удалось сбросить KolibriScript.",
        timestamp: moment.display,
        isoTimestamp: moment.iso,
      };
      setMessages((prev) => [...prev, assistantMessage]);
    } finally {
      setIsProcessing(false);
    }
  }, [bridgeReady]);

  const sendMessage = useCallback(async () => {
    const content = draft.trim();
    if (!content || isProcessing || !bridgeReady) {
      return;
    }

    const timestamp = nowPair();
    const userMessage: ChatMessage = {
      id: crypto.randomUUID(),
      role: "user",
      content,
      timestamp: timestamp.display,
      isoTimestamp: timestamp.iso,
    };

    setMessages((prev) => [...prev, userMessage]);
    setDraft("");
    setIsProcessing(true);

    knowledgeSearchAbortRef.current?.abort();
    const controller = new AbortController();
    knowledgeSearchAbortRef.current = controller;

    let knowledgeContext: KnowledgeSnippet[] = [];
    let contextError: string | undefined;
    let aborted = false;

    try {
      knowledgeContext = await searchKnowledge(content, { topK: 3, signal: controller.signal });
    } catch (error) {
      if (error instanceof DOMException && error.name === "AbortError") {
        aborted = true;
      } else {
        contextError =
          error instanceof Error && error.message ? error.message : "Не удалось получить контекст из памяти.";
      }
    } finally {
      knowledgeSearchAbortRef.current = null;
    }

    if (aborted) {
      setIsProcessing(false);
      void refreshKnowledgeStatus();
      return;
    }

    const prompt = knowledgeContext.length ? formatPromptWithContext(content, knowledgeContext) : content;

    try {
      const answer = await kolibriBridge.ask(prompt, mode, knowledgeContext);
      const moment = nowPair();
      const assistantMessage: ChatMessage = {
        id: crypto.randomUUID(),
        role: "assistant",
        content: answer,
        timestamp: moment.display,
        isoTimestamp: moment.iso,
        modeValue: mode,
        modeLabel: findModeLabel(mode),
      };
      if (knowledgeContext.length) {
        assistantMessage.context = knowledgeContext;
      }
      if (contextError) {
        assistantMessage.contextError = contextError;
      }
      setMessages((prev) => [...prev, assistantMessage]);
    } catch (error) {
      const moment = nowPair();
      const assistantMessage: ChatMessage = {
        id: crypto.randomUUID(),
        role: "assistant",
        content:
          error instanceof Error
            ? `Не удалось получить ответ: ${error.message}`
            : "Не удалось получить ответ от ядра Колибри.",
        timestamp: moment.display,
        isoTimestamp: moment.iso,
      };
      setMessages((prev) => [...prev, assistantMessage]);
    } finally {
      setIsProcessing(false);
      void refreshKnowledgeStatus();
    }
  }, [bridgeReady, draft, isProcessing, mode, refreshKnowledgeStatus]);

  const renameConversation = useCallback((nextTitle: string) => {
    const trimmed = nextTitle.trim();
    setConversationTitle(trimmed ? trimmed.slice(0, 80) : DEFAULT_TITLE);
  }, []);

  const latestAssistantMessage = useMemo(() => {
    for (let index = messages.length - 1; index >= 0; index -= 1) {
      const message = messages[index];
      if (message.role === "assistant") {
        return message;
      }
    }
    return undefined;
  }, [messages]);

  const metrics = useMemo<ConversationMetrics>(() => {
    let userMessages = 0;
    let assistantMessages = 0;
    let knowledgeReferences = 0;
    let lastUpdatedIso: string | undefined;

    for (const message of messages) {
      if (message.role === "user") {
        userMessages += 1;
      } else {
        assistantMessages += 1;
        if (message.context?.length) {
          knowledgeReferences += 1;
        }
      }
      if (message.isoTimestamp) {
        lastUpdatedIso = message.isoTimestamp;
      }
    }

    return {
      userMessages,
      assistantMessages,
      knowledgeReferences,
      lastUpdatedIso,
    };
  }, [messages]);

  return {
    messages,
    draft,
    mode,
    isProcessing,
    bridgeReady,
    conversationId,
    conversationTitle,
    knowledgeStatus,
    knowledgeError,
    statusLoading,
    latestAssistantMessage,
    metrics,
    setDraft,
    setMode,
    renameConversation,
    sendMessage,
    resetConversation,
    refreshKnowledgeStatus,
  };
};

export default useKolibriChat;
