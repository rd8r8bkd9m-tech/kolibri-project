import { useEffect, useState } from "react";

const DESKTOP_QUERY = "(min-width: 1024px)";

function readDesktopMatch() {
  if (typeof window === "undefined" || typeof window.matchMedia !== "function") {
    return true;
  }
  return window.matchMedia(DESKTOP_QUERY).matches;
}

export function useShellViewport() {
  const [desktop, setDesktop] = useState(readDesktopMatch);

  useEffect(() => {
    if (typeof window === "undefined" || typeof window.matchMedia !== "function") {
      return undefined;
    }

    const media = window.matchMedia(DESKTOP_QUERY);
    const apply = () => setDesktop(media.matches);
    apply();

    if (typeof media.addEventListener === "function") {
      media.addEventListener("change", apply);
      return () => media.removeEventListener("change", apply);
    }

    media.addListener(apply);
    return () => media.removeListener(apply);
  }, []);

  return { desktop };
}
