import { create } from "zustand";
import type { ComposerAction } from "@/types";

export interface PendingAttachmentPreview {
  file: File;
  kind: "image" | "text";
  name: string;
  mimeType: string;
  size: number;
  objectUrl?: string;
  textPreview?: string;
}

interface ComposerState {
  drafts: Record<string, string>;
  pendingAttachments: Record<string, PendingAttachmentPreview | undefined>;
  activeAction: ComposerAction | null;
  focused: boolean;
  setDraft: (sessionId: string, value: string) => void;
  clearDraft: (sessionId: string) => void;
  setPendingAttachment: (sessionId: string, attachment: PendingAttachmentPreview) => void;
  clearPendingAttachment: (sessionId: string) => void;
  openAction: (action: ComposerAction) => void;
  closeAction: () => void;
  setFocused: (value: boolean) => void;
}

export const useComposerStore = create<ComposerState>()((set) => ({
  drafts: {},
  pendingAttachments: {},
  activeAction: null,
  focused: false,
  setDraft: (sessionId, value) =>
    set((state) => ({
      drafts: {
        ...state.drafts,
        [sessionId]: value,
      },
    })),
  clearDraft: (sessionId) =>
    set((state) => {
      const nextDrafts = { ...state.drafts };
      delete nextDrafts[sessionId];
      return { drafts: nextDrafts };
    }),
  setPendingAttachment: (sessionId, attachment) =>
    set((state) => {
      const previous = state.pendingAttachments[sessionId];
      if (previous?.objectUrl) {
        URL.revokeObjectURL(previous.objectUrl);
      }
      return {
        pendingAttachments: {
          ...state.pendingAttachments,
          [sessionId]: attachment,
        },
      };
    }),
  clearPendingAttachment: (sessionId) =>
    set((state) => {
      const previous = state.pendingAttachments[sessionId];
      if (previous?.objectUrl) {
        URL.revokeObjectURL(previous.objectUrl);
      }
      const nextPending = { ...state.pendingAttachments };
      delete nextPending[sessionId];
      return { pendingAttachments: nextPending };
    }),
  openAction: (activeAction) => set({ activeAction }),
  closeAction: () => set({ activeAction: null }),
  setFocused: (focused) => set({ focused }),
}));
