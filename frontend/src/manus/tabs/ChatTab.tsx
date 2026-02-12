/**
 * ChatTab.tsx
 *
 * Grok-like chat canvas with floating composer.
 */

import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { ArrowRight, Bot, Ellipsis, Loader2, Mic, Sparkles, Volume2 } from 'lucide-react';
import type { ChatHistoryItem } from '../ManusLayout';

interface Message {
  id: string;
  role: 'user' | 'assistant';
  content: string;
  timestamp: Date;
}

interface AIResponse {
  response: string;
  conversation_id: string;
}

interface ChatTabProps {
  resetToken?: number;
  activeChatId?: string;
  onChatActivity?: (item: ChatHistoryItem) => void;
}

const API = '/api';
const CHAT_SESSIONS_KEY = 'kolibri-chat-sessions-v1';
const STARTER_PROMPTS = [
  'Покажи статус системы и доступные модули.',
  'Сделай краткий аудит backend и frontend.',
  'Обучи модель на https://en.wikipedia.org/wiki/Neural_network',
];

async function sendAIMessage(message: string, conversationId: string | null): Promise<AIResponse> {
  const urlMatch = message.match(/https?:\/\/\S+/i);
  if (urlMatch && (message.toLowerCase().includes('обучи') || message.toLowerCase().includes('обход'))) {
    const url = urlMatch[0];
    const pagesMatch = message.match(/(\d+)\s*страниц/);
    const maxPages = pagesMatch ? parseInt(pagesMatch[1], 10) : 5;

    const crawlResp = await fetch(`${API}/v1/crawl`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        url,
        mode: 'crawl',
        depth: 1,
        max_pages: maxPages,
        delay: 0.3,
      }),
    });

    const crawlData = await crawlResp.json();
    if (crawlData.status !== 'ok') {
      throw new Error(crawlData.detail || 'Ошибка web-обучения');
    }

    await fetch(`${API}/v1/ai/reload`, { method: 'POST' });

    return {
      response:
        `✅ Обучение завершено.\n` +
        `Источник: ${url}\n` +
        `Страниц: ${crawlData.pages_crawled ?? 0}, токенов: ${(crawlData.tokens ?? 0).toLocaleString()}`,
      conversation_id: conversationId || '',
    };
  }

  const response = await fetch(`${API}/v1/ai/chat`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      message,
      conversation_id: conversationId,
      temperature: 0.65,
    }),
  });

  if (!response.ok) {
    throw new Error(`AI endpoint error (${response.status})`);
  }

  const payload = await response.json();
  return {
    response: payload.response ?? '',
    conversation_id: payload.conversation_id ?? conversationId ?? '',
  };
}

export const ChatTab = ({ resetToken = 0, activeChatId, onChatActivity }: ChatTabProps) => {
  const [messages, setMessages] = useState<Message[]>([]);
  const [input, setInput] = useState('');
  const [conversationId, setConversationId] = useState<string | null>(null);
  const [isProcessing, setIsProcessing] = useState(false);
  const [micPulse, setMicPulse] = useState(false);
  const micTimerRef = useRef<number | null>(null);
  const endRef = useRef<HTMLDivElement>(null);
  const localSessionIdRef = useRef<string | null>(null);

  const loadSession = useCallback((sessionId: string) => {
    try {
      const raw = localStorage.getItem(CHAT_SESSIONS_KEY);
      if (!raw) return null;
      const parsed = JSON.parse(raw) as Record<
        string,
        {
          messages: Array<{ id: string; role: 'user' | 'assistant'; content: string; timestamp: string }>;
          conversationId: string | null;
        }
      >;
      const found = parsed[sessionId];
      if (!found) return null;
      return {
        conversationId: found.conversationId || null,
        messages: (found.messages || []).map((m) => ({
          id: m.id,
          role: m.role,
          content: m.content,
          timestamp: new Date(m.timestamp),
        })),
      };
    } catch {
      return null;
    }
  }, []);

  const persistSession = useCallback((sessionId: string, nextMessages: Message[], nextConversationId: string | null) => {
    try {
      const raw = localStorage.getItem(CHAT_SESSIONS_KEY);
      const parsed = raw
        ? (JSON.parse(raw) as Record<
            string,
            {
              messages: Array<{ id: string; role: 'user' | 'assistant'; content: string; timestamp: string }>;
              conversationId: string | null;
            }
          >)
        : {};
      parsed[sessionId] = {
        conversationId: nextConversationId,
        messages: nextMessages.slice(-120).map((m) => ({
          id: m.id,
          role: m.role,
          content: m.content,
          timestamp: m.timestamp.toISOString(),
        })),
      };
      localStorage.setItem(CHAT_SESSIONS_KEY, JSON.stringify(parsed));
    } catch {
      // ignore storage errors
    }
  }, []);

  const pushHistory = useCallback((title: string, preview: string) => {
    const id = localSessionIdRef.current || `chat-${Date.now()}`;
    localSessionIdRef.current = id;
    onChatActivity?.({
      id,
      title: title.trim().slice(0, 42) || 'Новый чат',
      preview: preview.trim().slice(0, 90),
      updatedAt: Date.now(),
      unread: false,
    });
  }, [onChatActivity]);

  useEffect(() => {
    endRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [messages, isProcessing]);

  useEffect(() => {
    return () => {
      if (micTimerRef.current != null) {
        window.clearTimeout(micTimerRef.current);
      }
    };
  }, []);

  useEffect(() => {
    setMessages([]);
    setInput('');
    setConversationId(null);
    localSessionIdRef.current = activeChatId || null;
  }, [resetToken]);

  useEffect(() => {
    if (!activeChatId) {
      localSessionIdRef.current = null;
      setMessages([]);
      setConversationId(null);
      return;
    }
    localSessionIdRef.current = activeChatId;
    const session = loadSession(activeChatId);
    if (session) {
      setMessages(session.messages);
      setConversationId(session.conversationId);
    } else {
      setMessages([]);
      setConversationId(null);
    }
  }, [activeChatId, loadSession]);

  useEffect(() => {
    const sid = localSessionIdRef.current;
    if (!sid) return;
    persistSession(sid, messages, conversationId);
  }, [messages, conversationId, persistSession]);

  const canSend = useMemo(() => input.trim().length > 3 && !isProcessing, [input, isProcessing]);

  const triggerMicPulse = useCallback(() => {
    setMicPulse(true);
    if (micTimerRef.current != null) {
      window.clearTimeout(micTimerRef.current);
    }
    micTimerRef.current = window.setTimeout(() => {
      setMicPulse(false);
      micTimerRef.current = null;
    }, 900);
  }, []);

  const handleSend = useCallback(async () => {
    const text = input.trim();
    if (!text || isProcessing) {
      return;
    }

    setMessages((prev) => [
      ...prev,
      {
        id: `${Date.now()}-user`,
        role: 'user',
        content: text,
        timestamp: new Date(),
      },
    ]);
    pushHistory(text, text);
    setInput('');
    setIsProcessing(true);

    try {
      const result = await sendAIMessage(text, conversationId);
      if (result.conversation_id && !conversationId) {
        setConversationId(result.conversation_id);
      }

      setMessages((prev) => [
        ...prev,
        {
          id: `${Date.now()}-assistant`,
          role: 'assistant',
          content: result.response || 'Я не смог сформировать ответ.',
          timestamp: new Date(),
        },
      ]);
      pushHistory(text, result.response || text);
    } catch (error) {
      setMessages((prev) => [
        ...prev,
        {
          id: `${Date.now()}-error`,
          role: 'assistant',
          content: `Ошибка: ${error instanceof Error ? error.message : String(error)}`,
          timestamp: new Date(),
        },
      ]);
      pushHistory(text, `Ошибка: ${error instanceof Error ? error.message : String(error)}`);
    } finally {
      setIsProcessing(false);
    }
  }, [conversationId, input, isProcessing, pushHistory]);

  const handleInputKeyDown = (event: React.KeyboardEvent<HTMLInputElement>) => {
    if (event.key === 'Enter') {
      event.preventDefault();
      handleSend();
    }
  };

  return (
    <div className="grok-chat-root">
      <header className="grok-chat-head">
        <div className="grok-chat-title">Kolibri AI · Чат</div>
        <div className="grok-chat-head-actions">
          <button type="button" className="head-icon" aria-label="Меню">
            <Ellipsis size={16} />
          </button>
          <button type="button" className="share-btn">
            Поделиться
          </button>
        </div>
      </header>
      <div className="grok-status-line">Ara слушает...</div>

      <div className="grok-chat-scroll" role="log" aria-live="polite">
        <div className="grok-thread">
          {messages.length === 0 && !isProcessing && (
            <div className="assistant-intro">
              <p>Добро пожаловать в Kolibri AI Beta.</p>
              <p>Доступны: чат, AI агент, архиватор, терминал, знания.</p>
              <div className="starter-prompts">
                {STARTER_PROMPTS.map((prompt) => (
                  <button key={prompt} type="button" className="starter-prompt" onClick={() => setInput(prompt)}>
                    {prompt}
                  </button>
                ))}
              </div>
            </div>
          )}

          {messages.map((message) => (
            <div key={message.id} className={`thread-row ${message.role}`}>
              {message.role === 'assistant' && (
                <div className="thread-avatar">
                  <Bot size={14} />
                </div>
              )}
              <div className={`thread-bubble ${message.role}`}>{message.content}</div>
            </div>
          ))}

          {isProcessing && (
            <div className="thread-row assistant">
              <div className="thread-avatar">
                <Bot size={14} />
              </div>
              <div className="thread-bubble assistant loading">
                <Loader2 size={14} className="spin" />
                <span>Ara формирует ответ...</span>
              </div>
            </div>
          )}

          <div ref={endRef} />
        </div>
      </div>

      <div className="composer-wrap">
        <div className="composer-card">
          <input
            type="text"
            className="composer-input"
            value={input}
            onChange={(event) => setInput(event.target.value)}
            onKeyDown={handleInputKeyDown}
            placeholder="Как Ara может помочь?"
            disabled={isProcessing}
          />
          <div className="composer-controls">
            <button type="button" className="chip icon" aria-label="Режимы">
              <Sparkles size={15} />
            </button>
            <button type="button" className={`chip icon ${micPulse ? 'pulse' : ''}`} onClick={triggerMicPulse} aria-label="Микрофон">
              <Mic size={15} />
            </button>
            <button type="button" className="chip icon" aria-label="Аудио">
              <Volume2 size={15} />
            </button>
            <button type="button" className="chip assistant-chip">
              Ara · Assistant
            </button>
            <button
              type="button"
              className={`send-btn ${canSend ? 'active' : ''}`}
              onClick={canSend ? handleSend : triggerMicPulse}
              aria-label={canSend ? 'Отправить' : 'Голос'}
            >
              {canSend ? <ArrowRight size={16} /> : <Mic size={16} />}
            </button>
          </div>
        </div>
        {conversationId && <div className="conversation-id">Диалог: {conversationId.slice(0, 8)}</div>}
      </div>

      <style>{`
        .grok-chat-root {
          position: relative;
          display: flex;
          flex-direction: column;
          width: 100%;
          height: 100%;
          background: var(--bg-primary);
          color: var(--text-primary);
          overflow: hidden;
        }

        .grok-chat-head {
          height: 50px;
          min-height: 50px;
          border-bottom: 1px solid var(--border-primary);
          display: flex;
          align-items: center;
          justify-content: space-between;
          gap: 12px;
          padding: 0 18px;
        }

        .grok-chat-title {
          color: var(--text-primary);
          font-size: 14px;
          font-weight: 600;
          white-space: nowrap;
          overflow: hidden;
          text-overflow: ellipsis;
        }

        .grok-status-line {
          min-height: 26px;
          display: inline-flex;
          align-items: center;
          padding: 0 18px;
          font-size: 12px;
          font-weight: 500;
          color: var(--accent-primary);
          opacity: 0.8;
          border-bottom: 1px solid var(--border-primary);
        }

        .grok-chat-head-actions {
          display: flex;
          align-items: center;
          gap: 8px;
        }

        .head-icon {
          width: 34px;
          height: 34px;
          border-radius: 999px;
          border: 1px solid var(--border-primary);
          background: var(--bg-overlay);
          color: var(--text-muted);
          display: inline-flex;
          align-items: center;
          justify-content: center;
          cursor: pointer;
        }

        .head-icon:hover {
          border-color: var(--border-hover);
          color: var(--text-primary);
        }

        .share-btn {
          min-height: 34px;
          border: 1px solid var(--border-primary);
          border-radius: 999px;
          background: var(--bg-overlay);
          color: var(--text-secondary);
          font-size: 13px;
          font-weight: 500;
          padding: 0 14px;
          cursor: pointer;
        }

        .share-btn:hover {
          border-color: var(--border-hover);
          color: var(--text-primary);
        }

        .grok-chat-scroll {
          flex: 1;
          overflow-y: auto;
          padding: 20px 24px 182px;
        }

        .grok-thread {
          max-width: 760px;
          margin: 0 auto;
          display: flex;
          flex-direction: column;
          gap: 14px;
        }

        .assistant-intro {
          color: var(--text-secondary);
          line-height: 1.5;
          font-size: 15px;
          display: flex;
          flex-direction: column;
          gap: 8px;
          margin-top: 8px;
        }

        .assistant-intro p {
          margin: 0;
        }

        .starter-prompts {
          display: flex;
          flex-wrap: wrap;
          gap: 8px;
          margin-top: 6px;
        }

        .starter-prompt {
          border: 1px solid var(--border-primary);
          background: var(--bg-overlay);
          color: var(--text-secondary);
          border-radius: 999px;
          padding: 8px 12px;
          font-size: 13px;
          cursor: pointer;
        }

        .starter-prompt:hover {
          background: var(--bg-hover);
          color: var(--text-primary);
          border-color: var(--border-hover);
        }

        .thread-row {
          display: flex;
          align-items: flex-start;
          gap: 8px;
          width: 100%;
        }

        .thread-row.user {
          justify-content: flex-end;
        }

        .thread-avatar {
          width: 24px;
          height: 24px;
          border-radius: 999px;
          display: inline-flex;
          align-items: center;
          justify-content: center;
          color: var(--accent-primary);
          background: var(--accent-bg);
          border: 1px solid var(--border-accent);
          flex-shrink: 0;
          margin-top: 2px;
        }

        .thread-bubble {
          max-width: min(76%, 720px);
          font-size: 15px;
          line-height: 1.5;
          white-space: pre-wrap;
          word-break: break-word;
          border: 1px solid var(--border-primary);
          padding: 10px 13px;
        }

        .thread-bubble.user {
          background: var(--bg-tertiary);
          color: var(--text-primary);
          border-radius: 14px 6px 6px 14px;
        }

        .thread-bubble.assistant {
          background: var(--bg-card);
          color: var(--text-secondary);
          border-radius: 6px 14px 14px 6px;
        }

        .thread-bubble.loading {
          display: inline-flex;
          align-items: center;
          gap: 8px;
          color: var(--text-secondary);
        }

        .spin {
          animation: spin 1s linear infinite;
        }

        @keyframes spin {
          to {
            transform: rotate(360deg);
          }
        }

        .composer-wrap {
          position: absolute;
          left: 50%;
          transform: translateX(-50%);
          bottom: 18px;
          width: min(760px, calc(100% - 44px));
          z-index: 3;
        }

        .composer-card {
          border-radius: 12px;
          border: 1px solid var(--border-primary);
          background: var(--bg-overlay);
          backdrop-filter: blur(8px);
          padding: 10px;
        }

        .composer-input {
          width: 100%;
          border: none;
          background: transparent;
          color: var(--text-primary);
          font-size: 15px;
          outline: none;
          padding: 0 6px;
          min-height: 40px;
        }

        .composer-input::placeholder {
          color: var(--text-muted);
        }

        .composer-controls {
          display: flex;
          align-items: center;
          gap: 8px;
          margin-top: 10px;
        }

        .chip {
          border: 1px solid var(--border-primary);
          background: var(--bg-input);
          color: var(--text-secondary);
          border-radius: 999px;
          min-height: 34px;
          padding: 0 12px;
          display: inline-flex;
          align-items: center;
          justify-content: center;
          cursor: pointer;
          font-size: 13px;
        }

        .chip.icon {
          width: 34px;
          min-width: 34px;
          padding: 0;
        }

        .chip.pulse {
          animation: pulse 0.9s ease-out;
        }

        @keyframes pulse {
          0% {
            transform: scale(1);
          }
          45% {
            transform: scale(1.08);
          }
          100% {
            transform: scale(1);
          }
        }

        .assistant-chip {
          margin-left: 2px;
          color: var(--text-secondary);
        }

        .send-btn {
          margin-left: auto;
          width: 36px;
          height: 36px;
          border-radius: 8px;
          border: 1px solid var(--border-primary);
          background: var(--bg-input);
          color: var(--text-muted);
          display: inline-flex;
          align-items: center;
          justify-content: center;
          cursor: pointer;
        }

        .send-btn.active {
          border-color: var(--border-accent);
          background: var(--accent-bg);
          color: var(--accent-primary);
        }

        .conversation-id {
          color: var(--text-dimmed);
          font-size: 11px;
          margin-top: 6px;
          text-align: right;
          padding: 0 4px;
        }

        @media (max-width: 760px) {
          .grok-chat-head {
            padding: 0 12px;
          }

          .grok-chat-title {
            font-size: 14px;
          }

          .grok-chat-scroll {
            padding: 14px 12px 174px;
          }

          .thread-bubble {
            max-width: 92%;
            font-size: 14px;
          }

          .composer-wrap {
            width: calc(100% - 20px);
            bottom: 10px;
          }

          .composer-card {
            border-radius: 16px;
          }

          .composer-input {
            font-size: 16px;
          }

          .share-btn {
            display: none;
          }
        }
      `}</style>
    </div>
  );
};
