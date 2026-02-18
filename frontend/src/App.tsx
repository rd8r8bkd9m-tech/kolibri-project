import { FormEvent, useMemo, useState } from "react";

type ChatMessage = {
  id: string;
  role: "user" | "assistant";
  text: string;
};

type ModelsPayload = {
  primary_model: string;
  model_available: boolean;
  c_trainer_available: boolean;
  model_path: string;
  model_size_mb: number;
  patterns: number;
  edges: number;
  documents: number;
  epoch: number;
  formula_generation: number;
  embedding_vocab_size: number;
  sentence_store_size: number;
};

const API_BASE = "/api/v1/ai";

export default function App() {
  const [theme, setTheme] = useState<"dark" | "light">("dark");
  const [input, setInput] = useState("");
  const [sending, setSending] = useState(false);
  const [conversationId, setConversationId] = useState<string | null>(null);
  const [messages, setMessages] = useState<ChatMessage[]>([]);
  const [models, setModels] = useState<ModelsPayload | null>(null);
  const [modelPanelOpen, setModelPanelOpen] = useState(false);
  const [error, setError] = useState("");

  const canSend = useMemo(() => input.trim().length > 0 && !sending, [input, sending]);

  const sendMessage = async (event: FormEvent) => {
    event.preventDefault();
    const text = input.trim();
    if (!text || sending) return;

    const userMessage: ChatMessage = { id: `u-${Date.now()}`, role: "user", text };
    setMessages((prev) => [...prev, userMessage]);
    setInput("");
    setSending(true);
    setError("");

    try {
      const response = await fetch(`${API_BASE}/chat`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          message: text,
          conversation_id: conversationId,
          temperature: 0.6,
        }),
      });

      if (!response.ok) {
        throw new Error(`Chat error ${response.status}`);
      }

      const payload = await response.json();
      if (payload.conversation_id) {
        setConversationId(payload.conversation_id);
      }

      const assistantMessage: ChatMessage = {
        id: `a-${Date.now()}`,
        role: "assistant",
        text: String(payload.response || ""),
      };

      setMessages((prev) => [...prev, assistantMessage]);
    } catch (e) {
      setError(e instanceof Error ? e.message : "Unknown error");
    } finally {
      setSending(false);
    }
  };

  const loadModels = async () => {
    setError("");
    try {
      const response = await fetch(`${API_BASE}/models`);
      if (!response.ok) {
        throw new Error(`Models error ${response.status}`);
      }
      const payload = (await response.json()) as ModelsPayload;
      setModels(payload);
      setModelPanelOpen(true);
    } catch (e) {
      setError(e instanceof Error ? e.message : "Unknown error");
    }
  };

  return (
    <div className={`app-root theme-${theme}`}>
      <header className="topbar">
        <div className="brand">
          <span className="brand-mark">K</span>
          <div>
            <h1>Kolibri AI</h1>
            <p>Chat Runtime</p>
          </div>
        </div>

        <div className="topbar-actions">
          <button className="ghost-btn" onClick={loadModels}>Модели</button>
          <button
            className="ghost-btn"
            onClick={() => setTheme((t) => (t === "dark" ? "light" : "dark"))}
          >
            {theme === "dark" ? "Светлая" : "Тёмная"}
          </button>
        </div>
      </header>

      <main className="chat-layout">
        <section className="chat-panel">
          <div className="messages">
            {messages.length === 0 ? (
              <div className="empty">
                <h2>Готов к диалогу</h2>
                <p>Отправь вопрос, и я отвечу через KLM-движок.</p>
              </div>
            ) : (
              messages.map((message) => (
                <article key={message.id} className={`message ${message.role}`}>
                  <div className="bubble">{message.text}</div>
                </article>
              ))
            )}
          </div>

          <form className="composer" onSubmit={sendMessage}>
            <textarea
              value={input}
              onChange={(event) => setInput(event.target.value)}
              placeholder="Напиши вопрос…"
              rows={3}
            />
            <button type="submit" disabled={!canSend}>
              {sending ? "Отправка…" : "Отправить"}
            </button>
          </form>

          {error ? <p className="error">{error}</p> : null}
        </section>

        <aside className={`model-panel ${modelPanelOpen ? "open" : ""}`}>
          <div className="panel-head">
            <h3>Загруженные модели</h3>
            <button className="ghost-btn" onClick={() => setModelPanelOpen(false)}>Закрыть</button>
          </div>

          {!models ? (
            <p className="muted">Нажми «Модели», чтобы загрузить статус.</p>
          ) : (
            <dl>
              <dt>Primary</dt><dd>{models.primary_model}</dd>
              <dt>Model path</dt><dd>{models.model_path}</dd>
              <dt>KLM loaded</dt><dd>{models.model_available ? "yes" : "no"}</dd>
              <dt>Trainer</dt><dd>{models.c_trainer_available ? "available" : "missing"}</dd>
              <dt>Size MB</dt><dd>{models.model_size_mb.toFixed(2)}</dd>
              <dt>Patterns</dt><dd>{models.patterns}</dd>
              <dt>Edges</dt><dd>{models.edges}</dd>
              <dt>Documents</dt><dd>{models.documents}</dd>
              <dt>Formula generation</dt><dd>{models.formula_generation}</dd>
              <dt>Embedding vocab</dt><dd>{models.embedding_vocab_size}</dd>
              <dt>Sentence store</dt><dd>{models.sentence_store_size}</dd>
            </dl>
          )}
        </aside>
      </main>
    </div>
  );
}
