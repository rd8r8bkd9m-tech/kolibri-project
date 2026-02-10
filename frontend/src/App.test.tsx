import { act, render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import type { MockInstance } from "vitest";
import App from "./App";

describe("Kolibri Manus UI", () => {
  let consoleErrorSpy: MockInstance<Parameters<typeof console.error>, ReturnType<typeof console.error>>;
  let fetchSpy: MockInstance;

  beforeEach(() => {
    consoleErrorSpy = vi.spyOn(console, "error").mockImplementation(() => {});
    // jsdom doesn't support scrollIntoView
    Element.prototype.scrollIntoView = vi.fn();
    fetchSpy = vi.spyOn(globalThis, "fetch").mockResolvedValue(
      new Response(JSON.stringify({ graph_patterns: 0, graph_edges: 0, formula_generation: 0, graph_documents: 0, graph_tokens: 0, graph_avg_fitness: 0, graph_avg_weight: 0, formula_fitness: 0, formula_genome_hex: "", c_model_patterns: 0, c_model_edges: 0, c_model_size_mb: 0, c_model_documents: 0, c_model_epoch: 0, c_model_avg_fitness: 0, c_model_avg_weight: 0, active_conversations: 0, model_available: false }), { status: 200, headers: { "Content-Type": "application/json" } })
    );
  });

  afterEach(() => {
    consoleErrorSpy.mockRestore();
    fetchSpy.mockRestore();
  });

  it("renders sidebar with navigation tabs", async () => {
    await act(async () => { render(<App />); });
    expect(screen.getByText("Kolibri")).toBeInTheDocument();
    expect(screen.getByText("Чат")).toBeInTheDocument();
    expect(screen.getByText("AI Агент")).toBeInTheDocument();
    expect(screen.getByText("Задачи")).toBeInTheDocument();
    expect(screen.getByText("Знания")).toBeInTheDocument();
    expect(screen.getByText("Терминал")).toBeInTheDocument();
    expect(screen.getByText("Настройки")).toBeInTheDocument();
  });

  it("renders chat welcome screen with suggestions", async () => {
    await act(async () => { render(<App />); });
    expect(screen.getByText("Числовое Мышление")).toBeInTheDocument();
  });

  it("renders chat input with correct placeholder", async () => {
    await act(async () => { render(<App />); });
    const textarea = screen.getByPlaceholderText(/паттерн слово/);
    expect(textarea).toBeInTheDocument();
  });

  it("allows typing a message", async () => {
    await act(async () => { render(<App />); });
    const textarea = screen.getByPlaceholderText(/паттерн слово/);
    await userEvent.type(textarea, "Привет");
    expect(textarea).toHaveValue("Привет");
  });

  it("sends a message and shows user bubble", async () => {
    const statsData = { graph_patterns: 100, graph_edges: 50, formula_generation: 3, graph_documents: 5, graph_tokens: 1000, graph_avg_fitness: 0.5, graph_avg_weight: 0.3, formula_fitness: 0.8, formula_genome_hex: "abc123", c_model_patterns: 0, c_model_edges: 0, c_model_size_mb: 0, c_model_documents: 0, c_model_epoch: 0, c_model_avg_fitness: 0, c_model_avg_weight: 0, active_conversations: 0, model_available: true, sentence_store_size: 10 };
    const chatResp = {
      response: "Тестовый ответ",
      confidence: 0.85,
      conversation_id: "test-conv",
      sources: [],
      knowledge_hits: 0,
      method: "greeting",
      duration_ms: 42,
      model_available: true,
    };
    fetchSpy.mockImplementation(async (url: string | URL | Request) => {
      const urlStr = typeof url === "string" ? url : url instanceof URL ? url.toString() : url.url;
      if (urlStr.includes("/ai/chat")) {
        return new Response(JSON.stringify(chatResp), { status: 200, headers: { "Content-Type": "application/json" } });
      }
      return new Response(JSON.stringify(statsData), { status: 200, headers: { "Content-Type": "application/json" } });
    });

    await act(async () => { render(<App />); });

    // Wait for stats to load with proper data
    await act(async () => { await new Promise(r => setTimeout(r, 100)); });

    const textarea = screen.getByPlaceholderText(/паттерн слово/);
    await userEvent.type(textarea, "Привет");

    // Find the send button (it has a Send icon)
    const buttons = screen.getAllByRole("button");
    const sendBtn = buttons.find(b => b.classList.contains("chat-send-btn"));
    expect(sendBtn).toBeDefined();
    await userEvent.click(sendBtn!);

    await waitFor(() => expect(screen.getAllByText("Kolibri AI").length).toBeGreaterThanOrEqual(2));
    await waitFor(() => expect(screen.getByText("Тестовый ответ")).toBeInTheDocument());
  });

  it("shows suggestion cards that fill input", async () => {
    await act(async () => { render(<App />); });
    const suggestion = screen.getByText("Покажи формулу");
    await userEvent.click(suggestion);
    const textarea = screen.getByPlaceholderText(/паттерн слово/) as HTMLTextAreaElement;
    expect(textarea.value).toBe("Покажи формулу");
  });

  it("switches tabs via sidebar navigation", async () => {
    await act(async () => { render(<App />); });
    const tasksBtn = screen.getByText("Задачи");
    await userEvent.click(tasksBtn);
    await waitFor(() => expect(screen.getByText("Задачи", { selector: "h1" })).toBeInTheDocument());
  });
});
