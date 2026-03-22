import { create } from "zustand";
import type { PrimarySurface, WorkspaceSurface } from "@/types";

interface ShellState {
  primarySurface: PrimarySurface;
  workspaceOpen: boolean;
  workspaceSurface: WorkspaceSurface;
  settingsOpen: boolean;
  setPrimarySurface: (surface: PrimarySurface) => void;
  openWorkspace: (surface?: WorkspaceSurface) => void;
  closeWorkspace: () => void;
  setWorkspaceSurface: (surface: WorkspaceSurface) => void;
  openSettings: () => void;
  closeSettings: () => void;
}

export const useShellStore = create<ShellState>()((set) => ({
  primarySurface: "thread",
  workspaceOpen: false,
  workspaceSurface: "swarm",
  settingsOpen: false,
  setPrimarySurface: (primarySurface) => set({ primarySurface }),
  openWorkspace: (workspaceSurface) =>
    set((state) => ({
      workspaceOpen: true,
      workspaceSurface: workspaceSurface ?? state.workspaceSurface,
    })),
  closeWorkspace: () => set({ workspaceOpen: false }),
  setWorkspaceSurface: (workspaceSurface) => set({ workspaceSurface }),
  openSettings: () => set({ settingsOpen: true }),
  closeSettings: () => set({ settingsOpen: false }),
}));
