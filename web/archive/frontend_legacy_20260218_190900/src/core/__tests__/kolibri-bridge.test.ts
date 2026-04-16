import { describe, expect, it } from "vitest";

import { buildScript } from "../kolibri-bridge";
import type { KnowledgeSnippet } from "../../types/knowledge";

describe("kolibri-bridge buildScript", () => {
  it("returns fallback program for empty prompt", () => {
    const program = buildScript("", "Быстрый ответ", []);
    expect(program).toContain("Пустой запрос");
    expect(program.trim().endsWith("конец.")).toBe(true);
  });

  it("injects mode and unique context snippets", () => {
    const context: KnowledgeSnippet[] = [
      { id: "1", title: "Doc", content: "Ответ Колибри", score: 0.9 },
      { id: "2", title: "Doc 2", content: "Ответ Колибри   ", score: 0.8 },
    ];
    const program = buildScript("Что такое Kolibri?", "Экспертный режим", context);
    const trainingMatches = program.match(/обучить связь/g) ?? [];
    expect(trainingMatches.length).toBe(1);
    expect(program).toContain('Режим: Экспертный режим');
    expect(program).toContain("Источник 1");
  });

  it("passes through user-defined programs verbatim", () => {
    const rawProgram = "начало:\n    показать \"Прямая программа\"\nконец.";
    expect(buildScript(rawProgram, "Любой", [])).toBe(`${rawProgram}\n`);
  });
});
