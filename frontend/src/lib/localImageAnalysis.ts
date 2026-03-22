import type { VisionAnalyzeResponse } from "@/api";

type Rgb = [number, number, number];

function colorName([r, g, b]: Rgb): string {
  const mx = Math.max(r, g, b);
  const mn = Math.min(r, g, b);
  if (mx < 35) return "почти чёрный";
  if (mn > 220) return "почти белый";
  if (mx - mn < 18) {
    if (mx < 95) return "тёмно-серый";
    if (mx > 180) return "светло-серый";
    return "серый";
  }
  if (r >= g && r >= b) {
    if (g > 150 && b < 120) return "жёлто-оранжевый";
    if (b > 110) return "розово-фиолетовый";
    return "красный";
  }
  if (g >= r && g >= b) {
    if (r > 150) return "жёлто-зелёный";
    if (b > 130) return "бирюзовый";
    return "зелёный";
  }
  if (r > 150 && g > 150) return "светло-голубой";
  return "синий";
}

function compactText(value: string, limit = 220): string {
  const normalized = value.replace(/\s+/g, " ").trim();
  if (normalized.length <= limit) return normalized;
  return `${normalized.slice(0, limit - 1).trimEnd()}…`;
}

function loadImage(file: File): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(file);
    const image = new Image();
    image.onload = () => {
      URL.revokeObjectURL(url);
      resolve(image);
    };
    image.onerror = () => {
      URL.revokeObjectURL(url);
      reject(new Error("Не удалось декодировать изображение"));
    };
    image.src = url;
  });
}

async function detectText(image: HTMLImageElement): Promise<string> {
  const detectorCtor = (window as Window & { TextDetector?: new () => { detect(input: CanvasImageSource): Promise<Array<{ rawValue?: string }>> } }).TextDetector;
  if (!detectorCtor) return "";
  try {
    const detector = new detectorCtor();
    const result = await detector.detect(image);
    const text = result
      .map((item) => item.rawValue?.trim() ?? "")
      .filter(Boolean)
      .join(" ");
    return compactText(text, 320);
  } catch {
    return "";
  }
}

export async function analyzeImageLocally(
  file: File,
  prompt = "Опиши изображение и выдели главное по-русски.",
): Promise<VisionAnalyzeResponse> {
  const started = performance.now();
  const image = await loadImage(file);
  const canvas = document.createElement("canvas");
  const ratio = Math.max(image.naturalWidth, image.naturalHeight) / 160;
  canvas.width = Math.max(1, Math.round(image.naturalWidth / Math.max(1, ratio)));
  canvas.height = Math.max(1, Math.round(image.naturalHeight / Math.max(1, ratio)));
  const context = canvas.getContext("2d", { willReadFrequently: true });
  if (!context) {
    throw new Error("Не удалось создать локальный canvas-анализ");
  }
  context.drawImage(image, 0, 0, canvas.width, canvas.height);

  const { data } = context.getImageData(0, 0, canvas.width, canvas.height);
  const pixels = Math.max(1, data.length / 4);

  let brightnessSum = 0;
  let saturationSum = 0;
  let grayLike = 0;
  let bright = 0;
  let dark = 0;
  const brightnessValues: number[] = [];
  const buckets = new Map<string, number>();

  for (let index = 0; index < data.length; index += 4) {
    const r = data[index] ?? 0;
    const g = data[index + 1] ?? 0;
    const b = data[index + 2] ?? 0;
    const brightness = 0.299 * r + 0.587 * g + 0.114 * b;
    brightnessValues.push(brightness);
    brightnessSum += brightness;
    const mx = Math.max(r, g, b);
    const mn = Math.min(r, g, b);
    saturationSum += mx === 0 ? 0 : (mx - mn) / mx;
    if (Math.max(Math.abs(r - g), Math.abs(g - b), Math.abs(r - b)) < 14) grayLike += 1;
    if (brightness > 215) bright += 1;
    if (brightness < 40) dark += 1;
    const key = `${Math.round(r / 32)},${Math.round(g / 32)},${Math.round(b / 32)}`;
    buckets.set(key, (buckets.get(key) ?? 0) + 1);
  }

  const avgBrightness = brightnessSum / pixels;
  const avgSaturation = saturationSum / pixels;
  const grayscaleRatio = grayLike / pixels;
  const brightRatio = bright / pixels;
  const darkRatio = dark / pixels;
  const mean = avgBrightness;
  const contrast = Math.sqrt(
    brightnessValues.reduce((acc, value) => acc + (value - mean) ** 2, 0) / Math.max(1, brightnessValues.length),
  );

  const dominantColors = [...buckets.entries()]
    .sort((a, b) => b[1] - a[1])
    .slice(0, 3)
    .map(([key]) => {
      const [r, g, b] = key.split(",").map((part) => Math.min(255, Number(part) * 32)) as Rgb;
      return colorName([r, g, b]);
    })
    .filter((value, index, array) => array.indexOf(value) === index);

  const text = await detectText(image);
  const aspect = image.naturalWidth / Math.max(1, image.naturalHeight);
  const orientation = aspect > 1.15 ? "горизонтальный" : aspect < 0.87 ? "вертикальный" : "почти квадратный";

  let sceneKind = "обычное изображение";
  if (text && (brightRatio > 0.42 || grayscaleRatio > 0.45)) {
    sceneKind = "скриншот, документ или интерфейс с текстом";
  } else if (brightRatio > 0.58 && grayscaleRatio > 0.4) {
    sceneKind = "документ, схема или светлый интерфейс";
  } else if (avgSaturation > 0.28 && contrast > 36) {
    sceneKind = "фотография или насыщенная иллюстрация";
  } else if (grayscaleRatio > 0.55 && contrast < 30) {
    sceneKind = "схема, иконка или минималистичный интерфейс";
  }

  const promptLc = prompt.toLowerCase();
  const wantsText = ["текст", "что написано", "прочитай", "ocr", "надпись"].some((token) => promptLc.includes(token));

  const sentences = [
    `Я вижу ${sceneKind}. Кадр ${orientation}, разрешение ${image.naturalWidth}x${image.naturalHeight}.`,
    `По тону изображение ${avgBrightness > 170 ? "светлое" : avgBrightness < 85 ? "тёмное" : "сбалансированное"}, контраст ${contrast > 55 ? "высокий" : contrast > 28 ? "умеренный" : "мягкий"}.`,
    `Основные цвета: ${(dominantColors.length ? dominantColors : ["неопределённые"]).join(", ")}.`,
  ];

  if (darkRatio > 0.45) {
    sentences.push("В кадре много тёмных областей.");
  }
  if (text) {
    sentences.push(`На изображении локально распознан текст: «${compactText(text)}».`);
  } else if (wantsText) {
    sentences.push("Читаемый текст локально не распознан.");
  }
  if (!text && sceneKind.includes("фотография")) {
    sentences.push("Локальный анализ хорошо описывает общие признаки кадра, но без внешней модели не называет редкие объекты по имени.");
  }

  return {
    response: sentences.join(" "),
    provider: "kolibri-browser-vision",
    model: "kolibri-browser-vision-v1",
    duration_ms: Math.round((performance.now() - started) * 100) / 100,
    mime_type: file.type || "image/unknown",
    width: image.naturalWidth,
    height: image.naturalHeight,
  };
}
