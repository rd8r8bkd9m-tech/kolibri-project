import { RefObject, useEffect, useRef } from "react";

export function useAutoScroll<T extends HTMLElement>(ref: RefObject<T>, deps: unknown[]) {
  const shouldStickRef = useRef(true);

  useEffect(() => {
    const node = ref.current;
    if (!node) return;

    const handleScroll = () => {
      const distanceToBottom = node.scrollHeight - (node.scrollTop + node.clientHeight);
      shouldStickRef.current = distanceToBottom < 120;
    };

    handleScroll();
    node.addEventListener("scroll", handleScroll, { passive: true });
    return () => node.removeEventListener("scroll", handleScroll);
  }, [ref]);

  useEffect(() => {
    const node = ref.current;
    if (!node) return;
    if (!shouldStickRef.current) return;
    node.scrollTo({ top: node.scrollHeight, behavior: "smooth" });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, deps);
}
