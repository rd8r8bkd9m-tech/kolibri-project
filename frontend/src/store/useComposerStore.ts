import { create } from "zustand";
import type { ComposerAction } from "@/types";

interface ComposerState {
  drafts: Record<string, string>;
  activeAction: ComposerAction | null;
  focused: boolean;
  setDraft: (sessionId: string, value: string) => void;
  clearDraft: (sessionId: string) => void;
  openAction: (action: ComposerAction) => void;
  closeAction: () => void;
  setFocused: (value: boolean) => void;
}

export const useComposerStore = create<ComposerState>()((set) => ({
  drafts: {},
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
  openAction: (activeAction) => set({ activeAction }),
  closeAction: () => set({ activeAction: null }),
  setFocused: (focused) => set({ focused }),
}));
