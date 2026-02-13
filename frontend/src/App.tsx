/**
 * App.tsx
 *
 * Точка входа: лендинг + основное приложение.
 */

import { useMemo, useState } from "react";
import { ManusAppUnified } from "./manus/ManusAppUnified";
import { LandingPage } from "./manus/LandingPage";
import { ThemeProvider } from "./manus/ThemeContext";

const LANDING_STORAGE_KEY = "kolibri-landing-dismissed-v1";

const shouldShowLandingInitially = (): boolean => {
  if (typeof window === "undefined") {
    return false;
  }

  const params = new URLSearchParams(window.location.search);
  if (params.get("landing") === "1") {
    return true;
  }
  if (params.get("app") === "1") {
    return false;
  }

  // На мобильных устройствах запускаем сразу приложение:
  // лендинг добавлял лишний шаг и мешал быстрому входу в чат.
  if (typeof window.matchMedia === "function" && window.matchMedia("(max-width: 900px)").matches) {
    return false;
  }

  try {
    return localStorage.getItem(LANDING_STORAGE_KEY) !== "1";
  } catch {
    return true;
  }
};

const App = () => {
  const [showLanding, setShowLanding] = useState<boolean>(shouldShowLandingInitially);
  const isTestMode = useMemo(() => import.meta.env.MODE === "test", []);

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
