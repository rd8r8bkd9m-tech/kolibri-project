/**
 * App.tsx
 *
 * Точка входа: основное приложение.
 * Лендинг оставлен только как явный режим: /?landing=1.
 */

import { useEffect, useMemo, useState } from "react";
import { ManusAppUnified } from "./manus/ManusAppUnified";
import { LandingPage } from "./manus/LandingPage";
import { ThemeProvider } from "./manus/ThemeContext";

const LANDING_STORAGE_KEY = "kolibri-landing-dismissed-v1";
const TEXT_SCALE_KEY = "kolibri-text-scale-v1";

const shouldShowLandingInitially = (): boolean => {
  if (typeof window === "undefined") {
    return false;
  }

  const params = new URLSearchParams(window.location.search);
  if (params.get("landing") === "1") {
    return true;
  }

  return false;
};

const App = () => {
  const [showLanding, setShowLanding] = useState<boolean>(shouldShowLandingInitially);
  const isTestMode = useMemo(() => import.meta.env.MODE === "test", []);

  useEffect(() => {
    if (typeof window === "undefined") {
      return;
    }
    try {
      const raw = localStorage.getItem(TEXT_SCALE_KEY);
      const value = raw ? Number(raw) : 1;
      if (Number.isFinite(value)) {
        const normalized = Math.min(1.3, Math.max(0.85, value));
        document.documentElement.style.setProperty("--kolibri-font-scale", normalized.toFixed(2));
      }
    } catch {
      document.documentElement.style.setProperty("--kolibri-font-scale", "1.00");
    }
  }, []);

  const enterApp = () => {
    try {
      localStorage.setItem(LANDING_STORAGE_KEY, "1");
    } catch {
      // no-op: localStorage может быть недоступен
    }
    setShowLanding(false);
  };

  return (
    <ThemeProvider>
      {showLanding && !isTestMode ? <LandingPage onEnter={enterApp} /> : <ManusAppUnified />}
    </ThemeProvider>
  );
};

export default App;
