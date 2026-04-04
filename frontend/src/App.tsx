import { lazy, Suspense, useEffect } from "react";
import { AppErrorBoundary } from "@/features/app-shell/AppErrorBoundary";
import { useChatStore } from "@/store/useChatStore";

const AppShellV3 = lazy(() =>
  import("@/features/app-shell/AppShellV3").then((module) => ({ default: module.AppShellV3 })),
);

export default function App() {
  const theme = useChatStore((s) => s.theme);

  useEffect(() => {
    const media = window.matchMedia("(prefers-color-scheme: dark)");
    const root = document.documentElement;
    const applyTheme = () => {
      const resolved = theme === "system" ? (media.matches ? "dark" : "light") : theme;
      root.classList.remove("theme-dark", "theme-light");
      root.classList.add(resolved === "light" ? "theme-light" : "theme-dark");
      document.body.classList.remove("theme-dark", "theme-light");
      document.body.classList.add(resolved === "light" ? "theme-light" : "theme-dark");
      root.style.colorScheme = resolved;
    };
    applyTheme();
    if (typeof media.addEventListener === "function") {
      media.addEventListener("change", applyTheme);
      return () => media.removeEventListener("change", applyTheme);
    }
    media.addListener(applyTheme);
    return () => media.removeListener(applyTheme);
  }, [theme]);

  useEffect(() => {
    const root = document.documentElement;
    const viewport = window.visualViewport;

    const applyAppHeight = () => {
      const nextHeight = viewport?.height ?? window.innerHeight;
      root.style.setProperty("--app-height", `${Math.round(nextHeight)}px`);
    };

    applyAppHeight();

    if (viewport) {
      viewport.addEventListener("resize", applyAppHeight);
      viewport.addEventListener("scroll", applyAppHeight);
      window.addEventListener("orientationchange", applyAppHeight);
      window.addEventListener("resize", applyAppHeight);
      return () => {
        viewport.removeEventListener("resize", applyAppHeight);
        viewport.removeEventListener("scroll", applyAppHeight);
        window.removeEventListener("orientationchange", applyAppHeight);
        window.removeEventListener("resize", applyAppHeight);
      };
    }

    window.addEventListener("resize", applyAppHeight);
    window.addEventListener("orientationchange", applyAppHeight);
    return () => {
      window.removeEventListener("resize", applyAppHeight);
      window.removeEventListener("orientationchange", applyAppHeight);
    };
  }, []);

  return (
    <AppErrorBoundary>
      <Suspense fallback={<div className="h-dvh w-full bg-background" />}>
        <AppShellV3 />
      </Suspense>
    </AppErrorBoundary>
  );
}
