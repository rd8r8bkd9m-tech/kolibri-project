import { RefObject, useEffect, useRef } from "react";

export function useAutoScroll<T extends HTMLElement>(ref: RefObject<T>, deps: unknown[]) {
  const shouldStickRef = useRef(true);
  const frameRef = useRef<number | null>(null);

  useEffect(() => {
    const node = ref.current;
    if (!node) return;

    const handleScroll = () => {
      const distanceToBottom = node.scrollHeight - (node.scrollTop + node.clientHeight);
      shouldStickRef.current = distanceToBottom < 120;
    };

    handleScroll();
    node.addEventListener("scroll", handleScroll, { passive: true });
    return () => {
      node.removeEventListener("scroll", handleScroll);
      if (frameRef.current !== null) {
        window.cancelAnimationFrame(frameRef.current);
      }
    };
  }, [ref]);

  useEffect(() => {
    const node = ref.current;
    if (!node) return;
    if (!shouldStickRef.current) return;
    if (frameRef.current !== null) {
      window.cancelAnimationFrame(frameRef.current);
    }
    frameRef.current = window.requestAnimationFrame(() => {
      const distanceToBottom = node.scrollHeight - (node.scrollTop + node.clientHeight);
      const behavior = distanceToBottom > 320 ? "auto" : "smooth";
      node.scrollTo({ top: node.scrollHeight, behavior });
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, deps);
}
