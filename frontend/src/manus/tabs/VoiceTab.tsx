/**
 * VoiceTab.tsx
 *
 * Голосовые команды: распознавание речи, отправка в AI, озвучка ответа.
 */

import { useRef, useState } from 'react';
import { AlertCircle, Loader2, Mic, Send, Square, Volume2 } from 'lucide-react';

interface VoiceAIResponse {
  response: string;
  conversation_id: string;
}

type SpeechRecognitionCtor = new () => {
  lang: string;
  continuous: boolean;
  interimResults: boolean;
  start: () => void;
  stop: () => void;
  onresult: ((event: any) => void) | null;
  onerror: ((event: any) => void) | null;
  onend: (() => void) | null;
};

function resolveRecognitionCtor(): SpeechRecognitionCtor | null {
  const win = window as Window & {
    SpeechRecognition?: SpeechRecognitionCtor;
    webkitSpeechRecognition?: SpeechRecognitionCtor;
  };
  return win.SpeechRecognition || win.webkitSpeechRecognition || null;
}

async function askAI(message: string, conversationId: string | null): Promise<VoiceAIResponse> {
  const resp = await fetch('/api/v1/ai/chat', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      message,
      conversation_id: conversationId,
      temperature: 0.6,
    }),
  });
  if (!resp.ok) {
    throw new Error(`AI endpoint error (${resp.status})`);
  }
  const payload = await resp.json();
  return {
    response: payload.response ?? '',
    conversation_id: payload.conversation_id ?? conversationId ?? '',
  };
}

export const VoiceTab = () => {
  const [transcript, setTranscript] = useState('');
  const [conversationId, setConversationId] = useState<string | null>(null);
  const [isListening, setIsListening] = useState(false);
  const [isProcessing, setIsProcessing] = useState(false);
  const [response, setResponse] = useState('');
  const [error, setError] = useState('');
  const recognitionRef = useRef<InstanceType<SpeechRecognitionCtor> | null>(null);

  const startListening = () => {
    setError('');
    const Ctor = resolveRecognitionCtor();
    if (!Ctor) {
      setError('Голосовой ввод не поддерживается в этом браузере.');
      return;
    }

    if (!recognitionRef.current) {
      const rec = new Ctor();
      rec.lang = 'ru-RU';
      rec.continuous = true;
      rec.interimResults = true;
      rec.onresult = (event: any) => {
        let text = '';
        for (let i = event.resultIndex; i < event.results.length; i += 1) {
          text += event.results[i][0].transcript || '';
        }
        setTranscript(text.trim());
      };
      rec.onerror = (event: any) => {
        setError(`Ошибка микрофона: ${event.error || 'unknown'}`);
      };
      rec.onend = () => {
        setIsListening(false);
      };
      recognitionRef.current = rec;
    }

    recognitionRef.current.start();
    setIsListening(true);
  };

  const stopListening = () => {
    recognitionRef.current?.stop();
    setIsListening(false);
  };

  const sendVoiceCommand = async () => {
    const text = transcript.trim();
    if (!text || isProcessing) return;
    setError('');
    setIsProcessing(true);
    try {
      const result = await askAI(text, conversationId);
      setResponse(result.response || 'Пустой ответ от модели.');
      if (result.conversation_id && !conversationId) {
        setConversationId(result.conversation_id);
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setIsProcessing(false);
    }
  };

  const speakAnswer = () => {
    if (!response.trim()) return;
    const utterance = new SpeechSynthesisUtterance(response);
    utterance.lang = 'ru-RU';
    utterance.rate = 1;
    window.speechSynthesis.cancel();
    window.speechSynthesis.speak(utterance);
  };

  return (
    <div className="voice-tab">
      <div className="voice-head">
        <h1>Голос</h1>
        <div className={`voice-status ${isListening ? 'live' : ''}`}>
          <Mic size={14} />
          {isListening ? 'Слушаю...' : 'Ожидание'}
        </div>
      </div>

      <div className="voice-panel">
        <textarea
          className="voice-input"
          value={transcript}
          onChange={(e) => setTranscript(e.target.value)}
          placeholder="Скажите команду голосом или введите текст вручную..."
        />

        <div className="voice-actions">
          {!isListening ? (
            <button type="button" className="voice-btn primary" onClick={startListening}>
              <Mic size={16} />
              Начать слушать
            </button>
          ) : (
            <button type="button" className="voice-btn danger" onClick={stopListening}>
              <Square size={16} />
              Остановить
            </button>
          )}
          <button
            type="button"
            className="voice-btn primary"
            onClick={sendVoiceCommand}
            disabled={isProcessing || !transcript.trim()}
          >
            {isProcessing ? <Loader2 size={16} className="spin" /> : <Send size={16} />}
            Отправить
          </button>
          <button type="button" className="voice-btn" onClick={speakAnswer} disabled={!response.trim()}>
            <Volume2 size={16} />
            Озвучить ответ
          </button>
        </div>
      </div>

      {error && (
        <div className="voice-error">
          <AlertCircle size={16} />
          {error}
        </div>
      )}

      <div className="voice-output">
        <div className="voice-output-title">Ответ AI:</div>
        <div className="voice-output-body">{response || 'Пока нет ответа.'}</div>
      </div>

      <style>{`
        .voice-tab {
          height: 100%;
          overflow: auto;
          padding: 20px;
          color: var(--text-primary);
          background: var(--bg-primary);
        }

        .voice-head {
          display: flex;
          align-items: center;
          justify-content: space-between;
          margin-bottom: 14px;
        }

        .voice-head h1 {
          margin: 0;
          font-size: 22px;
          font-weight: 700;
        }

        .voice-status {
          display: inline-flex;
          align-items: center;
          gap: 6px;
          font-size: 12px;
          color: var(--text-muted);
          border: 1px solid var(--border-primary);
          border-radius: 999px;
          padding: 5px 10px;
          background: var(--bg-overlay);
        }

        .voice-status.live {
          color: var(--accent-primary);
          border-color: var(--border-accent);
          background: var(--accent-bg);
        }

        .voice-panel {
          border: 1px solid var(--border-primary);
          border-radius: 14px;
          background: var(--bg-card);
          padding: 14px;
        }

        .voice-input {
          width: 100%;
          min-height: 140px;
          border: 1px solid var(--border-primary);
          border-radius: 10px;
          background: var(--bg-input);
          color: var(--text-primary);
          font-size: 14px;
          line-height: 1.5;
          padding: 10px 12px;
          outline: none;
          resize: vertical;
          box-sizing: border-box;
        }

        .voice-actions {
          margin-top: 10px;
          display: flex;
          align-items: center;
          gap: 8px;
          flex-wrap: wrap;
        }

        .voice-btn {
          min-height: 36px;
          border-radius: 8px;
          border: 1px solid var(--border-primary);
          background: var(--bg-overlay);
          color: var(--text-secondary);
          font-size: 13px;
          font-weight: 600;
          display: inline-flex;
          align-items: center;
          gap: 7px;
          padding: 0 12px;
          cursor: pointer;
        }

        .voice-btn:disabled {
          opacity: 0.45;
          cursor: not-allowed;
        }

        .voice-btn.primary {
          border-color: var(--border-accent);
          background: var(--accent-bg);
          color: var(--accent-primary);
        }

        .voice-btn.danger {
          border-color: rgba(239, 68, 68, 0.35);
          background: rgba(239, 68, 68, 0.1);
          color: var(--error);
        }

        .voice-error {
          margin-top: 12px;
          border: 1px solid rgba(239, 68, 68, 0.3);
          background: rgba(239, 68, 68, 0.1);
          color: #ff8f8f;
          border-radius: 10px;
          padding: 10px 12px;
          font-size: 13px;
          display: inline-flex;
          align-items: center;
          gap: 8px;
        }

        .voice-output {
          margin-top: 14px;
          border: 1px solid var(--border-primary);
          border-radius: 14px;
          background: var(--bg-card);
          padding: 14px;
        }

        .voice-output-title {
          font-size: 13px;
          font-weight: 700;
          color: var(--text-secondary);
          margin-bottom: 8px;
        }

        .voice-output-body {
          white-space: pre-wrap;
          word-break: break-word;
          font-size: 14px;
          line-height: 1.55;
          color: var(--text-primary);
          min-height: 40px;
        }

        .spin {
          animation: spin 1s linear infinite;
        }

        @keyframes spin {
          to { transform: rotate(360deg); }
        }
      `}</style>
    </div>
  );
};
