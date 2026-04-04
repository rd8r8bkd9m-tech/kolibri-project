import { lazy, Suspense } from "react";
import { MessagesSquare, Sparkles } from "lucide-react";
import { ThreadViewportV3 } from "@/features/thread/ThreadViewportV3";
import { ThreadSidebarV3 } from "@/features/thread/ThreadSidebarV3";
import { useAccountBootstrap } from "@/features/account/useAccountBootstrap";
import { useShellViewport } from "@/features/app-shell/useShellViewport";
import { cn } from "@/lib/utils";
import { useComposerStore } from "@/store/useComposerStore";
import { useChatStore } from "@/store/useChatStore";
import { useShellStore } from "@/store/useShellStore";

const WorkspaceDrawerV3 = lazy(() =>
  import("@/features/workspace/WorkspaceDrawerV3").then((module) => ({ default: module.WorkspaceDrawerV3 })),
);
const SettingsDrawerV3 = lazy(() =>
  import("@/features/settings/SettingsDrawerV3").then((module) => ({ default: module.SettingsDrawerV3 })),
);
const ImaginePanel = lazy(() =>
  import("@/components/layout/ImaginePanel").then((module) => ({ default: module.ImaginePanel })),
);
const VoiceOverlay = lazy(() =>
  import("@/components/layout/VoiceOverlay").then((module) => ({ default: module.VoiceOverlay })),
);

function MobilePrimaryNav() {
  const primarySurface = useShellStore((s) => s.primarySurface);
  const setPrimarySurface = useShellStore((s) => s.setPrimarySurface);

  return (
    <nav className="v3-mobile-nav lg:hidden">
      {[
        { value: "chats" as const, label: "Чаты", icon: MessagesSquare },
        { value: "thread" as const, label: "Диалог", icon: Sparkles },
      ].map((item) => {
        const Icon = item.icon;
        const active = primarySurface === item.value;
        return (
          <button
            key={item.value}
            type="button"
            onClick={() => setPrimarySurface(item.value)}
            aria-pressed={active}
            aria-label={item.label}
            className={cn(
              "flex min-w-0 flex-1 items-center justify-center gap-2 rounded-full px-4 py-3 text-sm font-medium transition",
              active ? "v3-accent-solid shadow-[0_12px_28px_rgba(56,189,248,0.28)]" : "text-muted",
            )}
          >
            <Icon className="h-4.5 w-4.5" />
            {item.label}
          </button>
        );
      })}
    </nav>
  );
}

export function AppShellV3() {
  useAccountBootstrap();
  const { desktop } = useShellViewport();
  const primarySurface = useShellStore((s) => s.primarySurface);
  const workspaceOpen = useShellStore((s) => s.workspaceOpen);
  const settingsOpen = useShellStore((s) => s.settingsOpen);
  const activeAction = useComposerStore((s) => s.activeAction);
  const composerFocused = useComposerStore((s) => s.focused);
  const imagineOpen = useChatStore((s) => s.imagineOpen);
  const voiceMode = useChatStore((s) => s.voiceMode);
  const showMobileNav = !composerFocused && !activeAction && !imagineOpen && !voiceMode && !workspaceOpen && !settingsOpen;

  return (
    <div
      className="v3-shell relative overflow-clip bg-background text-foreground"
      style={{ height: "var(--app-height)", minHeight: "var(--app-height)" }}
    >
      {desktop ? (
        <div className="h-full min-h-0 min-w-0">
          <div className="mx-auto grid h-full min-h-0 min-w-0 max-w-[1680px] grid-cols-[19.5rem_minmax(0,1fr)] items-stretch gap-4 px-4 py-4">
            <ThreadSidebarV3 />
            <div className="flex h-full min-h-0 min-w-0 flex-col overflow-hidden">
              <ThreadViewportV3 />
            </div>
          </div>
        </div>
      ) : (
        <div className="grid h-full min-h-0 min-w-0 grid-rows-[minmax(0,1fr)_auto]">
          {primarySurface === "chats" ? <ThreadSidebarV3 mobile /> : <ThreadViewportV3 mobile />}
          {showMobileNav ? <MobilePrimaryNav /> : null}
        </div>
      )}

      <Suspense fallback={null}>
        {workspaceOpen ? <WorkspaceDrawerV3 /> : null}
        {settingsOpen ? <SettingsDrawerV3 /> : null}
        {imagineOpen ? <ImaginePanel /> : null}
        {voiceMode ? <VoiceOverlay /> : null}
      </Suspense>
    </div>
  );
}
