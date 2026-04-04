import { useCallback, useRef } from "react";
import { useQueryClient } from "@tanstack/react-query";
import {
  analyzeImageAttachment,
  downloadSwarmKpack,
  exportSwarmKpack,
  importSwarmKpack,
  learnTextWithSwarmDemo,
  streamChat,
} from "@/api";
import { accountQueryKeys } from "@/features/account/query";
import { appQueryKeys } from "@/features/workspace/query";
import { uid } from "@/lib/utils";
import { useChatStore } from "@/store/useChatStore";
import type { KolibriContextTurn } from "@/lib/kolibriBridge";
import type { ChatMessage, LearnTextDemoResponse } from "@/types";

function formatSigned(value: number | null | undefined): string {
  const num = Number(value ?? 0);
  return `${num >= 0 ? "+" : ""}${num.toFixed(3)}`;
}

function getDemoSwarmNodeCount(result: LearnTextDemoResponse): number {
  const after = result.demo.after as { swarm_topology?: { target_node_count?: number } } | undefined;
  const count = Number(after?.swarm_topology?.target_node_count ?? 0);
  return Number.isFinite(count) && count > 0 ? count : 50;
}

function formatDemoAssistantMessage(result: LearnTextDemoResponse): string {
  const lines: string[] = ["Рой обновлён."];
  const summary = result.demo.comparison_summary;
  const swarmNodeCount = getDemoSwarmNodeCount(result);

  if (summary) {
    lines.push("");
    lines.push(`Сравнение 1 vs ${swarmNodeCount}:`);
    lines.push(
      `1 узел: ${summary.single_hit_before.toFixed(3)} -> ${summary.single_hit_after.toFixed(3)} (${formatSigned(
        summary.single_hit_after - summary.single_hit_before,
      )})`,
    );
    lines.push(
      `${swarmNodeCount} узлов: ${summary.swarm_hit_before.toFixed(3)} -> ${summary.swarm_hit_after.toFixed(3)} (${formatSigned(
        summary.swarm_hit_after - summary.swarm_hit_before,
      )})`,
    );
    lines.push(
      `Преимущество роя: ${formatSigned(summary.swarm_vs_single_before)} -> ${formatSigned(
        summary.swarm_vs_single_after,
      )} (${formatSigned(summary.swarm_vs_single_after - summary.swarm_vs_single_before)})`,
    );

    if (summary.focus_domain) {
      const focusDocsDelta = Number(summary.focus_domain_documents_delta ?? 0);
      const focusSwarmDelta = Number(summary.focus_domain_swarm_hit_delta ?? 0);
      const focusSingleDelta = Number(summary.focus_domain_single_hit_delta ?? 0);
      lines.push(
        `Лучший домен: ${summary.focus_domain} (документов ${focusDocsDelta >= 0 ? "+" : ""}${focusDocsDelta}, рой ${formatSigned(focusSwarmDelta)}, 1 узел ${formatSigned(focusSingleDelta)})`,
      );
    }
  }

  if (result.report.trim()) {
    lines.push("");
    lines.push("Отчёт:");
    lines.push(result.report.trim());
  }

  lines.push("");
  lines.push("Ответ ядра:");
  lines.push(result.chat.response);
  return lines.join("\n");
}

export function useStreaming() {
  const abortRef = useRef<AbortController | null>(null);
  const queryClient = useQueryClient();

  const refreshWorkspaceQueries = useCallback(async () => {
    await Promise.all([
      queryClient.invalidateQueries({ queryKey: appQueryKeys.swarmStatus }),
      queryClient.invalidateQueries({ queryKey: appQueryKeys.qualityHistory(12) }),
    ]);
  }, [queryClient]);

  const send = useCallback(async (
    prompt: string,
    options?: { displayContent?: string; imageUrl?: string; replaceMessageId?: string },
  ) => {
    const state = useChatStore.getState();
    const sessionId = state.currentSessionId;

    if (options?.replaceMessageId) {
      state.rewriteUserTurn(sessionId, options.replaceMessageId, options.displayContent ?? prompt);
    }

    const existingMessages = useChatStore.getState().messages[sessionId] ?? [];

    const context: KolibriContextTurn[] = [];
    for (let i = 0; i < existingMessages.length - 1; i += 1) {
      const current = existingMessages[i];
      const next = existingMessages[i + 1];
      if (current?.role === "user" && next?.role === "assistant") {
        context.push({ prompt: current.content, answer: next.content });
      }
    }

    if (!options?.replaceMessageId) {
      const userMessage: ChatMessage = {
        id: uid("msg"),
        role: "user",
        content: options?.displayContent ?? prompt,
        createdAt: Date.now(),
        imageUrl: options?.imageUrl,
      };
      state.addMessage(sessionId, userMessage);
    }
    state.setThinking(true);

    let partial = "";
    let assistantInserted = false;
    const assistantMessageId = uid("msg");

    const ensureAssistantMessage = (initialContent = "") => {
      if (assistantInserted) return;
      const assistantMessage: ChatMessage = {
        id: assistantMessageId,
        role: "assistant",
        content: initialContent,
        createdAt: Date.now(),
        streaming: true,
      };
      useChatStore.getState().addMessage(sessionId, assistantMessage);
      assistantInserted = true;
    };

    // Вставляем плейсхолдер сразу, чтобы UI не выглядел "зависшим",
    // пока backend собирает ответ.
    ensureAssistantMessage("…");

    abortRef.current?.abort();
    abortRef.current = new AbortController();

    try {
      const streamResult = await streamChat(
        {
          prompt,
          model: state.model,
          sessionId,
          persona: state.persona,
          memoryEnabled: state.memoryEnabled,
          context: context.slice(-6),
        },
        (token) => {
          partial += token;
          if (!assistantInserted && token.trim().length > 0) {
            ensureAssistantMessage();
          }
          if (assistantInserted) {
            useChatStore.getState().replaceLastAssistant(sessionId, partial, true);
          }
        },
        abortRef.current.signal,
      );
      const finalState = useChatStore.getState();
      const finalConversationId = streamResult.conversationId?.trim();
      if (finalConversationId && finalConversationId !== sessionId) {
        finalState.adoptSessionId(sessionId, finalConversationId);
      }

      if (!assistantInserted) {
        ensureAssistantMessage(partial || "Ответ сформирован.");
      }
      const persistedSessionId = finalConversationId && finalConversationId !== sessionId ? finalConversationId : sessionId;
      useChatStore.getState().replaceLastAssistant(persistedSessionId, partial || "Ответ сформирован.", false);
      void Promise.all([
        queryClient.invalidateQueries({ queryKey: accountQueryKeys.conversationTurnsRoot(sessionId) }),
        queryClient.invalidateQueries({
          queryKey: accountQueryKeys.conversationTurnsRoot(persistedSessionId),
        }),
      ]);
    } catch (error) {
      const isAbort = error instanceof DOMException && error.name === "AbortError";
      if (!assistantInserted) {
        ensureAssistantMessage(partial);
      }
      if (isAbort) {
        useChatStore.getState().replaceLastAssistant(sessionId, partial || "Ответ остановлен.", false);
      } else {
        useChatStore
          .getState()
          .replaceLastAssistant(sessionId, partial || "Не удалось получить ответ модели. Попробуйте ещё раз.", false);
      }
    } finally {
      useChatStore.getState().setThinking(false);
      void queryClient.invalidateQueries({ queryKey: accountQueryKeys.conversationSessions });
    }
  }, [queryClient]);

  const stop = useCallback(() => {
    abortRef.current?.abort();
    abortRef.current = null;
  }, []);

  const resendEditedMessage = useCallback(async (messageId: string, content: string) => {
    const normalized = content.trim();
    if (!normalized) return;
    await send(normalized, { displayContent: normalized, replaceMessageId: messageId });
  }, [send]);

  const analyzeAttachment = useCallback(async (
    file: File,
    promptOverride?: string,
    displayContentOverride?: string,
  ) => {
    const lowerName = file.name.toLowerCase();
    const isImage = file.type.startsWith("image/");
    const isTextLike =
      file.type.startsWith("text/") ||
      [".txt", ".md", ".json", ".js", ".ts", ".tsx", ".py", ".csv", ".log"].some((ext) => lowerName.endsWith(ext));
    const requestedPrompt = promptOverride?.trim();

    if (isTextLike) {
      const text = await file.text();
      const normalized = text.trim();
      if (!normalized) throw new Error("Файл пустой");
      const safeText = normalized.slice(0, 24000);
      const instruction = requestedPrompt
        ? `Проанализируй содержимое файла «${file.name}» с учётом запроса пользователя: ${requestedPrompt}\n\n${safeText}`
        : `Проанализируй содержимое файла «${file.name}» и кратко выдели главное:\n\n${safeText}`;
      await send(
        instruction,
        {
          displayContent:
            displayContentOverride ??
            (requestedPrompt
              ? `${requestedPrompt}\n\nВложение: «${file.name}».`
              : `Загружен файл «${file.name}». Проанализируй его.`),
        },
      );
      return;
    }

    if (!isImage) {
      throw new Error("Сейчас поддерживаются изображения и текстовые файлы");
    }

    const state = useChatStore.getState();
    const sessionId = state.currentSessionId;
    const imageUrl = URL.createObjectURL(file);
    const userMessage: ChatMessage = {
      id: uid("msg"),
      role: "user",
      content:
        displayContentOverride ??
        (requestedPrompt
          ? `${requestedPrompt}\n\nВложение: изображение «${file.name}».`
          : `Изображение «${file.name}». Проанализируй, что на нём изображено.`),
      createdAt: Date.now(),
      imageUrl,
    };

    state.addMessage(sessionId, userMessage);
    state.setThinking(true);
    const assistantId = uid("msg");
    state.addMessage(sessionId, {
      id: assistantId,
      role: "assistant",
      content: "Анализирую изображение…",
      createdAt: Date.now(),
      streaming: true,
    });

    try {
      const result = await analyzeImageAttachment(
        file,
        requestedPrompt || "Опиши изображение и выдели главное по-русски.",
      );
      useChatStore.getState().replaceLastAssistant(sessionId, result.response, false);
    } catch (error) {
      const message = error instanceof Error ? error.message : "Не удалось проанализировать изображение.";
      useChatStore.getState().replaceLastAssistant(sessionId, message, false);
    } finally {
      useChatStore.getState().setThinking(false);
    }
  }, [send]);

  const learnFromTextDemo = useCallback(async (payload: {
    text: string;
    question: string;
    title?: string;
    category?: string;
  }) => {
    const state = useChatStore.getState();
    const sessionId = state.currentSessionId;
    const title = payload.title?.trim() || "Новый текст";
    const question = payload.question.trim();
    const text = payload.text.trim();
    if (!text || !question) return;

    const stablePrompt = `Научить рой на тексте «${title}». Затем ответить: ${question}`;
    state.addMessage(sessionId, {
      id: uid("msg"),
      role: "user",
      content: stablePrompt,
      createdAt: Date.now(),
    });
    state.setThinking(true);

    const assistantId = uid("msg");
    state.addMessage(sessionId, {
      id: assistantId,
      role: "assistant",
      content: "Сохраняю знание, пересчитываю рой и собираю ответ…",
      createdAt: Date.now(),
      streaming: true,
    });

    try {
      const result = await learnTextWithSwarmDemo({
        text,
        question,
        title,
        source: "chat-demo",
        category: payload.category?.trim() || "manual",
        conversation_id: sessionId,
        profile: state.model === "Колибри 4 • Тяжёлая" ? "deep" : state.model === "Колибри 5 • Превью" ? "balanced" : "fast",
        time_budget_ms: state.model === "Колибри 4 • Тяжёлая" ? 22000 : state.model === "Колибри 5 • Превью" ? 14000 : 9000,
        persona: state.persona,
        memory_enabled: state.memoryEnabled,
        model: state.model,
      });
      useChatStore.getState().replaceLastAssistant(sessionId, formatDemoAssistantMessage(result), false);
      await refreshWorkspaceQueries();
    } catch (error) {
      const message = error instanceof Error ? error.message : "Не удалось обучить рой на новом тексте.";
      useChatStore.getState().replaceLastAssistant(sessionId, message, false);
    } finally {
      useChatStore.getState().setThinking(false);
    }
  }, [refreshWorkspaceQueries]);

  const exportKnowledgePack = useCallback(async (payload: { title: string; domain?: string }) => {
    const state = useChatStore.getState();
    const sessionId = state.currentSessionId;
    const title = payload.title.trim() || "Kolibri Knowledge Pack";
    const domain = payload.domain?.trim().toLowerCase();

    state.addMessage(sessionId, {
      id: uid("msg"),
      role: "user",
      content: domain
        ? `Экспортировать knowledge pack «${title}» по домену ${domain}.`
        : `Экспортировать knowledge pack «${title}».`,
      createdAt: Date.now(),
    });
    state.setThinking(true);
    state.addMessage(sessionId, {
      id: uid("msg"),
      role: "assistant",
      content: "Собираю .kpack и готовлю скачивание…",
      createdAt: Date.now(),
      streaming: true,
    });

    try {
      const packageId = title
        .toLowerCase()
        .replace(/[^a-z0-9а-яё]+/giu, "-")
        .replace(/^-+|-+$/g, "")
        .slice(0, 48) || "kolibri-pack";
      const pack = await exportSwarmKpack({
        package_id: packageId,
        title,
        language: "ru",
        domains: domain ? [domain] : undefined,
      });
      await downloadSwarmKpack({ download_url: pack.download_url, filename: pack.filename });
      await refreshWorkspaceQueries();
      useChatStore.getState().replaceLastAssistant(
        sessionId,
        [
          `.kpack готов: ${pack.filename}`,
          `Документов: ${pack.documents}.`,
          `Домены: ${pack.domains.join(", ") || "all"}.`,
          "Скачивание запущено.",
        ].join("\n"),
        false,
      );
    } catch (error) {
      const message = error instanceof Error ? error.message : "Не удалось экспортировать .kpack.";
      useChatStore.getState().replaceLastAssistant(sessionId, message, false);
    } finally {
      useChatStore.getState().setThinking(false);
    }
  }, [refreshWorkspaceQueries]);

  const importKnowledgePack = useCallback(async (file: File) => {
    const state = useChatStore.getState();
    const sessionId = state.currentSessionId;

    state.addMessage(sessionId, {
      id: uid("msg"),
      role: "user",
      content: `Импортировать knowledge pack «${file.name}».`,
      createdAt: Date.now(),
    });
    state.setThinking(true);
    state.addMessage(sessionId, {
      id: uid("msg"),
      role: "assistant",
      content: "Импортирую .kpack, обновляю живую память и пересчитываю рой…",
      createdAt: Date.now(),
      streaming: true,
    });

    try {
      const status = await importSwarmKpack(file, { refresh: true, refresh_timeout_sec: 180 });
      await refreshWorkspaceQueries();
      const imported = status.import?.imported_documents ?? 0;
      const delta = status.import?.domain_delta ?? [];
      useChatStore.getState().replaceLastAssistant(
        sessionId,
        [
          imported > 0 ? `.kpack импортирован: ${imported} документов.` : ".kpack обработан, новых документов не добавлено.",
          delta.length ? `Домены: ${delta.map((item) => `${item.domain} ${item.delta >= 0 ? "+" : ""}${item.delta}`).join(", ")}.` : "",
          status.last_knowledge_refresh_delta
            ? `Рой обновлён: documents ${status.last_knowledge_refresh_delta.documents_delta >= 0 ? "+" : ""}${status.last_knowledge_refresh_delta.documents_delta}, swarm ${formatSigned(status.last_knowledge_refresh_delta.swarm_hit_delta)}.`
            : "",
        ]
          .filter(Boolean)
          .join("\n"),
        false,
      );
    } catch (error) {
      const message = error instanceof Error ? error.message : "Не удалось импортировать .kpack.";
      useChatStore.getState().replaceLastAssistant(sessionId, message, false);
    } finally {
      useChatStore.getState().setThinking(false);
    }
  }, [refreshWorkspaceQueries]);

  return {
    send,
    stop,
    analyzeAttachment,
    resendEditedMessage,
    learnFromTextDemo,
    exportKnowledgePack,
    importKnowledgePack,
  };
}
