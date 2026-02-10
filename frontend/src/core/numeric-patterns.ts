/**
 * numeric-patterns.ts
 *
 * Числовые паттерны Kolibri прямо в браузере.
 *
 * Реализует:
 * - DJB2 → 64-цифровой паттерн (идентичный C/Python)
 * - Сравнение паттернов (pattern_similarity)
 * - Визуализация: числовая «тепловая карта» паттернов
 * - Кластеризация по сходству для UI
 *
 * Все вычисления — чисто клиентские, без сервера.
 */

/** Размер паттерна (64 цифры, как в C и Python) */
export const PATTERN_SIZE = 64;

/** DJB2 хеш (зеркало C/Python) */
export function djb2Hash(text: string): number {
  let hash = 5381;
  for (let i = 0; i < text.length; i++) {
    // hash * 33 + char
    hash = ((hash << 5) + hash + text.charCodeAt(i)) >>> 0;
  }
  return hash;
}

/** FNV-1a хеш */
export function fnv1aHash(text: string): number {
  let hash = 0x811c9dc5;
  for (let i = 0; i < text.length; i++) {
    hash ^= text.charCodeAt(i);
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash;
}

/**
 * Генерация 64-цифрового числового паттерна из слова.
 *
 * Алгоритм идентичен C (kolibri_word_to_pattern) и Python (word_to_pattern):
 *   seed = DJB2(word)
 *   for i in 0..64:
 *     seed = seed * 6364136223846793005 + 1442695040888963407
 *     digit = (seed >> 33) % 10
 *
 * Используем BigInt для точного воспроизведения LCG на 64-бит.
 */
export function wordToPattern(word: string): Uint8Array {
  const pattern = new Uint8Array(PATTERN_SIZE);
  const w = word.toLowerCase().trim();
  let seed = BigInt(djb2Hash(w));

  const LCG_A = 6364136223846793005n;
  const LCG_C = 1442695040888963407n;
  const MASK = (1n << 64n) - 1n;

  for (let i = 0; i < PATTERN_SIZE; i++) {
    seed = (seed * LCG_A + LCG_C) & MASK;
    pattern[i] = Number((seed >> 33n) % 10n);
  }
  return pattern;
}

/** Конвертировать паттерн в строку "1234567890..." */
export function patternToString(pattern: Uint8Array): string {
  let result = "";
  for (let i = 0; i < pattern.length; i++) {
    result += pattern[i].toString();
  }
  return result;
}

/**
 * Мера сходства двух паттернов (0..1).
 *
 * Алгоритм (зеркало Python pattern_similarity):
 * - Exact match (+3)
 * - Diff = 1 (+2)
 * - Diff = 2 (+1)
 * - Diff <= 4 (+0.3)
 */
export function patternSimilarity(a: Uint8Array, b: Uint8Array): number {
  const len = Math.min(a.length, b.length, PATTERN_SIZE);
  let score = 0;
  const maxScore = len * 3; // 3 за каждую exact-позицию

  for (let i = 0; i < len; i++) {
    const diff = Math.abs(a[i] - b[i]);
    if (diff === 0) score += 3;
    else if (diff === 1) score += 2;
    else if (diff === 2) score += 1;
    else if (diff <= 4) score += 0.3;
  }

  return score / maxScore;
}

/**
 * Текст → паттерн: представить целый текст как один паттерн.
 * XOR-свёртка паттернов всех слов.
 */
export function textToPattern(text: string): Uint8Array {
  const words = text.toLowerCase().trim().split(/\s+/).filter((w) => w.length >= 2);
  if (words.length === 0) return new Uint8Array(PATTERN_SIZE);

  const result = wordToPattern(words[0]);
  for (let w = 1; w < words.length; w++) {
    const wp = wordToPattern(words[w]);
    for (let i = 0; i < PATTERN_SIZE; i++) {
      result[i] = (result[i] + wp[i]) % 10;
    }
  }
  return result;
}

/**
 * Кластеризация слов по схожести их числовых паттернов.
 *
 * Greedy: для каждого слова находим ближайший кластер.
 * Если similarity >= threshold → добавляем в кластер.
 * Иначе → создаём новый кластер.
 */
export interface PatternCluster {
  /** Слова в кластере */
  words: string[];
  /** Паттерн «центроида» (первого слова) */
  centroid: Uint8Array;
  /** Средняя схожесть внутри кластера */
  avgSimilarity: number;
}

export function clusterByPattern(words: string[], threshold = 0.35): PatternCluster[] {
  const clusters: PatternCluster[] = [];

  for (const word of words) {
    const pat = wordToPattern(word);
    let bestCluster: PatternCluster | null = null;
    let bestSim = 0;

    for (const cluster of clusters) {
      const sim = patternSimilarity(pat, cluster.centroid);
      if (sim > bestSim) {
        bestSim = sim;
        bestCluster = cluster;
      }
    }

    if (bestCluster && bestSim >= threshold) {
      bestCluster.words.push(word);
      // Пересчитываем среднюю схожесть
      const n = bestCluster.words.length;
      bestCluster.avgSimilarity = (bestCluster.avgSimilarity * (n - 1) + bestSim) / n;
    } else {
      clusters.push({
        words: [word],
        centroid: pat,
        avgSimilarity: 1.0,
      });
    }
  }

  return clusters;
}

/**
 * Тепловая карта: для массива слов генерируем матрицу схожестей.
 * Используется для визуализации в UI.
 */
export interface HeatmapData {
  labels: string[];
  matrix: number[][];
}

export function patternHeatmap(words: string[]): HeatmapData {
  const n = words.length;
  const patterns = words.map((w) => wordToPattern(w));
  const matrix: number[][] = [];

  for (let i = 0; i < n; i++) {
    const row: number[] = [];
    for (let j = 0; j < n; j++) {
      row.push(i === j ? 1.0 : patternSimilarity(patterns[i], patterns[j]));
    }
    matrix.push(row);
  }

  return { labels: words, matrix };
}

/**
 * Найти топ-N самых похожих слов из словаря.
 */
export function findSimilar(
  query: string,
  vocabulary: string[],
  topK = 5,
): Array<{ word: string; similarity: number; pattern: string }> {
  const qPat = wordToPattern(query);
  const scored = vocabulary.map((word) => {
    const wPat = wordToPattern(word);
    return {
      word,
      similarity: patternSimilarity(qPat, wPat),
      pattern: patternToString(wPat),
    };
  });

  scored.sort((a, b) => b.similarity - a.similarity);
  return scored.slice(0, topK);
}
