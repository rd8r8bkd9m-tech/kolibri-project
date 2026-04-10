import { create } from "zustand";
import { devtools, persist } from "zustand/middleware";
import { uid } from "@/lib/utils";
import { sanitizeAssistantText, sanitizeAssistantTurn } from "@/lib/answerSanitizer";
import type { ChatMessage, ChatProductMeta, ChatSession, ModelOption } from "@/types";

export type ThemeMode = "dark" | "light" | "system";
export type AssistantPersona = "assistant" | "romantic" | "storyteller";

interface ChatState {
  sessions: ChatSession[];
  currentSessionId: string;
  messages: Record<string, ChatMessage[]>;
  model: ModelOption;
  theme: ThemeMode;
  thinking: boolean;
  voiceMode: boolean;
  imagineOpen: boolean;
  persona: AssistantPersona;
  memoryEnabled: boolean;
  apiToken: string;
  addSession: (title?: string) => void;
  selectSession: (id: string) => void;
  renameSession: (id: string, title: string) => void;
  pinSession: (id: string) => void;
  deleteSession: (id: string) => void;
  addMessage: (sessionId: string, message: ChatMessage) => void;
  rewriteUserTurn: (sessionId: string, messageId: string, content: string) => void;
  replaceLastAssistant: (sessionId: string, content: string, streaming?: boolean, productMeta?: ChatProductMeta) => void;
  setModel: (model: ModelOption) => void;
  setTheme: (theme: ThemeMode) => void;
  setThinking: (value: boolean) => void;
  setVoiceMode: (value: boolean) => void;
  setImagineOpen: (value: boolean) => void;
  setPersona: (value: AssistantPersona) => void;
  setMemoryEnabled: (value: boolean) => void;
  setApiToken: (value: string) => void;
  hydrateSessions: (sessions: ChatSession[]) => void;
  hydrateMessages: (sessionId: string, messages: ChatMessage[]) => void;
  adoptSessionId: (fromId: string, toId: string) => void;
}

const initialSessionId = uid("chat");
const MAX_PERSIST_SESSIONS = 24;
const MAX_PERSIST_MESSAGES_PER_SESSION = 120;

function createDefaultSession(id = initialSessionId): ChatSession {
  return { id, title: "Новый чат", updatedAt: Date.now(), pinned: true, customTitle: false };
}

function sanitizeSession(value: unknown): ChatSession | null {
  if (!value || typeof value !== "object") return null;
  const raw = value as Partial<ChatSession>;
  if (typeof raw.id !== "string" || !raw.id.trim()) return null;
  const title = typeof raw.title === "string" && raw.title.trim() ? raw.title.trim() : "Новый чат";
  const updatedAt = typeof raw.updatedAt === "number" && Number.isFinite(raw.updatedAt) ? raw.updatedAt : Date.now();
  const pinned = typeof raw.pinned === "boolean" ? raw.pinned : false;
  const customTitle = typeof raw.customTitle === "boolean" ? raw.customTitle : false;
  return { id: raw.id, title, updatedAt, pinned, customTitle };
}

function sanitizeMessage(value: unknown): ChatMessage | null {
  if (!value || typeof value !== "object") return null;
  const raw = value as Partial<ChatMessage>;
  if (typeof raw.id !== "string" || !raw.id.trim()) return null;
  if (raw.role !== "user" && raw.role !== "assistant") return null;
  if (typeof raw.content !== "string") return null;
  const createdAt = typeof raw.createdAt === "number" && Number.isFinite(raw.createdAt) ? raw.createdAt : Date.now();
  const editedAt = typeof raw.editedAt === "number" && Number.isFinite(raw.editedAt) ? raw.editedAt : undefined;
  const streaming = typeof raw.streaming === "boolean" ? raw.streaming : undefined;
  const imageUrl = typeof raw.imageUrl === "string" ? raw.imageUrl : undefined;
  const productMeta =
    raw.productMeta && typeof raw.productMeta === "object"
      ? {
          productMode:
            typeof raw.productMeta.productMode === "string" ? raw.productMeta.productMode : undefined,
          projectActive:
            typeof raw.productMeta.projectActive === "boolean" ? raw.productMeta.projectActive : undefined,
          domainMode:
            typeof raw.productMeta.domainMode === "string" ? raw.productMeta.domainMode : undefined,
          estimateStage:
            typeof raw.productMeta.estimateStage === "string" ? raw.productMeta.estimateStage : undefined,
          projectKind:
            typeof raw.productMeta.projectKind === "string" ? raw.productMeta.projectKind : undefined,
          projectAreaM2:
            typeof raw.productMeta.projectAreaM2 === "number" && Number.isFinite(raw.productMeta.projectAreaM2)
              ? raw.productMeta.projectAreaM2
              : undefined,
        }
      : undefined;
  return {
    id: raw.id,
    role: raw.role,
    content: raw.role === "assistant" ? sanitizeAssistantText(raw.content) : raw.content,
    createdAt,
    editedAt,
    streaming,
    imageUrl,
    productMeta,
  };
}

function trimPersistedMessages(
  sessions: ChatSession[],
  source: Record<string, ChatMessage[]>,
): Record<string, ChatMessage[]> {
  const out: Record<string, ChatMessage[]> = {};
  for (const session of sessions) {
    const list = source[session.id] ?? [];
    out[session.id] = list.slice(-MAX_PERSIST_MESSAGES_PER_SESSION);
  }
  return out;
}

export function normalizeConversationMessages(list: ChatMessage[]): ChatMessage[] {
  const out = [...list];
  for (let i = 0; i < out.length; i += 1) {
    const current = out[i];
    if (!current || current.role !== "assistant") continue;
    const previous = out[i - 1];
    if (previous?.role === "user") {
      out[i] = { ...current, content: sanitizeAssistantTurn(previous.content, current.content) };
      continue;
    }
    out[i] = { ...current, content: sanitizeAssistantText(current.content) };
  }
  return out;
}

export const useChatStore = create<ChatState>()(
  persist(
    devtools((set, get) => ({
    sessions: [createDefaultSession()],
    currentSessionId: initialSessionId,
    messages: { [initialSessionId]: [] },
    model: "Колибри 4.1 • Быстрая",
    theme: "system",
    thinking: false,
    voiceMode: false,
    imagineOpen: false,
    persona: "assistant",
    memoryEnabled: true,
    apiToken: "",

    addSession: (title) => {
      set((state) => {
        const current = state.sessions.find((s) => s.id === state.currentSessionId);
        const currentMessages = state.messages[state.currentSessionId] ?? [];
        const isUntouchedDraft = !title && current?.title === "Новый чат" && currentMessages.length === 0;

        if (isUntouchedDraft) return {};

        const id = uid("chat");
        const session: ChatSession = {
          id,
          title: title ?? "Новый чат",
          updatedAt: Date.now(),
          customTitle: Boolean(title?.trim()),
        };

        return {
          sessions: [session, ...state.sessions],
          currentSessionId: id,
          messages: { ...state.messages, [id]: [] },
        };
      });
    },

    selectSession: (id) => set({ currentSessionId: id }),

    renameSession: (id, title) =>
      set((state) => ({
        sessions: state.sessions.map((s) =>
          s.id === id
            ? {
                ...s,
                title: title.trim() || "Новый чат",
                customTitle: Boolean(title.trim()),
                updatedAt: Date.now(),
              }
            : s,
        ),
      })),

    pinSession: (id) =>
      set((state) => ({
        sessions: state.sessions.map((s) => (s.id === id ? { ...s, pinned: !s.pinned } : s)),
      })),

    deleteSession: (id) => {
      // Best-effort cleanup backend session memory.
      fetch(`/api/v1/ai/conversations/${encodeURIComponent(id)}`, { method: "DELETE" }).catch(() => {});
      set((state) => {
        if (!state.sessions.some((s) => s.id === id)) return {};

        const sessions = state.sessions.filter((s) => s.id !== id);
        const messages = { ...state.messages };
        delete messages[id];

        if (!sessions.length) {
          const fallbackId = uid("chat");
          return {
            sessions: [{ id: fallbackId, title: "Новый чат", updatedAt: Date.now(), pinned: true, customTitle: false }],
            currentSessionId: fallbackId,
            messages: { [fallbackId]: [] },
          };
        }

        return {
          sessions,
          currentSessionId: state.currentSessionId === id ? sessions[0].id : state.currentSessionId,
          messages,
        };
      });
    },

    addMessage: (sessionId, message) =>
      set((state) => ({
        sessions: state.sessions.map((s) =>
          s.id === sessionId
            ? {
                ...s,
                updatedAt: Date.now(),
                title:
                  !s.customTitle && s.title === "Новый чат"
                    ? message.content.slice(0, 36) || s.title
                    : s.title,
              }
            : s,
        ),
        messages: {
          ...state.messages,
          [sessionId]: [...(state.messages[sessionId] ?? []), message],
        },
      })),

    rewriteUserTurn: (sessionId, messageId, content) =>
      set((state) => {
        const list = [...(state.messages[sessionId] ?? [])];
        const index = list.findIndex((item) => item.id === messageId && item.role === "user");
        if (index < 0) return {};

        const nextTitle = content.trim().slice(0, 36) || "Новый чат";
        const nextList = list.slice(0, index + 1);
        nextList[index] = {
          ...nextList[index],
          content,
          editedAt: Date.now(),
        };

        return {
          sessions: state.sessions.map((session) =>
            session.id === sessionId
              ? {
                  ...session,
                  updatedAt: Date.now(),
                  title: !session.customTitle && index === 0 ? nextTitle : session.title,
                }
              : session,
          ),
          messages: {
            ...state.messages,
            [sessionId]: nextList,
          },
        };
      }),

    replaceLastAssistant: (sessionId, content, streaming = true, productMeta) =>
      set((state) => {
        const list = [...(state.messages[sessionId] ?? [])];
        for (let i = list.length - 1; i >= 0; i -= 1) {
          if (list[i]?.role === "assistant") {
            const previous = list[i - 1];
            list[i] = {
              ...list[i],
              content:
                previous?.role === "user"
                  ? sanitizeAssistantTurn(previous.content, content)
                  : sanitizeAssistantText(content),
              streaming,
              productMeta: productMeta ?? list[i]?.productMeta,
            };
            break;
          }
        }
        return { messages: { ...state.messages, [sessionId]: list } };
      }),

    setModel: (model) => set({ model }),
    setTheme: (theme) => set({ theme }),
    setThinking: (thinking) => set({ thinking }),
    setVoiceMode: (voiceMode) => set({ voiceMode }),
    setImagineOpen: (imagineOpen) => set({ imagineOpen }),
    setPersona: (persona) => set({ persona }),
    setMemoryEnabled: (memoryEnabled) => set({ memoryEnabled }),
    setApiToken: (apiToken) => set({ apiToken }),
    hydrateSessions: (incoming) =>
      set((state) => {
        const sanitized = incoming
          .map(sanitizeSession)
          .filter((session): session is ChatSession => session !== null)
          .slice(0, MAX_PERSIST_SESSIONS);
        if (!sanitized.length) return {};

        const nextMessages = { ...state.messages };
        for (const session of sanitized) {
          if (!nextMessages[session.id]) {
            nextMessages[session.id] = [];
          }
        }

        return {
          sessions: sanitized,
          currentSessionId: sanitized.some((session) => session.id === state.currentSessionId)
            ? state.currentSessionId
            : sanitized[0].id,
          messages: nextMessages,
        };
      }),
    hydrateMessages: (sessionId, incoming) =>
      set((state) => {
        if (!sessionId) return {};
        const sanitized = normalizeConversationMessages(
          incoming
            .map(sanitizeMessage)
            .filter((message): message is ChatMessage => message !== null)
            .slice(-MAX_PERSIST_MESSAGES_PER_SESSION),
        );
        return {
          messages: {
            ...state.messages,
            [sessionId]: sanitized,
          },
        };
      }),
    adoptSessionId: (fromId, toId) =>
      set((state) => {
        const sourceId = String(fromId || "").trim();
        const targetId = String(toId || "").trim();
        if (!sourceId || !targetId || sourceId === targetId) return {};

        const sourceSession = state.sessions.find((session) => session.id === sourceId);
        if (!sourceSession) return {};

        const existingTarget = state.sessions.find((session) => session.id === targetId);
        const sourceMessages = state.messages[sourceId] ?? [];
        const targetMessages = state.messages[targetId] ?? [];
        const mergedMessages = normalizeConversationMessages(
          (targetMessages.length ? targetMessages : sourceMessages)
            .map(sanitizeMessage)
            .filter((message): message is ChatMessage => message !== null)
            .slice(-MAX_PERSIST_MESSAGES_PER_SESSION),
        );

        const nextMessages = { ...state.messages };
        delete nextMessages[sourceId];
        nextMessages[targetId] = mergedMessages;

        const nextSessions = existingTarget
          ? state.sessions.filter((session) => session.id !== sourceId)
          : state.sessions.map((session) =>
              session.id === sourceId
                ? {
                    ...session,
                    id: targetId,
                  }
                : session,
            );

        return {
          sessions: nextSessions,
          currentSessionId: state.currentSessionId === sourceId ? targetId : state.currentSessionId,
          messages: nextMessages,
        };
      }),
    })),
    {
      name: "kolibri-ui",
      partialize: (state) => ({
        sessions: state.sessions.slice(0, MAX_PERSIST_SESSIONS),
        currentSessionId: state.currentSessionId,
        messages: trimPersistedMessages(
          state.sessions.slice(0, MAX_PERSIST_SESSIONS),
          state.messages,
        ),
        model: state.model,
        theme: state.theme,
        persona: state.persona,
        memoryEnabled: state.memoryEnabled,
        apiToken: state.apiToken,
      }),
      merge: (persistedState, currentState) => {
        const persisted = (persistedState ?? {}) as Partial<ChatState>;

        const persistedSessionsRaw = Array.isArray(persisted.sessions) ? persisted.sessions : [];
        const persistedSessions = persistedSessionsRaw
          .map(sanitizeSession)
          .filter((s): s is ChatSession => s !== null)
          .slice(0, MAX_PERSIST_SESSIONS);

        const sessions = persistedSessions.length ? persistedSessions : currentState.sessions;

        const persistedMessagesRaw = persisted.messages && typeof persisted.messages === "object"
          ? persisted.messages
          : {};
        const messages: Record<string, ChatMessage[]> = {};
        for (const session of sessions) {
          const list = Array.isArray((persistedMessagesRaw as Record<string, unknown>)[session.id])
            ? ((persistedMessagesRaw as Record<string, unknown>)[session.id] as unknown[])
            : [];
          messages[session.id] = normalizeConversationMessages(
            list
            .map(sanitizeMessage)
            .filter((m): m is ChatMessage => m !== null)
            .slice(-MAX_PERSIST_MESSAGES_PER_SESSION),
          );
        }

        const persistedCurrent = typeof persisted.currentSessionId === "string"
          ? persisted.currentSessionId
          : "";
        const currentSessionId = sessions.some((s) => s.id === persistedCurrent)
          ? persistedCurrent
          : sessions[0]?.id ?? currentState.currentSessionId;

        return {
          ...currentState,
          ...persisted,
          sessions: sessions.length ? sessions : [createDefaultSession()],
          currentSessionId,
          messages,
        };
      },
    },
  ),
);

export function currentMessages() {
  const state = useChatStore.getState();
  return state.messages[state.currentSessionId] ?? [];
}
