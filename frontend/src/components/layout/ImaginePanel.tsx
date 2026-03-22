import { motion } from "framer-motion";
import TextareaAutosize from "react-textarea-autosize";
import { useState } from "react";
import { Download, ExternalLink, Loader2 } from "lucide-react";
import { Button } from "@/components/ui/button";
import { imagineImage } from "@/api";
import { uid } from "@/lib/utils";
import { useShellStore } from "@/store/useShellStore";
import { useChatStore } from "@/store/useChatStore";

const styles = ["Кинематограф", "Фотореализм", "Аниме", "3D", "Минимализм"] as const;
const aspects = ["1:1", "9:16", "16:9"] as const;
const qualities = ["low", "medium", "high"] as const;
const models = [
  { id: "google/gemini-3-pro-image-preview", label: "Gemini 3 Pro Image" },
  { id: "google/gemini-2.5-flash-image", label: "Gemini 2.5 Flash Image" },
] as const;

interface GeneratedImage {
  id: string;
  url: string;
  prompt: string;
  revisedPrompt?: string | null;
  model: string;
  style: string;
  aspect: "1:1" | "9:16" | "16:9";
  quality: "low" | "medium" | "high";
  durationMs: number;
}

export function ImaginePanel() {
  const open = useChatStore((s) => s.imagineOpen);
  const setOpen = useChatStore((s) => s.setImagineOpen);
  const setPrimarySurface = useShellStore((s) => s.setPrimarySurface);
  const addMessage = useChatStore((s) => s.addMessage);
  const currentSessionId = useChatStore((s) => s.currentSessionId);
  const [prompt, setPrompt] = useState("");
  const [style, setStyle] = useState<(typeof styles)[number]>("Фотореализм");
  const [aspect, setAspect] = useState<(typeof aspects)[number]>("1:1");
  const [quality, setQuality] = useState<(typeof qualities)[number]>("medium");
  const [modelId, setModelId] = useState<(typeof models)[number]["id"]>(models[0].id);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");
  const [results, setResults] = useState<GeneratedImage[]>([]);

  if (!open) return null;

  const generate = async () => {
    const cleanPrompt = prompt.trim();
    if (!cleanPrompt || loading) return;

    setLoading(true);
    setError("");
    try {
      const result = await imagineImage({
        prompt: cleanPrompt,
        style,
        aspect,
        quality,
        model: modelId,
      });

      const next: GeneratedImage = {
        id: uid("img"),
        url: result.image_url,
        prompt: cleanPrompt,
        revisedPrompt: result.revised_prompt,
        model: result.model,
        style,
        aspect,
        quality,
        durationMs: result.duration_ms,
      };
      setResults((prev) => [next, ...prev].slice(0, 20));

      addMessage(currentSessionId, {
        id: uid("msg"),
        role: "assistant",
        content: `Сгенерировано изображение. Модель: ${next.model}, стиль: ${style}, формат: ${aspect}, качество: ${quality}.`,
        createdAt: Date.now(),
        imageUrl: result.image_url,
      });
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : "Ошибка генерации изображения");
    } finally {
      setLoading(false);
    }
  };

  return (
    <motion.section
      initial={{ opacity: 0, y: 24 }}
      animate={{ opacity: 1, y: 0 }}
      exit={{ opacity: 0, y: 24 }}
      className="absolute inset-0 z-40 overflow-auto bg-background/95 px-4 pb-36 pt-16 backdrop-blur-md lg:inset-14 lg:border lg:border-border/10 lg:bg-background/88 lg:p-4"
    >
      <div className="mx-auto max-w-5xl space-y-4">
        <div className="flex items-center justify-between">
          <h3 className="text-lg font-semibold tracking-tight">Генерация изображений</h3>
          <Button
            variant="ghost"
            onClick={() => {
              setOpen(false);
              setPrimarySurface("thread");
            }}
          >
            Закрыть
          </Button>
        </div>

        <TextareaAutosize
          minRows={4}
          value={prompt}
          onChange={(e) => setPrompt(e.target.value)}
          placeholder="Опишите сцену подробно: свет, композиция, стиль, детали..."
          className="w-full rounded-2xl border border-border/10 bg-card/70 p-4 text-base"
        />

        <div className="grid grid-cols-1 gap-2 md:grid-cols-2">
          {models.map((item) => (
            <Button
              key={item.id}
              type="button"
              variant={modelId === item.id ? "default" : "outline"}
              className="justify-start rounded-xl"
              onClick={() => setModelId(item.id)}
            >
              {item.label}
            </Button>
          ))}
        </div>

        <div className="flex gap-2 overflow-x-auto pb-2">
          {styles.map((item) => (
            <Button
              key={item}
              type="button"
              variant={style === item ? "default" : "outline"}
              className="rounded-full whitespace-nowrap"
              onClick={() => setStyle(item)}
            >
              {item}
            </Button>
          ))}
        </div>

        <div className="flex gap-2">
          {aspects.map((item) => (
            <Button
              key={item}
              type="button"
              variant={aspect === item ? "default" : "outline"}
              className="rounded-full"
              onClick={() => setAspect(item)}
            >
              {item}
            </Button>
          ))}
        </div>

        <div className="flex gap-2">
          {qualities.map((item) => (
            <Button
              key={item}
              type="button"
              variant={quality === item ? "default" : "outline"}
              className="rounded-full"
              onClick={() => setQuality(item)}
            >
              {item}
            </Button>
          ))}
        </div>

        <Button className="w-full" disabled={loading || !prompt.trim()} onClick={generate}>
          {loading ? (
            <span className="inline-flex items-center gap-2">
              <Loader2 className="h-4 w-4 animate-spin" />
              Генерирую...
            </span>
          ) : (
            "Сгенерировать"
          )}
        </Button>

        <p className="text-xs text-muted">
          Для стабильной скорости по умолчанию используется качество <b>medium</b>. Для максимально детальной картинки переключайте на <b>high</b>.
        </p>

        {error ? <p className="rounded-xl border border-border/10 bg-card/70 px-3 py-2 text-sm text-foreground">{error}</p> : null}

        {results.length ? (
          <div className="columns-1 gap-3 space-y-3 sm:columns-2 lg:columns-3">
            {results.map((item) => (
              <article key={item.id} className="mb-3 break-inside-avoid overflow-hidden rounded-2xl border border-border/10 bg-card/70">
                <a href={item.url} target="_blank" rel="noreferrer" className="block">
                  <img src={item.url} alt={item.prompt} className="w-full object-cover" loading="lazy" />
                </a>
                <div className="space-y-2 p-3">
                  <p className="line-clamp-3 text-sm font-medium">{item.prompt}</p>
                  {item.revisedPrompt ? <p className="line-clamp-2 text-xs text-muted">{item.revisedPrompt}</p> : null}
                  <p className="text-xs text-muted/90">
                    {item.model} • {item.style} • {item.aspect} • {item.quality} • {Math.round(item.durationMs)} мс
                  </p>
                  <div className="flex items-center gap-2">
                    <a href={item.url} target="_blank" rel="noreferrer" className="inline-flex items-center gap-1 rounded-lg border border-border/15 px-2 py-1 text-xs text-foreground hover:bg-card/90">
                      <ExternalLink className="h-3.5 w-3.5" />
                      Открыть
                    </a>
                    <a href={item.url} download className="inline-flex items-center gap-1 rounded-lg border border-border/15 px-2 py-1 text-xs text-foreground hover:bg-card/90">
                      <Download className="h-3.5 w-3.5" />
                      Скачать
                    </a>
                  </div>
                </div>
              </article>
            ))}
          </div>
        ) : null}
      </div>
    </motion.section>
  );
}
