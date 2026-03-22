import { motion } from "framer-motion";
import { useEffect, useMemo, useRef, useState } from "react";
import { Mic, SendHorizontal } from "lucide-react";
import { useChatStore } from "@/store/useChatStore";
import { useShellStore } from "@/store/useShellStore";
import { useStreaming } from "@/hooks/useStreaming";

type SpeechRecognitionLike = {
  lang: string;
  interimResults: boolean;
  continuous: boolean;
  start: () => void;
  stop: () => void;
  abort: () => void;
  onresult: ((event: { results: ArrayLike<ArrayLike<{ transcript: string }>> }) => void) | null;
  onerror: ((event: { error?: string }) => void) | null;
  onend: (() => void) | null;
};

declare global {
  interface Window {
    SpeechRecognition?: new () => SpeechRecognitionLike;
    webkitSpeechRecognition?: new () => SpeechRecognitionLike;
  }
}

export function VoiceOverlay() {
  const active = useChatStore((s) => s.voiceMode);
  const setActive = useChatStore((s) => s.setVoiceMode);
  const setPrimarySurface = useShellStore((s) => s.setPrimarySurface);
  const { send } = useStreaming();
  const bars = useMemo(() => Array.from({ length: 18 }, (_, i) => i), []);
  const recognitionRef = useRef<SpeechRecognitionLike | null>(null);
  const [transcript, setTranscript] = useState("");
  const [interim, setInterim] = useState("");
  const [error, setError] = useState("");
  const [supported, setSupported] = useState(true);
  const [listening, setListening] = useState(false);

  useEffect(() => {
    if (!active) return;

    const RecognitionCtor = window.SpeechRecognition || window.webkitSpeechRecognition;
    if (!RecognitionCtor) {
      setSupported(false);
      setError("Этот браузер не поддерживает распознавание речи.");
      return;
    }

    setSupported(true);
    setError("");
    setTranscript("");
    setInterim("");

    const recognition = new RecognitionCtor();
    recognition.lang = "ru-RU";
    recognition.interimResults = true;
    recognition.continuous = true;
    recognition.onresult = (event) => {
      let finalText = "";
      let interimText = "";
      for (let i = 0; i < event.results.length; i += 1) {
        const chunk = event.results[i]?.[0]?.transcript ?? "";
        const isFinal = Boolean((event.results[i] as unknown as { isFinal?: boolean })?.isFinal);
        if (isFinal) finalText += chunk;
        else interimText += chunk;
      }
      if (finalText.trim()) {
        setTranscript((current) => `${current} ${finalText}`.trim());
      }
      setInterim(interimText.trim());
    };
    recognition.onerror = (event) => {
      setError(event.error ? `Ошибка микрофона: ${event.error}` : "Не удалось распознать голос.");
      setListening(false);
    };
    recognition.onend = () => {
      setListening(false);
    };

    recognitionRef.current = recognition;
    try {
      recognition.start();
      setListening(true);
    } catch {
      setError("Не удалось запустить микрофон.");
      setListening(false);
    }

    return () => {
      recognitionRef.current?.abort();
      recognitionRef.current = null;
      setListening(false);
    };
  }, [active]);

  if (!active) return null;

  const finalText = `${transcript} ${interim}`.trim();

  return (
    <div className="absolute inset-0 z-50 flex flex-col items-center justify-center gap-6 bg-overlay/80 px-4 backdrop-blur-sm">
      <div className="flex h-32 w-full max-w-lg items-end justify-center gap-2 px-4">
        {bars.map((bar) => (
          <motion.div
            key={bar}
            className="w-2 rounded-full bg-cyan-500"
            animate={{ height: listening ? [16, 68 + (bar % 4) * 10, 20] : 16 }}
            transition={{ duration: 0.45, repeat: listening ? Infinity : 0, delay: bar * 0.03 }}
          />
        ))}
      </div>

      <div className="max-w-2xl text-center">
        <p className="text-2xl font-bold">{listening ? "Слушаю..." : "Голосовой ввод"}</p>
        <p className="mt-2 text-sm text-muted">
          {supported ? "Говорите естественно. Когда закончите, отправьте распознанный текст в чат." : "Распознавание речи недоступно."}
        </p>
      </div>

      <div className="w-full max-w-2xl rounded-[28px] border border-border/15 bg-card/75 p-5">
        <p className="text-xs uppercase tracking-[0.18em] text-muted">Распознанный текст</p>
        <div className="mt-3 min-h-24 rounded-2xl border border-border/10 bg-background/55 px-4 py-3 text-base">
          {finalText || "Пока ничего не распознано."}
        </div>
        {error ? <p className="mt-3 text-sm text-red-300">{error}</p> : null}
      </div>

      <div className="flex flex-wrap items-center justify-center gap-3">
        <button
          type="button"
          onClick={() => {
            recognitionRef.current?.stop();
            setListening(false);
          }}
          className="inline-flex items-center gap-2 rounded-full border border-border/20 px-4 py-2 text-sm text-muted"
        >
          <Mic className="h-4 w-4" />
          Остановить
        </button>
        <button
          type="button"
          disabled={!finalText}
          onClick={async () => {
            if (!finalText) return;
            recognitionRef.current?.stop();
            setActive(false);
            setPrimarySurface("thread");
            await send(finalText);
          }}
          className="inline-flex items-center gap-2 rounded-full bg-foreground px-5 py-2 text-sm font-semibold text-background disabled:cursor-not-allowed disabled:bg-foreground/20 disabled:text-foreground/60"
        >
          <SendHorizontal className="h-4 w-4" />
          Отправить в чат
        </button>
        <button
          type="button"
          onClick={() => {
            recognitionRef.current?.abort();
            setActive(false);
            setPrimarySurface("thread");
          }}
          className="rounded-full border border-border/20 px-4 py-2 text-sm text-muted"
        >
          Закрыть
        </button>
      </div>
    </div>
  );
}
