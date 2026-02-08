/**
 * ChatTab.tsx — Числовое Формульное Мышление Kolibri
 * 
 * Показывает:
 * - Числовые паттерны слов (64 цифры)
 * - Формулы (геном, fitness, predict)
 * - Граф знаний (паттерны, рёбра)
 * - Децентрализованный рой (узлы)
 */

import { useState, useCallback, useRef, useEffect } from 'react';
import {
  Send, Sparkles, User, Bot, Loader2,
  Brain, Zap, BookOpen, BarChart3, RefreshCw,
  Hash, Network, Binary, Dna
} from 'lucide-react';

/* ---------- Типы ---------- */
interface Message {
  id: string;
  role: 'user' | 'assistant';
  content: string;
  timestamp: Date;
  confidence?: number;
  method?: string;
  knowledgeHits?: number;
  formulaData?: FormulaData;
  graphStats?: GraphStats;
  durationMs?: number;
}

interface RetrievedSentence {
  text: string;
  score: number;
}

interface FormulaWord {
  word: string;
  score: number;
}

interface FormulaData {
  query_patterns?: Record<string, string>;
  query_hashes?: Record<string, number>;
  answer_patterns?: Record<string, string>;
  formula_predict?: number;
  formula_genome_hex?: string;
  formula_fitness?: number;
  formula_generation?: number;
  graph_score?: number;
  graph_candidates?: number;
  retrieved_sentences?: RetrievedSentence[];
  formula_generated_words?: FormulaWord[];
  sentence_store_size?: number;
}

interface GraphStats {
  patterns?: number;
  edges?: number;
  documents_trained?: number;
  tokens_processed?: number;
  avg_fitness?: number;
  avg_weight?: number;
}

interface AIResponse {
  response: string;
  confidence: number;
  conversation_id: string;
  sources: string[];
  knowledge_hits: number;
  method: string;
  duration_ms: number;
  model_available: boolean;
  formula_data?: FormulaData;
  graph_stats?: GraphStats;
}

interface EngineStats {
  model_available: boolean;
  graph_patterns: number;
  graph_edges: number;
  graph_documents: number;
  graph_tokens: number;
  graph_avg_fitness: number;
  graph_avg_weight: number;
  formula_generation: number;
  formula_fitness: number;
  formula_genome_hex: string;
  c_model_patterns: number;
  c_model_edges: number;
  c_model_size_mb: number;
  c_model_documents: number;
  c_model_epoch: number;
  c_model_avg_fitness: number;
  c_model_avg_weight: number;
  active_conversations: number;
  sentence_store_size?: number;
}

/* ---------- Константы ---------- */
const SUGGESTIONS = [
  { icon: '🔢', text: 'паттерн нейронная сеть' },
  { icon: '⚡', text: 'Покажи формулу' },
  { icon: '📊', text: 'Покажи статистику модели' },
  { icon: '🧠', text: 'Что ты знаешь об искусственном интеллекте?' },
  { icon: '🌐', text: 'Обучи модель на https://en.wikipedia.org/wiki/Neural_network' },
  { icon: '🔬', text: 'паттерн kolibri' },
];

const API = '/api';

/* ---------- Методы → бейджи ---------- */
const METHOD_BADGES: Record<string, { icon: string; label: string; color: string }> = {
  'knowledge-graph':    { icon: '🕸️', label: 'Граф знаний',      color: '#22c55e' },
  'graph+c-model':      { icon: '🔢', label: 'Граф + C-модель',  color: '#10b981' },
  'formula-association': { icon: '⚡', label: 'Формула (точная)', color: '#f59e0b' },
  'formula-predict':    { icon: '🧮', label: 'Формула predict',  color: '#f97316' },
  'formula-retrieval':  { icon: '🧬', label: 'Формула → ответ',  color: '#22d3ee' },
  'formula-generation': { icon: '🔮', label: 'Формула генерация', color: '#c084fc' },
  'greeting':           { icon: '👋', label: 'Приветствие',       color: '#a78bfa' },
  'no-knowledge':       { icon: '❓', label: 'Нет данных',        color: '#71717a' },
  'c-model':            { icon: '💾', label: 'C-модель (.klm)',  color: '#6366f1' },
  'pattern-lookup':     { icon: '🔢', label: 'Числовой паттерн', color: '#8b5cf6' },
  'formula-inspect':    { icon: '🧬', label: 'Формула (осмотр)', color: '#a855f7' },
  'command':            { icon: '⚡', label: 'Команда',           color: '#6366f1' },
  'crawl':              { icon: '🌐', label: 'Web-обучение',     color: '#3b82f6' },
  'unknown':            { icon: '❔', label: 'Без метода',        color: '#52525b' },
};

/* ---------- Markdown-рендер ---------- */
function renderMd(text: string): string {
  return text
    .replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>')
    .replace(/_(.+?)_/g, '<em>$1</em>')
    .replace(/`(.+?)`/g, '<code class="num-code">$1</code>')
    .replace(/\n/g, '<br/>');
}

/* ---------- API ---------- */
async function sendAIMessage(
  message: string,
  conversationId: string | null,
  temperature: number = 0.7,
): Promise<AIResponse> {
  const urlMatch = message.match(/https?:\/\/\S+/i);
  if (urlMatch && (message.toLowerCase().includes('обучи') || message.toLowerCase().includes('обход'))) {
    const url = urlMatch[0];
    const pagesMatch = message.match(/(\d+)\s*страниц/);
    const maxPages = pagesMatch ? parseInt(pagesMatch[1]) : 5;
    const wantsCrawl = message.toLowerCase().includes('страниц') || message.toLowerCase().includes('обход');

    try {
      const crawlResp = await fetch(`${API}/v1/crawl`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          url,
          mode: wantsCrawl ? 'crawl' : 'url',
          depth: 1,
          max_pages: maxPages,
          delay: 0.3,
        }),
      });
      const d = await crawlResp.json();
      if (d.status === 'ok') {
        await fetch(`${API}/v1/ai/reload`, { method: 'POST' });
        return {
          response:
            `✅ **Обучение завершено — знания сохранены в числах!**\n\n` +
            `🌐 Источник: ${url}\n` +
            `• Страниц: **${d.pages_crawled}** | Токенов: **${(d.tokens ?? 0).toLocaleString()}**\n` +
            `• Паттернов: **${(d.patterns ?? 0).toLocaleString()}** | Связей: **${(d.edges ?? 0).toLocaleString()}**\n` +
            `• Каждое слово → **64-цифровой числовой паттерн**\n` +
            `• Связи → **граф со-occurence с сигмоидными весами**\n\n` +
            `Числовой граф знаний обновлён. Задавайте вопросы!`,
          confidence: 1.0,
          conversation_id: conversationId || '',
          sources: ['web-crawl'],
          knowledge_hits: d.pages_crawled ?? 0,
          method: 'crawl',
          duration_ms: (d.time_sec ?? 0) * 1000,
          model_available: true,
        };
      }
      throw new Error(d.detail || 'Ошибка обучения');
    } catch (e) {
      return {
        response: `❌ Ошибка обучения: ${e instanceof Error ? e.message : e}`,
        confidence: 0, conversation_id: conversationId || '', sources: [],
        knowledge_hits: 0, method: 'crawl', duration_ms: 0, model_available: false,
      };
    }
  }

  const resp = await fetch(`${API}/v1/ai/chat`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ message, conversation_id: conversationId, temperature }),
  });

  if (!resp.ok) {
    const detail = await resp.text();
    throw new Error(`AI endpoint error (${resp.status}): ${detail}`);
  }
  return resp.json();
}

/* ---------- Числовые компоненты ---------- */

const PatternDisplay = ({ patterns, title }: { patterns: Record<string, string>; title: string }) => {
  const entries = Object.entries(patterns).slice(0, 4);
  if (!entries.length) return null;
  return (
    <div className="numeric-panel">
      <div className="numeric-panel-title">
        <Hash size={12} /> {title}
      </div>
      {entries.map(([word, pat]) => (
        <div key={word} className="pattern-row">
          <span className="pattern-word">{word}</span>
          <span className="pattern-digits">{pat.slice(0, 28)}…</span>
        </div>
      ))}
    </div>
  );
};

const FormulaDisplay = ({ data }: { data: FormulaData }) => {
  if (!data.formula_genome_hex) return null;
  return (
    <div className="numeric-panel formula-panel">
      <div className="numeric-panel-title">
        <Dna size={12} /> Формула
      </div>
      <div className="formula-stats">
        <span>predict: <b>{data.formula_predict?.toFixed(2)}</b></span>
        <span>fitness: <b>{data.formula_fitness?.toFixed(4)}</b></span>
        <span>gen: <b>{data.formula_generation}</b></span>
      </div>
      <div className="genome-hex">{data.formula_genome_hex?.slice(0, 32)}…</div>
    </div>
  );
};

const FormulaWordsDisplay = ({ words }: { words: FormulaWord[] }) => {
  if (!words.length) return null;
  return (
    <div className="numeric-panel formula-words-panel">
      <div className="numeric-panel-title">
        <Sparkles size={12} /> Формула сгенерировала
      </div>
      <div className="formula-words-grid">
        {words.slice(0, 8).map((w, i) => (
          <div key={i} className="formula-word-chip">
            <span className="fw-word">{w.word}</span>
            <span className="fw-score">{w.score.toFixed(2)}</span>
          </div>
        ))}
      </div>
    </div>
  );
};

const RetrievedDisplay = ({ sentences }: { sentences: RetrievedSentence[] }) => {
  if (!sentences.length) return null;
  return (
    <div className="numeric-panel retrieved-panel">
      <div className="numeric-panel-title">
        <BookOpen size={12} /> Найдено в памяти
      </div>
      {sentences.slice(0, 3).map((s, i) => (
        <div key={i} className="retrieved-row">
          <span className="retrieved-text">{s.text.length > 120 ? s.text.slice(0, 120) + '…' : s.text}</span>
          <span className="retrieved-score">{(s.score * 100).toFixed(0)}%</span>
        </div>
      ))}
    </div>
  );
};

const GraphScoreBadge = ({ data }: { data: FormulaData }) => {
  if (!data.graph_candidates) return null;
  return (
    <span className="graph-score-badge">
      <Network size={10} /> {data.graph_candidates} связей, score {data.graph_score?.toFixed(2)}
    </span>
  );
};

const ConfidenceBadge = ({ confidence, method }: { confidence: number; method: string }) => {
  const pct = Math.round(confidence * 100);
  const badge = METHOD_BADGES[method] || METHOD_BADGES['unknown'];
  const barColor = confidence >= 0.7 ? '#22c55e' : confidence >= 0.4 ? '#f59e0b' : '#ef4444';
  return (
    <div className="confidence-badge">
      <span className="method-icon">{badge.icon}</span>
      <span className="method-label" style={{ color: badge.color }}>{badge.label}</span>
      <div className="confidence-bar-bg">
        <div className="confidence-bar-fill" style={{ width: `${pct}%`, background: barColor }} />
      </div>
      <span className="confidence-value">{pct}%</span>
    </div>
  );
};

/* ---------- Статус движка ---------- */
const EngineStatus = ({ stats }: { stats: EngineStats | null }) => {
  if (!stats) return null;
  return (
    <div className="engine-status">
      <div className="engine-status-item" title="Числовой граф">
        <Binary size={12} />
        <span>{stats.graph_patterns.toLocaleString()} пат.</span>
      </div>
      <div className="engine-status-item" title="Рёбра">
        <Network size={12} />
        <span>{stats.graph_edges.toLocaleString()} рёб.</span>
      </div>
      <div className="engine-status-item" title="Формула">
        <Dna size={12} />
        <span>gen {stats.formula_generation}</span>
      </div>
      {stats.sentence_store_size != null && stats.sentence_store_size > 0 && (
        <div className="engine-status-item" title="Предложения в памяти">
          <BookOpen size={12} />
          <span>{stats.sentence_store_size} пред.</span>
        </div>
      )}
      {stats.c_model_patterns > 0 && (
        <div className="engine-status-item" title={`C-модель: ${stats.c_model_patterns.toLocaleString()} пат., ${stats.c_model_edges.toLocaleString()} рёб., ${stats.c_model_size_mb} МБ`}>
          <BarChart3 size={12} />
          <span>{(stats.c_model_patterns / 1000).toFixed(0)}K C</span>
        </div>
      )}
    </div>
  );
};

/* ---------- Главный компонент ---------- */
export const ChatTab = () => {
  const [messages, setMessages] = useState<Message[]>([]);
  const [input, setInput] = useState('');
  const [isProcessing, setIsProcessing] = useState(false);
  const [conversationId, setConversationId] = useState<string | null>(null);
  const [engineStats, setEngineStats] = useState<EngineStats | null>(null);
  const endRef = useRef<HTMLDivElement>(null);

  const fetchStats = useCallback(async () => {
    try {
      const r = await fetch(`${API}/v1/ai/stats`);
      if (r.ok) setEngineStats(await r.json());
    } catch { /* ignore */ }
  }, []);

  useEffect(() => {
    fetchStats();
    const interval = setInterval(fetchStats, 30000);
    return () => clearInterval(interval);
  }, [fetchStats]);

  useEffect(() => {
    endRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [messages]);

  const handleSend = useCallback(async () => {
    if (!input.trim() || isProcessing) return;
    const text = input;
    setInput('');

    const userMsg: Message = {
      id: Date.now().toString(), role: 'user',
      content: text, timestamp: new Date(),
    };
    setMessages(prev => [...prev, userMsg]);
    setIsProcessing(true);

    try {
      const result = await sendAIMessage(text, conversationId);
      if (result.conversation_id && !conversationId) setConversationId(result.conversation_id);

      const assistantMsg: Message = {
        id: (Date.now() + 1).toString(), role: 'assistant',
        content: result.response, timestamp: new Date(),
        confidence: result.confidence, method: result.method,
        knowledgeHits: result.knowledge_hits,
        formulaData: result.formula_data, graphStats: result.graph_stats,
        durationMs: result.duration_ms,
      };
      setMessages(prev => [...prev, assistantMsg]);
      fetchStats();
    } catch (e) {
      setMessages(prev => [...prev, {
        id: (Date.now() + 1).toString(), role: 'assistant',
        content: `❌ Ошибка: ${e instanceof Error ? e.message : e}`,
        timestamp: new Date(), method: 'error', confidence: 0,
      }]);
    } finally {
      setIsProcessing(false);
    }
  }, [input, isProcessing, conversationId, fetchStats]);

  const handleNewChat = useCallback(() => {
    setMessages([]);
    setConversationId(null);
  }, []);

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); handleSend(); }
  };

  return (
    <div className="chat-tab">
      {/* Хедер */}
      <div className="chat-header">
        <div className="chat-header-left">
          <Brain size={20} className="chat-header-icon" />
          <span className="chat-header-title">Kolibri AI</span>
          <span className="chat-header-version">Числовое Формульное Мышление</span>
        </div>
        <div className="chat-header-right">
          <EngineStatus stats={engineStats} />
          <button className="chat-new-btn" onClick={handleNewChat} title="Новый диалог">
            <RefreshCw size={16} />
          </button>
        </div>
      </div>

      {/* Сообщения */}
      <div className="chat-messages">
        {messages.length === 0 ? (
          <div className="chat-welcome">
            <div className="welcome-icon"><Binary size={48} /></div>
            <h1 className="welcome-title">Числовое Мышление</h1>
            <p className="welcome-subtitle">
              Каждое слово = <strong>64-цифровой паттерн</strong>. Знания хранятся в <strong>числах</strong>.<br />
              Формулы из 1024 цифр определяют поведение AI. Всё — числа.
            </p>
            <div className="welcome-suggestions">
              {SUGGESTIONS.map((s, i) => (
                <button key={i} className="suggestion-card" onClick={() => setInput(s.text)}>
                  <span className="suggestion-icon">{s.icon}</span>
                  <span className="suggestion-text">{s.text}</span>
                </button>
              ))}
            </div>
            {engineStats && (
              <div className="welcome-engine-info">
                <Hash size={14} />
                <span>
                  {engineStats.graph_patterns > 0
                    ? `Граф: ${engineStats.graph_patterns.toLocaleString()} паттернов, ${engineStats.graph_edges.toLocaleString()} рёбер | Формула gen ${engineStats.formula_generation}`
                    : 'Граф пуст. Обучите через URL.'}
                  {engineStats.c_model_patterns > 0
                    ? ` | C-модель: ${engineStats.c_model_patterns.toLocaleString()} пат., ${engineStats.c_model_size_mb} МБ`
                    : ''}
                </span>
              </div>
            )}
          </div>
        ) : (
          <>
            {messages.map(msg => (
              <div key={msg.id} className={`chat-message ${msg.role}`}>
                <div className="message-avatar">
                  {msg.role === 'user' ? <User size={18} /> : <Bot size={18} />}
                </div>
                <div className="message-content">
                  <div className="message-header">
                    <span className="message-role">{msg.role === 'user' ? 'Вы' : 'Kolibri AI'}</span>
                    <span className="message-time">
                      {msg.timestamp.toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit' })}
                    </span>
                    {msg.durationMs != null && msg.durationMs > 0 && (
                      <span className="message-duration">{msg.durationMs}мс</span>
                    )}
                  </div>
                  <div className="message-text" dangerouslySetInnerHTML={{ __html: renderMd(msg.content) }} />

                  {/* === ЧИСЛОВЫЕ ДАННЫЕ === */}
                  {msg.role === 'assistant' && msg.formulaData && (
                    <div className="numeric-data-section">
                      {/* Формульные слова — главный результат генерации */}
                      {msg.formulaData.formula_generated_words && msg.formulaData.formula_generated_words.length > 0 && (
                        <FormulaWordsDisplay words={msg.formulaData.formula_generated_words} />
                      )}
                      {/* Retrieved предложения */}
                      {msg.formulaData.retrieved_sentences && msg.formulaData.retrieved_sentences.length > 0 && (
                        <RetrievedDisplay sentences={msg.formulaData.retrieved_sentences} />
                      )}
                      {/* Паттерны и формула */}
                      <PatternDisplay patterns={msg.formulaData.query_patterns || {}} title="Паттерны запроса" />
                      <PatternDisplay patterns={msg.formulaData.answer_patterns || {}} title="Паттерны ответа" />
                      <FormulaDisplay data={msg.formulaData} />
                    </div>
                  )}

                  {/* Метаданные */}
                  {msg.role === 'assistant' && msg.method && msg.method !== 'error' && (
                    <div className="message-meta">
                      <ConfidenceBadge confidence={msg.confidence ?? 0} method={msg.method ?? 'unknown'} />
                      {msg.formulaData && <GraphScoreBadge data={msg.formulaData} />}
                    </div>
                  )}
                </div>
              </div>
            ))}
            {isProcessing && (
              <div className="chat-message assistant">
                <div className="message-avatar"><Bot size={18} /></div>
                <div className="message-content">
                  <div className="thinking-indicator">
                    <Loader2 className="thinking-spinner" size={16} />
                    <span>Числовое мышление...</span>
                  </div>
                </div>
              </div>
            )}
            <div ref={endRef} />
          </>
        )}
      </div>

      {/* Ввод */}
      <div className="chat-input-area">
        <div className="chat-input-container">
          <textarea
            className="chat-input"
            value={input}
            onChange={e => setInput(e.target.value)}
            onKeyDown={handleKeyDown}
            placeholder="Вопрос, «паттерн слово», «покажи формулу» или URL для обучения..."
            rows={1}
          />
          <button className="chat-send-btn" onClick={isProcessing ? undefined : handleSend}
            disabled={!input.trim() && !isProcessing}>
            <Send size={20} />
          </button>
        </div>
        <div className="chat-input-hint">
          Enter — отправить · Shift+Enter — новая строка
          {conversationId && <span className="hint-conv-id"> · Диалог: {conversationId.slice(0, 8)}</span>}
        </div>
      </div>

      <style>{`
        .chat-tab { display:flex; flex-direction:column; height:100%; overflow:hidden; }

        .chat-header { display:flex; align-items:center; justify-content:space-between; padding:12px 20px; border-bottom:1px solid rgba(255,255,255,0.06); background:rgba(24,24,27,0.6); }
        .chat-header-left { display:flex; align-items:center; gap:10px; }
        .chat-header-icon { color:#818cf8; }
        .chat-header-title { font-size:16px; font-weight:700; color:#fafafa; }
        .chat-header-version { font-size:11px; color:#818cf8; font-weight:500; }
        .chat-header-right { display:flex; align-items:center; gap:12px; }
        .chat-new-btn { width:32px; height:32px; border-radius:8px; background:rgba(255,255,255,0.05); border:1px solid rgba(255,255,255,0.08); color:#71717a; cursor:pointer; display:flex; align-items:center; justify-content:center; transition:all 0.15s; }
        .chat-new-btn:hover { background:rgba(99,102,241,0.15); color:#818cf8; border-color:rgba(99,102,241,0.3); }

        .engine-status { display:flex; gap:12px; }
        .engine-status-item { display:flex; align-items:center; gap:4px; font-size:11px; color:#52525b; }

        .chat-messages { flex:1; overflow-y:auto; padding:20px; }
        .chat-messages::-webkit-scrollbar { width:4px; }
        .chat-messages::-webkit-scrollbar-thumb { background:rgba(255,255,255,0.08); border-radius:2px; }

        .chat-welcome { display:flex; flex-direction:column; align-items:center; justify-content:center; height:100%; text-align:center; padding:40px; }
        .welcome-icon { width:80px; height:80px; border-radius:24px; background:linear-gradient(135deg,rgba(16,185,129,0.2),rgba(99,102,241,0.2)); display:flex; align-items:center; justify-content:center; color:#10b981; margin-bottom:24px; }
        .welcome-title { font-size:36px; font-weight:700; margin:0 0 12px; background:linear-gradient(135deg,#10b981,#818cf8); -webkit-background-clip:text; -webkit-text-fill-color:transparent; }
        .welcome-subtitle { font-size:15px; color:#71717a; max-width:500px; margin:0 0 32px; line-height:1.6; }
        .welcome-subtitle strong { color:#a5b4fc; }
        .welcome-suggestions { display:grid; grid-template-columns:repeat(3,1fr); gap:10px; max-width:600px; }
        .suggestion-card { display:flex; align-items:center; gap:10px; padding:14px 16px; background:rgba(39,39,42,0.5); border:1px solid rgba(255,255,255,0.06); border-radius:12px; cursor:pointer; transition:all 0.15s; text-align:left; }
        .suggestion-card:hover { background:rgba(63,63,70,0.5); border-color:rgba(16,185,129,0.3); transform:translateY(-2px); }
        .suggestion-icon { font-size:18px; flex-shrink:0; }
        .suggestion-text { font-size:12px; color:#d4d4d8; line-height:1.4; }
        .welcome-engine-info { display:flex; align-items:center; gap:8px; margin-top:24px; padding:10px 16px; background:rgba(16,185,129,0.08); border:1px solid rgba(16,185,129,0.15); border-radius:10px; color:#6ee7b7; font-size:12px; }

        .chat-message { display:flex; gap:12px; margin-bottom:20px; animation:fadeIn 0.3s ease; }
        @keyframes fadeIn { from{opacity:0;transform:translateY(10px)} to{opacity:1;transform:translateY(0)} }
        .message-avatar { width:36px; height:36px; border-radius:10px; display:flex; align-items:center; justify-content:center; flex-shrink:0; }
        .chat-message.user .message-avatar { background:linear-gradient(135deg,#3b82f6,#2563eb); color:white; }
        .chat-message.assistant .message-avatar { background:linear-gradient(135deg,#10b981,#059669); color:white; }
        .message-content { flex:1; min-width:0; }
        .message-header { display:flex; align-items:center; gap:8px; margin-bottom:6px; }
        .message-role { font-size:13px; font-weight:600; color:#fafafa; }
        .message-time { font-size:11px; color:#52525b; }
        .message-duration { font-size:10px; color:#3f3f46; background:rgba(255,255,255,0.04); padding:1px 6px; border-radius:4px; }
        .message-text { font-size:14px; line-height:1.7; color:#d4d4d8; }
        .message-text strong { color:#fafafa; font-weight:600; }
        .message-text .num-code { background:rgba(16,185,129,0.15); padding:2px 6px; border-radius:4px; font-size:12px; color:#6ee7b7; font-family:'JetBrains Mono',monospace; letter-spacing:0.5px; }
        .message-text em { color:#a1a1aa; }

        /* --- ЧИСЛОВЫЕ ПАНЕЛИ --- */
        .numeric-data-section { display:flex; flex-wrap:wrap; gap:8px; margin-top:10px; }
        .numeric-panel { background:rgba(16,185,129,0.06); border:1px solid rgba(16,185,129,0.12); border-radius:10px; padding:8px 12px; font-size:11px; min-width:200px; flex:1; }
        .formula-panel { background:rgba(99,102,241,0.06); border-color:rgba(99,102,241,0.12); }
        .numeric-panel-title { display:flex; align-items:center; gap:5px; font-weight:600; color:#6ee7b7; margin-bottom:6px; font-size:10px; text-transform:uppercase; letter-spacing:0.5px; }
        .formula-panel .numeric-panel-title { color:#a5b4fc; }
        .pattern-row { display:flex; justify-content:space-between; gap:8px; padding:2px 0; }
        .pattern-word { color:#d4d4d8; font-weight:500; }
        .pattern-digits { color:#6ee7b7; font-family:'JetBrains Mono',monospace; font-size:10px; letter-spacing:0.8px; opacity:0.8; }
        .formula-stats { display:flex; gap:10px; font-size:10px; color:#a1a1aa; margin-bottom:4px; }
        .formula-stats b { color:#a5b4fc; }
        .genome-hex { font-family:'JetBrains Mono',monospace; font-size:9px; color:#818cf8; letter-spacing:1px; word-break:break-all; opacity:0.7; }

        /* --- Формульные слова --- */
        .formula-words-panel { background:rgba(192,132,252,0.06); border-color:rgba(192,132,252,0.15); }
        .formula-words-panel .numeric-panel-title { color:#c084fc; }
        .formula-words-grid { display:flex; flex-wrap:wrap; gap:6px; }
        .formula-word-chip { display:inline-flex; align-items:center; gap:4px; padding:3px 10px; background:rgba(192,132,252,0.1); border:1px solid rgba(192,132,252,0.2); border-radius:20px; font-size:11px; }
        .fw-word { color:#e9d5ff; font-weight:600; }
        .fw-score { color:#a78bfa; font-size:9px; opacity:0.7; }

        /* --- Retrieved предложения --- */
        .retrieved-panel { background:rgba(34,211,238,0.06); border-color:rgba(34,211,238,0.15); min-width:300px; }
        .retrieved-panel .numeric-panel-title { color:#22d3ee; }
        .retrieved-row { display:flex; justify-content:space-between; align-items:flex-start; gap:8px; padding:4px 0; border-bottom:1px solid rgba(255,255,255,0.03); }
        .retrieved-row:last-child { border-bottom:none; }
        .retrieved-text { color:#a1a1aa; font-size:11px; line-height:1.4; flex:1; }
        .retrieved-score { color:#22d3ee; font-size:10px; font-weight:700; white-space:nowrap; padding:1px 6px; background:rgba(34,211,238,0.1); border-radius:4px; }

        .message-meta { display:flex; align-items:center; gap:12px; margin-top:8px; flex-wrap:wrap; }
        .confidence-badge { display:flex; align-items:center; gap:6px; font-size:11px; padding:4px 10px; background:rgba(255,255,255,0.03); border:1px solid rgba(255,255,255,0.06); border-radius:8px; }
        .method-icon { font-size:12px; }
        .method-label { font-size:10px; font-weight:600; text-transform:uppercase; letter-spacing:0.5px; }
        .confidence-bar-bg { width:40px; height:4px; background:rgba(255,255,255,0.08); border-radius:2px; overflow:hidden; }
        .confidence-bar-fill { height:100%; border-radius:2px; transition:width 0.5s ease; }
        .confidence-value { font-size:10px; color:#71717a; font-weight:600; min-width:28px; text-align:right; }
        .graph-score-badge { display:flex; align-items:center; gap:4px; font-size:10px; color:#52525b; }

        .thinking-indicator { display:flex; align-items:center; gap:8px; color:#10b981; font-size:13px; }
        .thinking-spinner { animation:spin 1s linear infinite; }
        @keyframes spin { to{transform:rotate(360deg)} }

        .chat-input-area { padding:14px 20px 20px; border-top:1px solid rgba(255,255,255,0.06); background:rgba(24,24,27,0.4); }
        .chat-input-container { display:flex; gap:12px; align-items:flex-end; background:rgba(39,39,42,0.6); border:1px solid rgba(255,255,255,0.08); border-radius:16px; padding:12px 16px; transition:border-color 0.15s; }
        .chat-input-container:focus-within { border-color:rgba(16,185,129,0.5); }
        .chat-input { flex:1; background:transparent; border:none; outline:none; color:#fafafa; font-size:14px; line-height:1.5; resize:none; max-height:120px; }
        .chat-input::placeholder { color:#52525b; }
        .chat-send-btn { width:40px; height:40px; border-radius:10px; background:linear-gradient(135deg,#10b981,#059669); border:none; color:white; cursor:pointer; display:flex; align-items:center; justify-content:center; transition:all 0.15s; flex-shrink:0; }
        .chat-send-btn:hover:not(:disabled) { transform:scale(1.05); }
        .chat-send-btn:disabled { opacity:0.5; cursor:not-allowed; }
        .chat-input-hint { font-size:11px; color:#3f3f46; text-align:center; margin-top:8px; }
        .hint-conv-id { color:#52525b; }
      `}</style>
    </div>
  );
};
