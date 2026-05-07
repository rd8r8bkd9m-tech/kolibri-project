/**
 * ChatTab.tsx
 *
 * Main conversational surface with a GPT-style structure:
 * header, thread, sticky composer, and mobile-safe behavior.
 */

import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  ArrowUp,
  Bot,
  Check,
  Copy,
  Database,
  Ellipsis,
  Loader2,
  ListTodo,
  Mic,
  Paperclip,
  RefreshCw,
  Rocket,
  Settings2,
  Share2,
  ThumbsDown,
  ThumbsUp,
  Video,
  RotateCcw,
  Volume2,
  VolumeX,
} from 'lucide-react';
import ReactMarkdown from 'react-markdown';
import remarkGfm from 'remark-gfm';
import type { ChatHistoryItem, TabId } from '../ManusLayout';
import { KolibriBrandMark } from '../components/KolibriBrandMark';
import kolibriBridge from '../../core/kolibri-bridge';

interface Message {
  id: string;
  role: 'user' | 'assistant';
  content: string;
  timestamp: Date;
  gpuContext?: GpuContextHit[];
}

interface AIResponse {
  response: string;
  conversation_id: string;
  gpu_context?: GpuContextHit[];
}

interface GpuContextHit {
  doc_id: number;
  score: number;
  path: string;
  class?: string;
  bytes?: number;
  snippet: string;
}

interface VoiceHealthResponse {
  enabled: boolean;
  default_voice?: string;
}

interface VoiceSpeakResponse {
  audio_base64: string;
  audio_mime: string;
}

interface ChatTabProps {
  resetToken?: number;
  activeChatId?: string;
  onChatActivity?: (item: ChatHistoryItem) => void;
  onNavigate?: (tab: TabId) => void;
}

interface SpeechRecognitionAlternativeLike {
  transcript: string;
  confidence?: number;
}

interface SpeechRecognitionResultLike {
  readonly isFinal: boolean;
  readonly length: number;
  [index: number]: SpeechRecognitionAlternativeLike;
}

interface SpeechRecognitionResultListLike {
  readonly length: number;
  [index: number]: SpeechRecognitionResultLike;
}

interface SpeechRecognitionResultEventLike {
  readonly resultIndex: number;
  readonly results: SpeechRecognitionResultListLike;
}

interface SpeechRecognitionErrorEventLike {
  readonly error?: string;
}

type SpeechRecognitionCtor = new () => {
  lang: string;
  continuous: boolean;
  interimResults: boolean;
  start: () => void;
  stop: () => void;
  onresult: ((event: SpeechRecognitionResultEventLike) => void) | null;
  onerror: ((event: SpeechRecognitionErrorEventLike) => void) | null;
  onend: (() => void) | null;
};

const API = '/api';
const CHAT_SESSIONS_KEY = 'kolibri-chat-sessions-v1';
const CHAT_AUTO_SPEAK_KEY = 'kolibri-chat-auto-speak-v1';
const CHAT_REQUEST_RETRIES = 2;
const STARTER_PROMPTS = [
  'Сделай быстрый статус backend и frontend.',
  'Покажи 5 рисков текущей архитектуры и решения.',
  'Обучи модель на https://en.wikipedia.org/wiki/Neural_network',
  'Составь план релиза на эту неделю.',
];

type ChatModeId = 'smart' | 'deep' | 'creative';

const CHAT_MODES: Array<{ id: ChatModeId; label: string; temperature: number }> = [
  { id: 'smart', label: 'Auto', temperature: 0.62 },
  { id: 'deep', label: 'Глубоко', temperature: 0.45 },
  { id: 'creative', label: 'Креатив', temperature: 0.9 },
];

const FEEDBACK_EMOJIS = ['😡', '😔', '😐', '🙂', '😁'];

const buildFollowUps = (content: string): string[] => {
  const text = content.toLowerCase();
  if (!text.trim()) {
    return [];
  }
  if (text.includes('колобок')) {
    return ['Расскажи историю Колобка для детей', 'Сказка про Теремок', 'Сделай конец счастливым'];
  }
  if (text.includes('план') || text.includes('задач')) {
    return ['Сделай это в формате чек-листа', 'Добавь сроки и риски', 'Сформируй это как задачу'];
  }
  if (text.includes('код') || text.includes('backend') || text.includes('frontend')) {
    return ['Покажи конкретные правки', 'Сделай вариант без регрессий', 'Добавь тест-кейсы'];
  }
  return ['Уточни ключевой вывод', 'Сделай краткую версию', 'Продолжай'];
};

const sleep = (ms: number) => new Promise((resolve) => globalThis.setTimeout(resolve, ms));

async function fetchWithRetry(url: string, init: RequestInit): Promise<Response> {
  for (let attempt = 0; attempt <= CHAT_REQUEST_RETRIES; attempt += 1) {
    try {
      const response = await fetch(url, init);
      if (response.ok || response.status < 500 || attempt === CHAT_REQUEST_RETRIES) {
        return response;
      }
    } catch (error) {
      if (attempt === CHAT_REQUEST_RETRIES) {
        throw error;
      }
    }

    const backoff = 260 * (attempt + 1);
    await sleep(backoff);
  }

  throw new Error('Сервер не отвечает');
}

async function parseErrorMessage(response: Response, fallback: string): Promise<string> {
  try {
    const payload = await response.json();
    if (payload && typeof payload.detail === 'string' && payload.detail.trim()) {
      return payload.detail.trim();
    }
    if (payload && typeof payload.response === 'string' && payload.response.trim()) {
      return payload.response.trim();
    }
  } catch {
    // ignore JSON parsing errors
  }
  return fallback;
}

const resolveBridgeModeLabel = (temperature: number): string => {
  if (temperature <= 0.5) {
    return 'Глубоко';
  }
  if (temperature >= 0.85) {
    return 'Креатив';
  }
  return 'Auto';
};

async function sendApiAIMessage(message: string, conversationId: string | null, temperature: number): Promise<AIResponse> {
  const response = await fetchWithRetry(`${API}/v1/ai/chat`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      message,
      conversation_id: conversationId,
      temperature,
    }),
  });

  if (!response.ok) {
    const detail = await parseErrorMessage(response, `AI endpoint error (${response.status})`);
    throw new Error(detail);
  }

  const payload = await response.json();
  const rawContext = Array.isArray(payload.gpu_context) ? payload.gpu_context : [];
  return {
    response: payload.response ?? '',
    conversation_id: payload.conversation_id ?? conversationId ?? '',
    gpu_context: rawContext
      .map((item: unknown) => {
        if (!item || typeof item !== 'object') return null;
        const hit = item as Record<string, unknown>;
        const snippet = typeof hit.snippet === 'string' ? hit.snippet.trim() : '';
        const path = typeof hit.path === 'string' ? hit.path.trim() : '';
        if (!snippet || !path) return null;
        return {
          doc_id: Number(hit.doc_id ?? 0),
          score: Number(hit.score ?? 0),
          path,
          class: typeof hit.class === 'string' ? hit.class : undefined,
          bytes: Number(hit.bytes ?? 0),
          snippet,
        } satisfies GpuContextHit;
      })
      .filter((item: GpuContextHit | null): item is GpuContextHit => Boolean(item)),
  };
}

async function sendAIMessage(message: string, conversationId: string | null, temperature: number): Promise<AIResponse> {
  const urlMatch = message.match(/https?:\/\/\S+/i);
  if (urlMatch && (message.toLowerCase().includes('обучи') || message.toLowerCase().includes('обход'))) {
    const url = urlMatch[0];
    const pagesMatch = message.match(/(\d+)\s*страниц/);
    const maxPages = pagesMatch ? parseInt(pagesMatch[1], 10) : 5;

    const crawlResp = await fetchWithRetry(`${API}/v1/crawl`, {
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

    await fetchWithRetry(`${API}/v1/ai/reload`, { method: 'POST' });

    return {
      response:
        `Обучение завершено.\n` +
        `Источник: ${url}\n` +
        `Страниц: ${crawlData.pages_crawled ?? 0}, токенов: ${(crawlData.tokens ?? 0).toLocaleString()}`,
      conversation_id: conversationId || '',
    };
  }

  try {
    return await sendApiAIMessage(message, conversationId, temperature);
  } catch (apiError) {
    console.warn('[ChatTab] Backend AI недоступен, используем локальный KolibriBridge fallback.', apiError);
    const response = await kolibriBridge.ask(message, resolveBridgeModeLabel(temperature));
    return {
      response,
      conversation_id: conversationId || globalThis.crypto.randomUUID(),
      gpu_context: [],
    };
  }
}

const resolveRecognitionCtor = (): SpeechRecognitionCtor | null => {
  if (typeof window === 'undefined') {
    return null;
  }
  const win = window as Window & {
    SpeechRecognition?: SpeechRecognitionCtor;
    webkitSpeechRecognition?: SpeechRecognitionCtor;
  };
  return win.SpeechRecognition || win.webkitSpeechRecognition || null;
};

const mapSpeechError = (code: string): string => {
  switch (code) {
    case 'not-allowed':
    case 'service-not-allowed':
      return 'Доступ к микрофону заблокирован. Разрешите его в настройках браузера.';
    case 'no-speech':
      return 'Речь не распознана. Попробуйте говорить ближе к микрофону.';
    case 'audio-capture':
      return 'Микрофон не найден или занят другим приложением.';
    case 'network':
      return 'Ошибка сети во время распознавания речи.';
    case 'aborted':
      return 'Голосовой ввод остановлен.';
    default:
      return `Ошибка микрофона: ${code || 'unknown'}`;
  }
};

const canUseSpeechSynthesis = (): boolean => {
  if (typeof window === 'undefined') {
    return false;
  }
  return typeof window.speechSynthesis !== 'undefined' && typeof window.SpeechSynthesisUtterance !== 'undefined';
};

export const ChatTab = ({ resetToken = 0, activeChatId, onChatActivity, onNavigate }: ChatTabProps) => {
  const [messages, setMessages] = useState<Message[]>([]);
  const [input, setInput] = useState('');
  const [conversationId, setConversationId] = useState<string | null>(null);
  const [isProcessing, setIsProcessing] = useState(false);
  const [micPulse, setMicPulse] = useState(false);
  const [copiedMessageId, setCopiedMessageId] = useState<string | null>(null);
  const [isListening, setIsListening] = useState(false);
  const [voiceError, setVoiceError] = useState<string>('');
  const [isSpeaking, setIsSpeaking] = useState(false);
  const [serverVoiceAvailable, setServerVoiceAvailable] = useState(false);
  const [serverVoiceName, setServerVoiceName] = useState('alloy');
  const [chatMode, setChatMode] = useState<ChatModeId>('smart');
  const [voiceSession, setVoiceSession] = useState(false);
  const [showCallFeedback, setShowCallFeedback] = useState(false);
  const [messageReactions, setMessageReactions] = useState<Record<string, 'up' | 'down' | undefined>>({});
  const [autoSpeak, setAutoSpeak] = useState<boolean>(() => {
    try {
      if (typeof window === 'undefined') {
        return true;
      }
      const raw = localStorage.getItem(CHAT_AUTO_SPEAK_KEY);
      if (raw === '0') {
        return false;
      }
      return true;
    } catch {
      return true;
    }
  });

  const endRef = useRef<HTMLDivElement>(null);
  const composerRef = useRef<HTMLTextAreaElement>(null);
  const localSessionIdRef = useRef<string | null>(null);
  const micTimerRef = useRef<number | null>(null);
  const recognitionRef = useRef<InstanceType<SpeechRecognitionCtor> | null>(null);
  const finalTranscriptRef = useRef('');
  const speechUtteranceRef = useRef<SpeechSynthesisUtterance | null>(null);
  const replyAudioRef = useRef<HTMLAudioElement | null>(null);

  const loadSession = useCallback((sessionId: string) => {
    try {
      const raw = localStorage.getItem(CHAT_SESSIONS_KEY);
      if (!raw) {
        return null;
      }
      const parsed = JSON.parse(raw) as Record<
        string,
        {
          messages: Array<{ id: string; role: 'user' | 'assistant'; content: string; timestamp: string; gpuContext?: GpuContextHit[] }>;
          conversationId: string | null;
        }
      >;
      const found = parsed[sessionId];
      if (!found) {
        return null;
      }
      return {
        conversationId: found.conversationId || null,
        messages: (found.messages || []).map((item) => ({
          id: item.id,
          role: item.role,
          content: item.content,
          timestamp: new Date(item.timestamp),
          gpuContext: Array.isArray(item.gpuContext) ? item.gpuContext : undefined,
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
              messages: Array<{ id: string; role: 'user' | 'assistant'; content: string; timestamp: string; gpuContext?: GpuContextHit[] }>;
              conversationId: string | null;
            }
          >)
        : {};

      parsed[sessionId] = {
        conversationId: nextConversationId,
        messages: nextMessages.slice(-120).map((item) => ({
          id: item.id,
          role: item.role,
          content: item.content,
          timestamp: item.timestamp.toISOString(),
          gpuContext: item.gpuContext,
        })),
      };

      localStorage.setItem(CHAT_SESSIONS_KEY, JSON.stringify(parsed));
    } catch {
      // ignore storage errors
    }
  }, []);

  const pushHistory = useCallback(
    (title: string, preview: string) => {
      const id = localSessionIdRef.current || `chat-${Date.now()}`;
      localSessionIdRef.current = id;
      onChatActivity?.({
        id,
        title: title.trim().slice(0, 52) || 'Новый чат',
        preview: preview.trim().slice(0, 120),
        updatedAt: Date.now(),
        unread: false,
      });
    },
    [onChatActivity],
  );

  const resizeComposer = useCallback(() => {
    if (!composerRef.current) {
      return;
    }
    composerRef.current.style.height = '0px';
    const nextHeight = Math.min(composerRef.current.scrollHeight, 220);
    composerRef.current.style.height = `${nextHeight}px`;
  }, []);

  useEffect(() => {
    resizeComposer();
  }, [input, resizeComposer]);

  useEffect(() => {
    endRef.current?.scrollIntoView({ behavior: 'smooth', block: 'end' });
  }, [messages, isProcessing]);

  useEffect(() => {
    return () => {
      if (micTimerRef.current != null) {
        window.clearTimeout(micTimerRef.current);
      }
      try {
        recognitionRef.current?.stop();
      } catch {
        // ignore voice cleanup errors
      }
      if (canUseSpeechSynthesis()) {
        window.speechSynthesis.cancel();
      }
      if (replyAudioRef.current) {
        replyAudioRef.current.pause();
        replyAudioRef.current.src = '';
      }
    };
  }, []);

  useEffect(() => {
    const loadVoiceHealth = async () => {
      try {
        const resp = await fetch('/api/v1/ai/voice/health');
        if (!resp.ok) {
          return;
        }
        const payload = (await resp.json()) as VoiceHealthResponse;
        if (payload.enabled) {
          setServerVoiceAvailable(true);
          if (payload.default_voice?.trim()) {
            setServerVoiceName(payload.default_voice.trim());
          }
        }
      } catch {
        // silent fallback to browser speech
      }
    };
    void loadVoiceHealth();
  }, []);

  useEffect(() => {
    try {
      localStorage.setItem(CHAT_AUTO_SPEAK_KEY, autoSpeak ? '1' : '0');
    } catch {
      // ignore storage errors
    }
  }, [autoSpeak]);

  useEffect(() => {
    setMessages([]);
    setInput('');
    setConversationId(null);
    localSessionIdRef.current = activeChatId || null;
  }, [resetToken, activeChatId]);

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
      return;
    }

    setMessages([]);
    setConversationId(null);
  }, [activeChatId, loadSession]);

  useEffect(() => {
    const sessionId = localSessionIdRef.current;
    if (!sessionId) {
      return;
    }
    persistSession(sessionId, messages, conversationId);
  }, [messages, conversationId, persistSession]);

  const canSend = useMemo(() => input.trim().length > 0 && !isProcessing, [input, isProcessing]);
  const activeMode = useMemo(() => CHAT_MODES.find((item) => item.id === chatMode) ?? CHAT_MODES[0], [chatMode]);
  const voiceSupported = useMemo(() => resolveRecognitionCtor() !== null, []);
  const speechSupported = useMemo(() => canUseSpeechSynthesis(), []);
  const audioReplySupported = speechSupported || serverVoiceAvailable;
  const latestAssistantReply = useMemo(() => {
    for (let i = messages.length - 1; i >= 0; i -= 1) {
      if (messages[i].role === 'assistant') {
        return messages[i].content;
      }
    }
    return '';
  }, [messages]);
  const lastUserMessage = useMemo(() => {
    for (let i = messages.length - 1; i >= 0; i -= 1) {
      if (messages[i].role === 'user') {
        return messages[i].content;
      }
    }
    return '';
  }, [messages]);
  const followUpSuggestions = useMemo(() => buildFollowUps(latestAssistantReply), [latestAssistantReply]);

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

  const stopListening = useCallback(() => {
    try {
      recognitionRef.current?.stop();
    } catch {
      // stop can throw if recognition is not active
    }
    setIsListening(false);
  }, []);

  const startListening = useCallback(() => {
    if (isProcessing) {
      return;
    }
    setVoiceError('');
    const Ctor = resolveRecognitionCtor();
    if (!Ctor) {
      setVoiceError('Голосовой ввод не поддерживается в этом браузере.');
      return;
    }
    finalTranscriptRef.current = '';

    if (!recognitionRef.current) {
      const recognition = new Ctor();
      const browserLang = typeof navigator !== 'undefined' ? navigator.language : '';
      recognition.lang = browserLang?.toLowerCase().startsWith('ru') ? 'ru-RU' : browserLang || 'ru-RU';
      recognition.continuous = true;
      recognition.interimResults = true;
      recognition.onresult = (event) => {
        let interim = '';
        for (let i = event.resultIndex; i < event.results.length; i += 1) {
          const value = event.results[i][0].transcript || '';
          if (event.results[i].isFinal) {
            finalTranscriptRef.current = `${finalTranscriptRef.current} ${value}`.trim();
          } else {
            interim += value;
          }
        }
        const nextText = `${finalTranscriptRef.current} ${interim}`.trim();
        setInput(nextText);
      };
      recognition.onerror = (event) => {
        setVoiceError(mapSpeechError(event.error || 'unknown'));
        setIsListening(false);
      };
      recognition.onend = () => {
        setIsListening(false);
      };
      recognitionRef.current = recognition;
    }

    try {
      recognitionRef.current.start();
      setIsListening(true);
      triggerMicPulse();
    } catch (error) {
      setVoiceError(`Не удалось запустить микрофон: ${error instanceof Error ? error.message : String(error)}`);
    }
  }, [isProcessing, triggerMicPulse]);

  const handleVoiceToggle = useCallback(() => {
    if (isListening) {
      stopListening();
      return;
    }
    startListening();
  }, [isListening, startListening, stopListening]);

  const handleCopyMessage = useCallback(async (messageId: string, content: string) => {
    try {
      await navigator.clipboard.writeText(content);
      setCopiedMessageId(messageId);
      window.setTimeout(() => setCopiedMessageId((current) => (current === messageId ? null : current)), 1600);
    } catch {
      // ignore clipboard errors
    }
  }, []);

  const handleShareMessage = useCallback(
    async (content: string) => {
      const text = content.trim();
      if (!text) {
        return;
      }

      const nav = typeof navigator === 'undefined' ? null : (navigator as Navigator & { share?: (data: ShareData) => Promise<void> });
      if (nav?.share) {
        try {
          await nav.share({ text });
          return;
        } catch {
          // ignore and fallback to clipboard
        }
      }

      await handleCopyMessage(`share-${Date.now()}`, text);
    },
    [handleCopyMessage],
  );

  const speakReply = useCallback(
    async (text: string) => {
      const content = text.trim();
      if (!content) {
        return;
      }

      if (serverVoiceAvailable) {
        try {
          const response = await fetch('/api/v1/ai/voice/speak', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
              text: content,
              voice: serverVoiceName,
              audio_format: 'mp3',
              speed: 1.0,
            }),
          });
          if (response.ok) {
            const payload = (await response.json()) as VoiceSpeakResponse;
            const audio = new Audio(`data:${payload.audio_mime};base64,${payload.audio_base64}`);
            if (replyAudioRef.current) {
              replyAudioRef.current.pause();
              replyAudioRef.current.src = '';
            }
            replyAudioRef.current = audio;
            audio.onplay = () => setIsSpeaking(true);
            audio.onended = () => setIsSpeaking(false);
            audio.onerror = () => {
              setIsSpeaking(false);
              setVoiceError('Не удалось воспроизвести серверный голос.');
            };
            await audio.play();
            return;
          }
          if (response.status >= 500) {
            setServerVoiceAvailable(false);
            setVoiceError('Серверный голос временно недоступен. Переключен на браузерную озвучку.');
          }
        } catch {
          setServerVoiceAvailable(false);
        }
      }

      if (!speechSupported) {
        return;
      }

      const message = content.length > 1400 ? `${content.slice(0, 1400)}...` : content;
      window.speechSynthesis.cancel();
      const utterance = new SpeechSynthesisUtterance(message);
      const browserLang = typeof navigator !== 'undefined' ? navigator.language : '';
      utterance.lang = browserLang?.toLowerCase().startsWith('ru') ? 'ru-RU' : browserLang || 'ru-RU';
      utterance.rate = 1;
      utterance.onstart = () => setIsSpeaking(true);
      utterance.onend = () => {
        setIsSpeaking(false);
        speechUtteranceRef.current = null;
      };
      utterance.onerror = () => {
        setIsSpeaking(false);
        speechUtteranceRef.current = null;
      };
      speechUtteranceRef.current = utterance;
      window.speechSynthesis.speak(utterance);
    },
    [serverVoiceAvailable, serverVoiceName, speechSupported],
  );

  const resetCurrentConversation = () => {
    stopListening();
    if (speechSupported) {
      window.speechSynthesis.cancel();
    }
    setIsSpeaking(false);
    if (replyAudioRef.current) {
      replyAudioRef.current.pause();
      replyAudioRef.current.src = '';
      replyAudioRef.current = null;
    }
    setMessages([]);
    setInput('');
    setConversationId(null);
    setVoiceSession(false);
    setShowCallFeedback(false);
    setMessageReactions({});
    localSessionIdRef.current = `chat-${Date.now()}`;
  };

  const sendMessageText = useCallback(
    async (rawText: string) => {
      const text = rawText.trim();
      if (!text || isProcessing) {
        return;
      }
      stopListening();

      const userMessage: Message = {
        id: `${Date.now()}-user`,
        role: 'user',
        content: text,
        timestamp: new Date(),
      };

      setMessages((previous) => [...previous, userMessage]);
      pushHistory(text, text);
      setInput('');
      setIsProcessing(true);

      try {
        const result = await sendAIMessage(text, conversationId, activeMode.temperature);
        if (result.conversation_id) {
          setConversationId(result.conversation_id);
        }

        const assistantMessage: Message = {
          id: `${Date.now()}-assistant`,
          role: 'assistant',
          content: result.response || 'Не удалось сформировать ответ.',
          timestamp: new Date(),
          gpuContext: result.gpu_context,
        };

        setMessages((previous) => [...previous, assistantMessage]);
        pushHistory(text, assistantMessage.content);
        setShowCallFeedback(true);
        if (autoSpeak) {
          void speakReply(assistantMessage.content);
        }
      } catch (error) {
        const message = `Ошибка: ${error instanceof Error ? error.message : String(error)}`;
        const failedMessage: Message = {
          id: `${Date.now()}-error`,
          role: 'assistant',
          content: message,
          timestamp: new Date(),
        };
        setMessages((previous) => [...previous, failedMessage]);
        pushHistory(text, message);
      } finally {
        setIsProcessing(false);
        composerRef.current?.focus();
      }
    },
    [activeMode.temperature, autoSpeak, conversationId, isProcessing, pushHistory, speakReply, stopListening],
  );

  const handleSend = useCallback(async () => {
    await sendMessageText(input);
  }, [input, sendMessageText]);

  const handleRegenerate = useCallback(async () => {
    if (!lastUserMessage || isProcessing) {
      return;
    }
    await sendMessageText(lastUserMessage);
  }, [isProcessing, lastUserMessage, sendMessageText]);

  const stopVoiceSession = useCallback(() => {
    setVoiceSession(false);
    stopListening();
    setIsSpeaking(false);
    if (speechSupported) {
      window.speechSynthesis.cancel();
    }
    if (replyAudioRef.current) {
      replyAudioRef.current.pause();
      replyAudioRef.current.src = '';
      replyAudioRef.current = null;
    }
  }, [speechSupported, stopListening]);

  const handleTalkToggle = useCallback(() => {
    if (voiceSession) {
      stopVoiceSession();
      return;
    }
    setVoiceSession(true);
    if (!isListening) {
      startListening();
    }
  }, [isListening, startListening, stopVoiceSession, voiceSession]);

  useEffect(() => {
    if (!isListening || isProcessing) {
      return;
    }
    stopListening();
  }, [isListening, isProcessing, stopListening]);

  const handleInputKeyDown = (event: React.KeyboardEvent<HTMLTextAreaElement>) => {
    if (event.key === 'Enter' && !event.shiftKey) {
      event.preventDefault();
      void handleSend();
    }
  };

  const handleSuggestionClick = (text: string) => {
    setInput(text);
    window.requestAnimationFrame(() => composerRef.current?.focus());
  };

  const setReaction = (messageId: string, reaction: 'up' | 'down') => {
    setMessageReactions((prev) => ({
      ...prev,
      [messageId]: prev[messageId] === reaction ? undefined : reaction,
    }));
  };

  const statusLabel = isListening
    ? 'Слушаю микрофон'
    : isProcessing
      ? 'Модель отвечает'
      : 'Готово к запросу';

  const primaryActionLabel = canSend ? 'Отправить' : voiceSession ? 'Остановить' : 'Говорить';
  const primaryActionAria = canSend ? 'Отправить сообщение' : primaryActionLabel;

  return (
    <div className="gx-chat-root">
      <header className="gx-chat-header">
        <div className="gx-chat-header-main">
          <h1>Колибри AI</h1>
          <p>{statusLabel}</p>
          <div className="gx-chat-mode-row" role="tablist" aria-label="Режим ответа">
            {CHAT_MODES.map((mode) => (
              <button
                key={mode.id}
                type="button"
                role="tab"
                className={`gx-chat-mode-chip ${chatMode === mode.id ? 'is-active' : ''}`}
                aria-selected={chatMode === mode.id}
                onClick={() => setChatMode(mode.id)}
              >
                {mode.label}
              </button>
            ))}
          </div>
        </div>
        <div className="gx-chat-header-actions">
          <button type="button" className="gx-chat-head-btn" onClick={resetCurrentConversation}>
            <RotateCcw size={14} />
            <span>Очистить</span>
          </button>
          <button
            type="button"
            className={`gx-chat-head-btn is-accent ${isListening ? 'is-listening' : ''}`}
            onClick={handleVoiceToggle}
            disabled={!voiceSupported}
            aria-label={isListening ? 'Остановить голосовой ввод' : 'Запустить голосовой ввод'}
          >
            {isListening ? <Loader2 size={14} className="gx-spin" /> : <Mic size={14} />}
            <span>{isListening ? 'Стоп' : 'Голос'}</span>
          </button>
          <button
            type="button"
            className={`gx-chat-head-btn ${autoSpeak ? 'is-accent' : ''}`}
            onClick={() => setAutoSpeak((v) => !v)}
            disabled={!audioReplySupported}
            aria-label={autoSpeak ? 'Отключить автоозвучку' : 'Включить автоозвучку'}
          >
            {autoSpeak ? <Volume2 size={14} /> : <VolumeX size={14} />}
            <span>{autoSpeak ? 'Озвучка ON' : 'Озвучка OFF'}</span>
          </button>
        </div>
      </header>

      <section className="gx-chat-feed" role="log" aria-live="polite">
        <div className="gx-chat-thread">
          {voiceError && (
            <div className="gx-voice-error" role="status">
              {voiceError}
            </div>
          )}

          {messages.length === 0 && !isProcessing && (
            <div className="gx-chat-placeholder">
              <div className="gx-chat-logo" aria-hidden="true">
                <KolibriBrandMark size={44} />
              </div>
              <div className="gx-empty-state-desktop">
                <h2>Чем помочь в этом чате?</h2>
                <div className="gx-prompt-grid">
                  {STARTER_PROMPTS.map((prompt) => (
                    <button key={prompt} type="button" className="gx-prompt-card" onClick={() => handleSuggestionClick(prompt)}>
                      {prompt}
                    </button>
                  ))}
                </div>
              </div>
            </div>
          )}

          {messages.map((message) => {
            const isAssistant = message.role === 'assistant';
            const reaction = messageReactions[message.id];
            return (
              <article key={message.id} className={`gx-message-row ${isAssistant ? 'is-assistant' : 'is-user'}`}>
                {isAssistant && (
                  <div className="gx-message-avatar" aria-hidden="true">
                    <Bot size={14} />
                  </div>
                )}

                <div className={`gx-message-bubble ${isAssistant ? 'is-assistant' : 'is-user'}`}>
                  {isAssistant ? (
                    <ReactMarkdown remarkPlugins={[remarkGfm]}>{message.content}</ReactMarkdown>
                  ) : (
                    <p>{message.content}</p>
                  )}

                  {isAssistant && message.gpuContext && message.gpuContext.length > 0 && (
                    <div className="gx-gpu-context" aria-label="GPU Store context">
                      <div className="gx-gpu-context-head">
                        <Database size={14} />
                        <span>GPU Store: {message.gpuContext.length} источника</span>
                      </div>
                      <div className="gx-gpu-context-list">
                        {message.gpuContext.slice(0, 4).map((hit) => (
                          <div key={`${hit.doc_id}-${hit.path}`} className="gx-gpu-context-item">
                            <strong>{hit.path}</strong>
                            <span>{hit.score.toFixed(3)}</span>
                            <p>{hit.snippet}</p>
                          </div>
                        ))}
                      </div>
                    </div>
                  )}

                  {isAssistant && (
                    <div className="gx-message-action-row">
                      <button
                        type="button"
                        className="gx-message-action"
                        onClick={() => void handleCopyMessage(message.id, message.content)}
                      >
                        {copiedMessageId === message.id ? <Check size={15} /> : <Copy size={15} />}
                      </button>
                      <button
                        type="button"
                        className="gx-message-action"
                        onClick={() => void handleShareMessage(message.content)}
                      >
                        <Share2 size={15} />
                      </button>
                      <button
                        type="button"
                        className={`gx-message-action ${reaction === 'up' ? 'is-active' : ''}`}
                        onClick={() => setReaction(message.id, 'up')}
                      >
                        <ThumbsUp size={15} />
                      </button>
                      <button
                        type="button"
                        className={`gx-message-action ${reaction === 'down' ? 'is-active' : ''}`}
                        onClick={() => setReaction(message.id, 'down')}
                      >
                        <ThumbsDown size={15} />
                      </button>
                      <button
                        type="button"
                        className="gx-message-action"
                        disabled={!message.content.trim() || isSpeaking}
                        onClick={() => void speakReply(message.content)}
                      >
                        {isSpeaking ? <Loader2 size={15} className="gx-spin" /> : <Volume2 size={15} />}
                      </button>
                      <button type="button" className="gx-message-action" onClick={() => void handleRegenerate()}>
                        <RefreshCw size={15} />
                      </button>
                      <button type="button" className="gx-message-action">
                        <Ellipsis size={15} />
                      </button>
                    </div>
                  )}
                </div>
              </article>
            );
          })}

          {isProcessing && (
            <article className="gx-message-row is-assistant is-loading">
              <div className="gx-message-avatar" aria-hidden="true">
                <Bot size={14} />
              </div>
              <div className="gx-message-bubble is-assistant">
                <div className="gx-loading-line">
                  <Loader2 size={14} className="gx-spin" />
                  <span>Формирую ответ...</span>
                </div>
              </div>
            </article>
          )}

          {showCallFeedback && latestAssistantReply && (
            <section className="gx-call-feedback">
              <h3>Как прошёл твой звонок?</h3>
              <div className="gx-call-feedback-row">
                {FEEDBACK_EMOJIS.map((emoji) => (
                  <button key={emoji} type="button" className="gx-call-feedback-btn">
                    <span>{emoji}</span>
                    <span className="gx-call-feedback-line" />
                  </button>
                ))}
              </div>
            </section>
          )}

          {followUpSuggestions.length > 0 && (
            <section className="gx-followup-list">
              {followUpSuggestions.map((item) => (
                <button key={item} type="button" className="gx-followup-item" onClick={() => handleSuggestionClick(item)}>
                  {item}
                </button>
              ))}
            </section>
          )}

          <div ref={endRef} />
        </div>
      </section>

      <footer className="gx-composer-wrap">
        {messages.length === 0 && !isProcessing && !voiceSession && (
          <div className="gx-cta-row" aria-label="Быстрые действия">
            <button
              type="button"
              className="gx-cta-btn gx-cta-pro"
              onClick={() => {
                if (onNavigate) {
                  onNavigate('settings');
                  return;
                }
                handleSuggestionClick('Расскажи про Колибри Pro и что в нём будет.');
              }}
            >
              <span className="gx-cta-icon" aria-hidden="true">
                <KolibriBrandMark size={18} />
              </span>
              <span>Получить Колибри Pro</span>
            </button>

            <button
              type="button"
              className="gx-cta-btn gx-cta-create"
              onClick={() => {
                if (onNavigate) {
                  onNavigate('tasks');
                  return;
                }
                handleSuggestionClick('Создай задачу на сегодня и напомни мне вечером.');
              }}
            >
              <ListTodo size={18} aria-hidden="true" />
              <span>Создать задачу</span>
            </button>
          </div>
        )}

        {latestAssistantReply && (
          <div className="gx-think-wrap">
            <button type="button" className="gx-think-btn" onClick={() => setChatMode('deep')}>
              <Rocket size={18} />
              <span>Think harder</span>
            </button>
          </div>
        )}

        {voiceSession && (
          <div className="gx-voice-session">
            <div className="gx-voice-tip">Tap here to change the voice or personality</div>
            <div className="gx-voice-controls">
              <button type="button" className="gx-voice-ctrl">
                <Video size={20} />
              </button>
              <button type="button" className="gx-voice-ctrl" onClick={() => setAutoSpeak((v) => !v)}>
                <Volume2 size={20} />
              </button>
              <button type="button" className="gx-voice-ctrl" onClick={handleVoiceToggle}>
                {isListening ? <Loader2 size={19} className="gx-spin" /> : <Mic size={19} />}
              </button>
              <button type="button" className="gx-voice-ctrl">
                <Settings2 size={20} />
              </button>
            </div>
          </div>
        )}

        <div className="gx-composer-card">
          <textarea
            ref={composerRef}
            className="gx-composer-input"
            value={input}
            onChange={(event) => setInput(event.target.value)}
            onKeyDown={handleInputKeyDown}
            placeholder="Спрашивай что угодно"
            disabled={isProcessing}
            rows={1}
          />

          <div className="gx-composer-footer">
            <div className="gx-composer-tools">
              <button type="button" className="gx-tool-btn" aria-label="Прикрепить файл">
                <Paperclip size={18} />
              </button>

              <button
                type="button"
                className={`gx-auto-btn ${chatMode === 'smart' ? 'is-active' : ''}`}
                onClick={() => setChatMode('smart')}
              >
                <Rocket size={17} />
                <span>Auto</span>
              </button>
            </div>

            <div className="gx-composer-primary">
              <button
                type="button"
                className={`gx-tool-btn ${micPulse ? 'is-pulse' : ''}`}
                onClick={handleVoiceToggle}
                disabled={!voiceSupported || isProcessing}
                aria-label={isListening ? 'Остановить голосовой ввод' : 'Голосовой ввод'}
              >
                {isListening ? <Loader2 size={16} className="gx-spin" /> : <Mic size={16} />}
              </button>

              <button
                type="button"
                className={`gx-talk-btn ${voiceSession ? 'is-live' : ''}`}
                onClick={canSend ? () => void handleSend() : handleTalkToggle}
                aria-label={primaryActionAria}
              >
                {canSend ? <ArrowUp size={20} /> : <Volume2 size={20} />}
                <span>{primaryActionLabel}</span>
              </button>
            </div>
          </div>
        </div>
      </footer>
    </div>
  );
};
