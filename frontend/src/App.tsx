import { useState, useRef, useEffect, useCallback } from "react";
import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";
import "./index.css";

const uid = () => Math.random().toString(36).substring(2, 10);

const API_TIMEOUT_MS = 10_000; // 10 секунд таймаут

interface Message {
  id: string;
  role: "user" | "assistant";
  content: string;
  time: string;
}

interface ChatSession {
  id: string;
  title: string;
  messages: Message[];
  createdAt: number;
}

const SUGGESTIONS = [
  { icon: "🎓", text: "Глубокое исследование" },
  { icon: "🎨", text: "Создать изображение" },
  { icon: "🎬", text: "Создать видео" },
  { icon: "💻", text: "Веб-разработка" },
  { icon: "📊", text: "Слайды" },
];

function MarkdownContent({ content, isUser }: { content: string; isUser: boolean }) {
  if (isUser) return <p>{content}</p>;
  
  return (
    <ReactMarkdown
      remarkPlugins={[remarkGfm]}
      components={{
        code: ({ inline, className, children, ...props }: any) => {
          const match = /language-(\w+)/.exec(className || "");
          return !inline && match ? (
            <div style={{ position: "relative" }}>
              <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", padding: "6px 12px", background: "rgba(0,0,0,0.05)", borderRadius: "8px 8px 0 0", fontSize: 12, color: "var(--text-tertiary)" }}>
                <span>{match[1]}</span>
                <button
                  onClick={() => navigator.clipboard.writeText(String(children).replace(/\n$/, ""))}
                  style={{ background: "none", border: "none", cursor: "pointer", color: "var(--text-secondary)", fontSize: 12 }}
                >
                  Копировать
                </button>
              </div>
              <pre style={{ margin: 0, borderRadius: "0 0 8px 8px" }}><code className={className} {...props}>{children}</code></pre>
            </div>
          ) : (
            <code className={className} {...props}>{children}</code>
          );
        },
      }}
    >
      {content}
    </ReactMarkdown>
  );
}

function App() {
  const [sessions, setSessions] = useState<ChatSession[]>(() => {
    try {
      const saved = localStorage.getItem("kolibri-sessions");
      if (saved) {
        const parsed = JSON.parse(saved);
        if (parsed.length > 0) return parsed;
      }
    } catch {}
    const id = uid();
    return [{ id, title: "Новый чат", messages: [], createdAt: Date.now() }];
  });
  const [currentId, setCurrentId] = useState(sessions[0].id);
  const [input, setInput] = useState("");
  const [loading, setLoading] = useState(false);
  const [sidebarOpen, setSidebarOpen] = useState(true);
  const [darkMode, setDarkMode] = useState(() => {
    try { return localStorage.getItem("kolibri-dark") === "true"; } catch { return false; }
  });
  const [dropdownOpen, setDropdownOpen] = useState(false);
  const [projectsOpen, setProjectsOpen] = useState(true);
  const [chatsOpen, setChatsOpen] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const messagesEndRef = useRef<HTMLDivElement>(null);
  const textareaRef = useRef<HTMLTextAreaElement>(null);
  const dropdownRef = useRef<HTMLDivElement>(null);

  const session = sessions.find((s) => s.id === currentId)!;
  const messages = session?.messages || [];

  // Save sessions to localStorage
  useEffect(() => {
    try { localStorage.setItem("kolibri-sessions", JSON.stringify(sessions)); } catch {}
  }, [sessions]);

  // Save dark mode
  useEffect(() => {
    try { localStorage.setItem("kolibri-dark", String(darkMode)); } catch {}
    if (darkMode) document.documentElement.classList.add("dark");
    else document.documentElement.classList.remove("dark");
  }, [darkMode]);

  // Auto scroll
  useEffect(() => {
    messagesEndRef.current?.scrollIntoView({ behavior: "smooth" });
  }, [messages.length, loading]);

  // Close dropdown on outside click
  useEffect(() => {
    const handleClick = (e: MouseEvent) => {
      if (dropdownRef.current && !dropdownRef.current.contains(e.target as Node)) {
        setDropdownOpen(false);
      }
    };
    if (dropdownOpen) document.addEventListener("mousedown", handleClick);
    return () => document.removeEventListener("mousedown", handleClick);
  }, [dropdownOpen]);

  // Auto-resize textarea
  const autoResize = useCallback(() => {
    const ta = textareaRef.current;
    if (ta) {
      ta.style.height = "auto";
      ta.style.height = Math.min(ta.scrollHeight, 120) + "px";
    }
  }, []);

  useEffect(() => { autoResize(); }, [input]);

  const createSession = () => {
    const ns: ChatSession = { id: uid(), title: "Новый чат", messages: [], createdAt: Date.now() };
    setSessions((prev) => [ns, ...prev]);
    setCurrentId(ns.id);
    setTimeout(() => textareaRef.current?.focus(), 100);
  };

  const deleteSession = (id: string) => {
    setSessions((prev) => {
      const filtered = prev.filter((s) => s.id !== id);
      if (currentId === id) setCurrentId(filtered[0]?.id || "");
      return filtered.length ? filtered : [{ id: uid(), title: "Новый чат", messages: [], createdAt: Date.now() }];
    });
  };

  const sendMessage = async (text?: string) => {
    const question = (text || input).trim();
    if (!question || loading) return;

    const userMsg: Message = { id: uid(), role: "user", content: question, time: new Date().toLocaleTimeString("ru-RU", { hour: "2-digit", minute: "2-digit" }) };

    setSessions((prev) =>
      prev.map((s) => {
        if (s.id !== currentId) return s;
        const msgs = [...s.messages, userMsg];
        const title = s.messages.length === 0 ? question.substring(0, 30) + (question.length > 30 ? "..." : "") : s.title;
        return { ...s, messages: msgs, title };
      })
    );

    setInput("");
    setDropdownOpen(false);
    setLoading(true);
    setError(null);

    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), API_TIMEOUT_MS);

    try {
      const res = await fetch("/api/v1/ai/chat", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ message: question }),
        signal: controller.signal,
      });
      clearTimeout(timeoutId);

      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();
      const assistantMsg: Message = { id: uid(), role: "assistant", content: data.response || "Нет ответа", time: new Date().toLocaleTimeString("ru-RU", { hour: "2-digit", minute: "2-digit" }) };
      setSessions((prev) => prev.map((s) => (s.id === currentId ? { ...s, messages: [...s.messages, assistantMsg] } : s)));
    } catch (err) {
      const msg = err instanceof Error
        ? err.name === "AbortError"
          ? "Превышено время ожидания. Попробуйте ещё раз."
          : err.message
        : "Не удалось получить ответ";
      setError(msg);
      // Добавляем сообщение об ошибке в чат для видимости
      const errMsg: Message = { id: uid(), role: "assistant", content: `⚠️ Ошибка: ${msg}`, time: new Date().toLocaleTimeString("ru-RU", { hour: "2-digit", minute: "2-digit" }) };
      setSessions((prev) => prev.map((s) => (s.id === currentId ? { ...s, messages: [...s.messages, errMsg] } : s)));
    } finally {
      setLoading(false);
    }
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === "Enter" && !e.shiftKey) { e.preventDefault(); sendMessage(); }
  };

  const handleSuggestion = (text: string) => {
    sendMessage(text);
  };

  const copyMessage = (content: string) => {
    navigator.clipboard.writeText(content);
  };

  const recent7 = sessions.filter((s) => Date.now() - s.createdAt < 7 * 86400000);
  const recent30 = sessions.filter((s) => Date.now() - s.createdAt >= 7 * 86400000 && Date.now() - s.createdAt < 30 * 86400000);

  return (
    <div className="app-container">
      {/* ═══ SIDEBAR ═══ */}
      <aside className={`sidebar ${!sidebarOpen ? "collapsed" : ""}`}>
        <div className="sidebar-header">
          <div className="sidebar-logo">
            <svg className="logo-icon" viewBox="0 0 32 32" fill="none">
              <rect width="32" height="32" rx="8" fill="#615ef0"/>
              <path d="M8 16c0-4.4 3.6-8 8-8s8 3.6 8 8-3.6 8-8 8" stroke="white" strokeWidth="2.5" strokeLinecap="round"/>
              <path d="M12 14h8M14 18h4" stroke="white" strokeWidth="2" strokeLinecap="round"/>
            </svg>
            <span className="logo-text">Колибри</span>
          </div>
          <button className="sidebar-toggle" onClick={() => setSidebarOpen(false)}>
            <svg width="18" height="18" viewBox="0 0 18 18" fill="none"><rect x="2" y="2" width="14" height="14" rx="2" stroke="currentColor" strokeWidth="1.5"/><path d="M7 9h4" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"/></svg>
          </button>
        </div>

        <div className="sidebar-nav">
          <button className="nav-item new-chat" onClick={createSession}>
            <span className="icon">
              <svg width="18" height="18" viewBox="0 0 18 18" fill="none"><path d="M9 3v12M3 9h12" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"/></svg>
            </span>
            Новый чат
          </button>
          <button className="nav-item">
            <span className="icon">
              <svg width="18" height="18" viewBox="0 0 18 18" fill="none"><circle cx="8" cy="8" r="5" stroke="currentColor" strokeWidth="1.5"/><path d="M12 12l4 4" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"/></svg>
            </span>
            Поиск в чатах
          </button>
          <button className="nav-item">
            <span className="icon">
              <svg width="18" height="18" viewBox="0 0 18 18" fill="none"><rect x="2" y="2" width="5" height="5" rx="1" stroke="currentColor" strokeWidth="1.5"/><rect x="11" y="2" width="5" height="5" rx="1" stroke="currentColor" strokeWidth="1.5"/><rect x="2" y="11" width="5" height="5" rx="1" stroke="currentColor" strokeWidth="1.5"/><rect x="11" y="11" width="5" height="5" rx="1" stroke="currentColor" strokeWidth="1.5"/></svg>
            </span>
            Сообщество
          </button>
          <button className="nav-item">
            <span className="icon">
              <svg width="18" height="18" viewBox="0 0 18 18" fill="none"><path d="M4 14V6l5-3 5 3v8l-5 3-5-3z" stroke="currentColor" strokeWidth="1.5"/><path d="M9 9v6" stroke="currentColor" strokeWidth="1.5"/><path d="M6.5 7.5L9 6l2.5 1.5" stroke="currentColor" strokeWidth="1.5"/></svg>
            </span>
            Coder
          </button>

          {/* Projects */}
          <div className="section">
            <div className={`section-header ${projectsOpen ? "open" : ""}`} onClick={() => setProjectsOpen(!projectsOpen)}>
              <span>Проекты</span>
              <span className="arrow">▶</span>
            </div>
            <div className={`section-list ${projectsOpen ? "" : "collapsed"}`}>
              <button className="nav-item" style={{ paddingLeft: 20 }}>
                <span className="icon">
                  <svg width="16" height="16" viewBox="0 0 16 16" fill="none"><path d="M3 8h10M8 3v10" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"/></svg>
                </span>
                Новый проект
              </button>
              <button className="nav-item" style={{ paddingLeft: 20 }}>
                <span className="icon">
                  <svg width="16" height="16" viewBox="0 0 16 16" fill="none"><path d="M2 4a2 2 0 012-2h4l2 2h4a2 2 0 012 2v6a2 2 0 01-2 2H4a2 2 0 01-2-2V4z" stroke="currentColor" strokeWidth="1.5"/></svg>
                </span>
                колибри
              </button>
            </div>
          </div>

          {/* Chats */}
          <div className="section">
            <div className={`section-header ${chatsOpen ? "open" : ""}`} onClick={() => setChatsOpen(!chatsOpen)}>
              <span>Все чаты</span>
              <span className="arrow">▶</span>
            </div>
            <div className={`section-list ${chatsOpen ? "" : "collapsed"}`}>
              {recent7.length > 0 && (
                <>
                  <div style={{ padding: "6px 12px", fontSize: 11, color: "var(--text-section)", fontWeight: 500 }}>Предыдущие 7 дней</div>
                  {recent7.slice(0, 10).map((s) => (
                    <div key={s.id} className={`chat-item ${s.id === currentId ? "active" : ""}`} onClick={() => setCurrentId(s.id)}>
                      <span className="title">{s.title}</span>
                      <button className="delete-btn" onClick={(e) => { e.stopPropagation(); deleteSession(s.id); }}>✕</button>
                    </div>
                  ))}
                </>
              )}
              {recent30.length > 0 && (
                <>
                  <div style={{ padding: "6px 12px", fontSize: 11, color: "var(--text-section)", fontWeight: 500 }}>Предыдущие 30 дней</div>
                  {recent30.slice(0, 10).map((s) => (
                    <div key={s.id} className={`chat-item ${s.id === currentId ? "active" : ""}`} onClick={() => setCurrentId(s.id)}>
                      <span className="title">{s.title}</span>
                      <button className="delete-btn" onClick={(e) => { e.stopPropagation(); deleteSession(s.id); }}>✕</button>
                    </div>
                  ))}
                </>
              )}
            </div>
          </div>
        </div>

        <div className="sidebar-footer">
          <button className="theme-btn" onClick={() => setDarkMode(!darkMode)}>
            {darkMode ? (
              <svg width="16" height="16" viewBox="0 0 16 16" fill="none"><circle cx="8" cy="8" r="4" stroke="currentColor" strokeWidth="1.5"/><path d="M8 2v1M8 13v1M2 8h1M13 8h1M3.76 3.76l.7.7M11.54 11.54l.7.7M3.76 12.24l.7-.7M11.54 4.46l.7-.7" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"/></svg>
            ) : (
              <svg width="16" height="16" viewBox="0 0 16 16" fill="none"><path d="M13 9.5a5 5 0 01-6.5-6.5A5.5 5.5 0 1013 9.5z" stroke="currentColor" strokeWidth="1.5"/></svg>
            )}
            {darkMode ? "Светлая тема" : "Тёмная тема"}
          </button>
        </div>
      </aside>

      {/* ═══ MAIN ═══ */}
      <main className="main-area">
        <header className="chat-header">
          {!sidebarOpen && (
            <button className="menu-btn" onClick={() => setSidebarOpen(true)}>
              <svg width="20" height="20" viewBox="0 0 20 20" fill="none"><path d="M3 5h14M3 10h14M3 15h14" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"/></svg>
            </button>
          )}
          <button className="model-selector">
            Qwen3.6-Plus
            <span className="arrow">▾</span>
          </button>
          <div className="header-spacer" />
          <button className="settings-btn">
            <svg width="18" height="18" viewBox="0 0 18 18" fill="none"><circle cx="9" cy="9" r="2.5" stroke="currentColor" strokeWidth="1.5"/><path d="M9 1.5v2M9 14.5v2M1.5 9h2M14.5 9h2M3.7 3.7l1.4 1.4M12.9 12.9l1.4 1.4M3.7 14.3l1.4-1.4M12.9 5.1l1.4-1.4" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"/></svg>
          </button>
        </header>

        {messages.length === 0 ? (
          <div className="empty-state">
            <h1>Готовы начать?</h1>
            <div className="input-area" style={{ width: "100%", maxWidth: 700 }}>
              <div className="input-wrapper">
                {dropdownOpen && (
                  <div className="dropdown-overlay" ref={dropdownRef}>
                    <div className="dropdown-menu">
                      <button className="dropdown-item" onClick={() => { document.getElementById("file-upload")?.click(); setDropdownOpen(false); }}>
                        <span className="di-icon">
                          <svg width="20" height="20" viewBox="0 0 20 20" fill="none"><path d="M10 3v14M3 10h14" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"/></svg>
                        </span>
                        <div><div className="di-text">Загрузить вложение</div><div className="di-desc">Файл, изображение, видео, аудио</div></div>
                      </button>
                      <div className="dropdown-divider" />
                      {SUGGESTIONS.map((s, i) => (
                        <button key={i} className="dropdown-item" onClick={() => handleSuggestion(s.text)}>
                          <span className="di-icon">{s.icon}</span>
                          <span className="di-text">{s.text}</span>
                        </button>
                      ))}
                    </div>
                  </div>
                )}
                <input type="file" id="file-upload" style={{ display: "none" }} />
                <div className="input-box">
                  <button className="action-btn" onClick={() => setDropdownOpen(!dropdownOpen)} title="Вложения">
                    <svg width="18" height="18" viewBox="0 0 18 18" fill="none"><path d="M9 3v12M3 9h12" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"/></svg>
                  </button>
                  <textarea
                    ref={textareaRef}
                    value={input}
                    onChange={(e) => setInput(e.target.value)}
                    onKeyDown={handleKeyDown}
                    onFocus={() => setDropdownOpen(true)}
                    placeholder="Чем я могу помочь вам сегодня?"
                    rows={1}
                    disabled={loading}
                  />
                  <div className="input-actions">
                    <button className="model-select-inline">Автоматический ▾</button>
                    <button className="action-btn" title="Голосовой ввод">
                      <svg width="18" height="18" viewBox="0 0 18 18" fill="none"><rect x="6" y="2" width="6" height="9" rx="3" stroke="currentColor" strokeWidth="1.5"/><path d="M4 9a5 5 0 0010 0" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"/><path d="M9 14v2" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"/></svg>
                    </button>
                    <button className="send-btn" onClick={() => sendMessage()} disabled={!input.trim() || loading}>
                      {loading ? (
                        <svg width="16" height="16" viewBox="0 0 16 16" fill="none"><circle cx="8" cy="8" r="6" stroke="white" strokeWidth="2" strokeDasharray="30" strokeDashoffset="10"><animateTransform attributeName="transform" type="rotate" from="0 8 8" to="360 8 8" dur="1s" repeatCount="indefinite"/></circle></svg>
                      ) : (
                        <svg width="16" height="16" viewBox="0 0 16 16" fill="none"><path d="M3 8h10M9 4l4 4-4 4" stroke="white" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"/></svg>
                      )}
                    </button>
                  </div>
                </div>
              </div>
            </div>
          </div>
        ) : (
          <>
            <div className="messages-area">
              <div className="messages-inner">
                {messages.map((msg) => (
                  <div key={msg.id} className={`message ${msg.role}`}>
                    <div className="msg-avatar">{msg.role === "user" ? "👤" : "🐦"}</div>
                    <div className="msg-body">
                      <div className="msg-bubble">
                        <MarkdownContent content={msg.content} isUser={msg.role === "user"} />
                      </div>
                      <div className="msg-actions">
                        <button className="msg-action-btn" onClick={() => copyMessage(msg.content)}>
                          <svg width="14" height="14" viewBox="0 0 14 14" fill="none"><rect x="4" y="4" width="8" height="8" rx="1.5" stroke="currentColor" strokeWidth="1.2"/><path d="M10 4V3a1.5 1.5 0 00-1.5-1.5h-6A1.5 1.5 0 001 3v6A1.5 1.5 0 002.5 10H4" stroke="currentColor" strokeWidth="1.2"/></svg>
                          Копировать
                        </button>
                      </div>
                      <div className="msg-time">{msg.time}</div>
                    </div>
                  </div>
                ))}
                {loading && (
                  <div className="message assistant">
                    <div className="msg-avatar">🐦</div>
                    <div className="msg-body">
                      <div className="msg-bubble">
                        <div className="thinking"><span /><span /><span /></div>
                      </div>
                      <div className="msg-time">Думаю...</div>
                    </div>
                  </div>
                )}
                {error && !loading && (
                  <div className="message assistant">
                    <div className="msg-avatar">⚠️</div>
                    <div className="msg-body">
                      <div className="msg-bubble error-bubble">
                        <p>Произошла ошибка при обработке запроса.</p>
                        <button className="retry-btn" onClick={() => { setError(null); sendMessage(session.messages[session.messages.length - 1]?.content); }}>
                          <svg width="14" height="14" viewBox="0 0 14 14" fill="none"><path d="M1 1v5h5M13 13V8H8" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"/><path d="M12 7A5 5 0 003.6 3.6L1 6M2 7a5 5 0 008.4 3.4L14 8" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"/></svg>
                          Повторить
                        </button>
                      </div>
                    </div>
                  </div>
                )}
                <div ref={messagesEndRef} />
              </div>
            </div>

            <div className="input-area">
              <div className="input-wrapper">
                <div className="input-box">
                  <button className="action-btn" onClick={() => setDropdownOpen(!dropdownOpen)} title="Вложения">
                    <svg width="18" height="18" viewBox="0 0 18 18" fill="none"><path d="M9 3v12M3 9h12" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"/></svg>
                  </button>
                  <textarea
                    ref={textareaRef}
                    value={input}
                    onChange={(e) => setInput(e.target.value)}
                    onKeyDown={handleKeyDown}
                    placeholder="Напишите сообщение..."
                    rows={1}
                    disabled={loading}
                  />
                  <div className="input-actions">
                    <button className="send-btn" onClick={() => sendMessage()} disabled={!input.trim() || loading}>
                      {loading ? "⏳" : "➤"}
                    </button>
                  </div>
                </div>
              </div>
            </div>
          </>
        )}
      </main>
    </div>
  );
}

export default App;
