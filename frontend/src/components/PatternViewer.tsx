/**
 * PatternViewer.tsx — Визуализация числовых паттернов Колибри в браузере.
 *
 * Показывает:
 * - 64-цифровой числовой паттерн любого слова
 * - Тепловую карту сходства между словами
 * - Интерактивное сравнение паттернов
 */

import { useState, useMemo, useCallback } from "react";
import {
  wordToPattern,
  patternToString,
  patternSimilarity,
  patternHeatmap,
  findSimilar,
  PATTERN_SIZE,
} from "../core/numeric-patterns";

/* --- Цвета цифр для тепловой карты --- */
const DIGIT_COLORS = [
  "#1a1a2e", "#16213e", "#0f3460", "#533483",
  "#e94560", "#f38181", "#fce38a", "#eaffd0",
  "#95e1d3", "#f5f5f5",
] as const;

const digitToColor = (digit: number): string => DIGIT_COLORS[Math.min(digit, 9)];

const similarityToColor = (sim: number): string => {
  const r = Math.round(255 * (1 - sim));
  const g = Math.round(180 * sim);
  const b = Math.round(255 * sim);
  return `rgb(${r}, ${g}, ${b})`;
};

/* --- Компонент отображения одного паттерна --- */
function PatternDisplay({ word, pattern }: { word: string; pattern: Uint8Array }) {
  const patStr = patternToString(pattern);
  return (
    <div className="rounded-xl border border-border-strong bg-background-input/60 p-3">
      <p className="text-xs text-text-secondary mb-1 font-mono">«{word}»</p>
      <div className="flex flex-wrap gap-[1px]">
        {Array.from(pattern).map((digit, i) => (
          <span
            key={i}
            className="inline-block w-[14px] h-[18px] text-[10px] font-mono text-center leading-[18px] rounded-sm"
            style={{ backgroundColor: digitToColor(digit), color: digit > 6 ? "#111" : "#eee" }}
            title={`Позиция ${i}: цифра ${digit}`}
          >
            {digit}
          </span>
        ))}
      </div>
      <p className="text-[10px] text-text-secondary mt-1 font-mono select-all">{patStr}</p>
    </div>
  );
}

/* --- Мини тепловая карта --- */
function MiniHeatmap({ words }: { words: string[] }) {
  const data = useMemo(() => patternHeatmap(words.slice(0, 12)), [words]);

  if (data.labels.length < 2) return null;

  return (
    <div className="overflow-x-auto">
      <table className="border-collapse">
        <thead>
          <tr>
            <th className="text-[9px] text-text-secondary p-1" />
            {data.labels.map((label) => (
              <th key={label} className="text-[9px] text-text-secondary p-1 font-normal rotate-45 origin-bottom-left">
                {label.slice(0, 6)}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {data.matrix.map((row, i) => (
            <tr key={data.labels[i]}>
              <td className="text-[9px] text-text-secondary p-1 text-right font-mono">
                {data.labels[i].slice(0, 6)}
              </td>
              {row.map((sim, j) => (
                <td
                  key={j}
                  className="w-5 h-5 text-[8px] text-center font-mono"
                  style={{ backgroundColor: similarityToColor(sim) }}
                  title={`${data.labels[i]} ↔ ${data.labels[j]}: ${(sim * 100).toFixed(1)}%`}
                >
                  {i === j ? "—" : (sim * 100).toFixed(0)}
                </td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

/* --- Главный компонент --- */
export default function PatternViewer() {
  const [input, setInput] = useState("");
  const [words, setWords] = useState<string[]>(["kolibri", "знание", "паттерн", "формула"]);

  const handleAdd = useCallback(() => {
    const trimmed = input.trim().toLowerCase();
    if (trimmed && !words.includes(trimmed)) {
      setWords((prev) => [...prev, trimmed]);
    }
    setInput("");
  }, [input, words]);

  const handleRemove = useCallback((word: string) => {
    setWords((prev) => prev.filter((w) => w !== word));
  }, []);

  const patterns = useMemo(() => {
    return words.map((w) => ({ word: w, pattern: wordToPattern(w) }));
  }, [words]);

  const comparisons = useMemo(() => {
    if (patterns.length < 2) return [];
    const result: Array<{ a: string; b: string; similarity: number }> = [];
    for (let i = 0; i < patterns.length; i++) {
      for (let j = i + 1; j < patterns.length; j++) {
        result.push({
          a: patterns[i].word,
          b: patterns[j].word,
          similarity: patternSimilarity(patterns[i].pattern, patterns[j].pattern),
        });
      }
    }
    result.sort((a, b) => b.similarity - a.similarity);
    return result.slice(0, 10);
  }, [patterns]);

  return (
    <div className="space-y-4 p-4">
      <h3 className="text-sm font-semibold text-text-primary">
        🔢 Числовые паттерны Колибри
      </h3>

      {/* Ввод */}
      <div className="flex gap-2">
        <input
          type="text"
          value={input}
          onChange={(e) => setInput(e.target.value)}
          onKeyDown={(e) => e.key === "Enter" && handleAdd()}
          placeholder="Введите слово..."
          className="flex-1 rounded-lg border border-border-strong bg-background-input px-3 py-1.5 text-sm text-text-primary placeholder:text-text-secondary"
        />
        <button
          onClick={handleAdd}
          className="rounded-lg bg-primary px-3 py-1.5 text-sm text-white hover:opacity-90"
        >
          Добавить
        </button>
      </div>

      {/* Паттерны */}
      <div className="space-y-2">
        {patterns.map(({ word, pattern }) => (
          <div key={word} className="relative group">
            <PatternDisplay word={word} pattern={pattern} />
            <button
              onClick={() => handleRemove(word)}
              className="absolute top-2 right-2 text-xs text-text-secondary opacity-0 group-hover:opacity-100 hover:text-red-500 transition-opacity"
            >
              ✕
            </button>
          </div>
        ))}
      </div>

      {/* Сравнения */}
      {comparisons.length > 0 && (
        <div className="space-y-1">
          <h4 className="text-xs font-semibold text-text-secondary uppercase tracking-wide">
            Сходство паттернов
          </h4>
          {comparisons.map(({ a, b, similarity }) => (
            <div key={`${a}-${b}`} className="flex items-center gap-2 text-xs font-mono">
              <span className="text-text-primary">{a}</span>
              <span className="text-text-secondary">↔</span>
              <span className="text-text-primary">{b}</span>
              <div className="flex-1 h-1.5 bg-background-input rounded-full overflow-hidden">
                <div
                  className="h-full rounded-full"
                  style={{
                    width: `${similarity * 100}%`,
                    backgroundColor: similarityToColor(similarity),
                  }}
                />
              </div>
              <span className="text-text-secondary w-12 text-right">
                {(similarity * 100).toFixed(1)}%
              </span>
            </div>
          ))}
        </div>
      )}

      {/* Тепловая карта */}
      {words.length >= 3 && (
        <div>
          <h4 className="text-xs font-semibold text-text-secondary uppercase tracking-wide mb-2">
            Матрица сходства
          </h4>
          <MiniHeatmap words={words} />
        </div>
      )}
    </div>
  );
}
