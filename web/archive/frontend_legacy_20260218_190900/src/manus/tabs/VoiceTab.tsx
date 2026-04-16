/**
 * VoiceTab.tsx
 *
 * Полноценный голосовой режим:
 * - Browser mode: Web Speech API
 * - Server mode: MediaRecorder -> backend STT -> AI -> backend TTS
 */

import { useEffect, useMemo, useRef, useState } from 'react';
import { AlertCircle, Loader2, Mic, Send, Square, Volume2 } from 'lucide-react';

interface VoiceAIResponse {
  response: string;
  conversation_id: string;
}

interface VoiceHealthResponse {
  enabled: boolean;
  detail: string;
  default_voice?: string;
}

interface VoiceChatTurnResponse {
  transcript: string;
  response: string;
  conversation_id: string;
  confidence: number;
  duration_ms: number;
  audio_base64?: string | null;
  audio_mime?: string | null;
  voice?: string | null;
}

interface VoiceSpeakResponse {
  audio_base64: string;
  audio_mime: string;
  voice: string;
  duration_ms: number;
}

type VoiceMode = 'server' | 'browser';

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
  if (typeof window === 'undefined') {
    return null;
  }
  const win = window as Window & {
    SpeechRecognition?: SpeechRecognitionCtor;
    webkitSpeechRecognition?: SpeechRecognitionCtor;
  };
  return win.SpeechRecognition || win.webkitSpeechRecognition || null;
}

function mapSpeechError(code: string): string {
  switch (code) {
    case 'not-allowed':
    case 'service-not-allowed':
      return 'Доступ к микрофону запрещен. Разрешите его в настройках браузера.';
    case 'no-speech':
      return 'Речь не распознана. Попробуйте говорить немного громче.';
    case 'audio-capture':
      return 'Микрофон не найден или уже используется другим приложением.';
    case 'network':
      return 'Ошибка сети во время распознавания речи.';
    case 'aborted':
      return 'Голосовой ввод остановлен.';
    default:
      return `Ошибка микрофона: ${code || 'unknown'}`;
  }
}

function canUseSpeechSynthesis(): boolean {
  if (typeof window === 'undefined') {
    return false;
  }
  return typeof window.speechSynthesis !== 'undefined' && typeof window.SpeechSynthesisUtterance !== 'undefined';
}

function canRecordAudio(): boolean {
  if (typeof window === 'undefined') {
    return false;
  }
  const mediaDevices = navigator.mediaDevices as MediaDevices | undefined;
  return Boolean(mediaDevices && typeof mediaDevices.getUserMedia === 'function' && typeof MediaRecorder !== 'undefined');
}

function isServerVoiceUnsupported(detail: string): boolean {
  const text = detail.toLowerCase();
  return (
    text.includes('location is not supported') ||
    text.includes('not supported for the api use') ||
    text.includes('stt upstream error') ||
    text.includes('tts upstream error')
  );
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

function blobToBase64(blob: Blob): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => {
      const value = typeof reader.result === 'string' ? reader.result : '';
      if (!value || !value.includes(',')) {
        reject(new Error('Не удалось прочитать аудио'));
        return;
      }
      resolve(value.split(',', 2)[1]);
    };
    reader.onerror = () => reject(new Error('Ошибка чтения аудио'));
    reader.readAsDataURL(blob);
  });
}

async function parseErrorDetail(resp: Response, fallback: string): Promise<string> {
  try {
    const payload = await resp.json();
    if (payload && typeof payload.detail === 'string' && payload.detail.trim()) {
      return payload.detail;
    }
  } catch {
    // ignore json parse errors
  }
  return fallback;
}

export const VoiceTab = () => {
  const [transcript, setTranscript] = useState('');
  const [conversationId, setConversationId] = useState<string | null>(null);
  const [response, setResponse] = useState('');
  const [error, setError] = useState('');

  const [voiceMode, setVoiceMode] = useState<VoiceMode>('server');
  const [serverVoiceAvailable, setServerVoiceAvailable] = useState(false);
  const [serverVoiceDetail, setServerVoiceDetail] = useState('Проверяю серверный голос...');
  const [serverVoiceName, setServerVoiceName] = useState('alloy');

  const [isListening, setIsListening] = useState(false);
  const [isRecordingServer, setIsRecordingServer] = useState(false);
  const [isProcessing, setIsProcessing] = useState(false);
  const [isSpeaking, setIsSpeaking] = useState(false);
  const [autoSpeak, setAutoSpeak] = useState(true);

  const recognitionRef = useRef<InstanceType<SpeechRecognitionCtor> | null>(null);
  const finalTranscriptRef = useRef('');
  const mediaRecorderRef = useRef<MediaRecorder | null>(null);
  const mediaStreamRef = useRef<MediaStream | null>(null);
  const mediaChunksRef = useRef<Blob[]>([]);
  const replyAudioRef = useRef<HTMLAudioElement | null>(null);

  const voiceSupported = useMemo(() => resolveRecognitionCtor() !== null, []);
  const speechSupported = useMemo(() => canUseSpeechSynthesis(), []);
  const recorderSupported = useMemo(() => canRecordAudio(), []);

  useEffect(() => {
    const loadVoiceHealth = async () => {
      try {
        const resp = await fetch('/api/v1/ai/voice/health');
        if (!resp.ok) {
          throw new Error(`voice health ${resp.status}`);
        }
        const payload = (await resp.json()) as VoiceHealthResponse;
        setServerVoiceAvailable(Boolean(payload.enabled));
        setServerVoiceDetail(payload.detail || (payload.enabled ? 'Серверный голос доступен' : 'Серверный голос недоступен'));
        if (payload.default_voice && payload.default_voice.trim()) {
          setServerVoiceName(payload.default_voice.trim());
        }
        if (!payload.enabled) {
          setVoiceMode('browser');
        }
      } catch {
        setServerVoiceAvailable(false);
        setServerVoiceDetail('Нет подключения к серверному голосу. Используется браузерный режим.');
        setVoiceMode('browser');
      }
    };
    void loadVoiceHealth();
  }, []);

  useEffect(() => {
    return () => {
      try {
        recognitionRef.current?.stop();
      } catch {
        // ignore cleanup errors
      }
      try {
        mediaRecorderRef.current?.stop();
      } catch {
        // ignore cleanup errors
      }
      mediaStreamRef.current?.getTracks().forEach((track) => track.stop());
      mediaStreamRef.current = null;

      if (speechSupported) {
        window.speechSynthesis.cancel();
      }
      if (replyAudioRef.current) {
        replyAudioRef.current.pause();
        replyAudioRef.current.src = '';
      }
    };
  }, [speechSupported]);

  useEffect(() => {
    if (voiceMode === 'server') {
      try {
        recognitionRef.current?.stop();
      } catch {
        // ignore stop errors
      }
      setIsListening(false);
      return;
    }
    try {
      mediaRecorderRef.current?.stop();
    } catch {
      // ignore stop errors
    }
    mediaStreamRef.current?.getTracks().forEach((track) => track.stop());
    mediaStreamRef.current = null;
    setIsRecordingServer(false);
  }, [voiceMode]);

  const playServerAudio = async (audioBase64: string, audioMime: string) => {
    const src = `data:${audioMime};base64,${audioBase64}`;
    if (replyAudioRef.current) {
      replyAudioRef.current.pause();
      replyAudioRef.current.src = '';
      replyAudioRef.current = null;
    }

    const audio = new Audio(src);
    replyAudioRef.current = audio;
    audio.onplay = () => setIsSpeaking(true);
    audio.onended = () => setIsSpeaking(false);
    audio.onerror = () => {
      setIsSpeaking(false);
      setError('Не удалось воспроизвести серверный аудио-ответ.');
    };

    try {
      await audio.play();
    } catch (err) {
      setIsSpeaking(false);
      setError(`Браузер заблокировал воспроизведение: ${err instanceof Error ? err.message : String(err)}`);
    }
  };

  const downgradeToBrowserVoice = (reason: string) => {
    setServerVoiceAvailable(false);
    setServerVoiceDetail(reason);
    setVoiceMode('browser');
  };

  const speakResponseBrowser = (text: string, onDone?: () => void) => {
    const content = text.trim();
    if (!content || !speechSupported) {
      onDone?.();
      return;
    }

    window.speechSynthesis.cancel();
    const utterance = new SpeechSynthesisUtterance(content.length > 1400 ? `${content.slice(0, 1400)}...` : content);
    utterance.lang = 'ru-RU';
    utterance.rate = 1;
    utterance.onstart = () => setIsSpeaking(true);
    utterance.onend = () => {
      setIsSpeaking(false);
      onDone?.();
    };
    utterance.onerror = () => {
      setIsSpeaking(false);
      onDone?.();
    };
    window.speechSynthesis.speak(utterance);
  };

  const speakResponseServer = async (text: string) => {
    const content = text.trim();
    if (!content || !serverVoiceAvailable) {
      return;
    }
    const resp = await fetch('/api/v1/ai/voice/speak', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        text: content,
        voice: serverVoiceName,
        audio_format: 'mp3',
        speed: 1.0,
      }),
    });
    if (!resp.ok) {
      const detail = await parseErrorDetail(resp, `Voice speak error (${resp.status})`);
      throw new Error(detail);
    }
    const payload = (await resp.json()) as VoiceSpeakResponse;
    await playServerAudio(payload.audio_base64, payload.audio_mime);
  };

  const startListening = () => {
    if (isProcessing) {
      return;
    }
    setError('');
    const Ctor = resolveRecognitionCtor();
    if (!Ctor) {
      setError('Голосовой ввод не поддерживается в этом браузере.');
      return;
    }
    finalTranscriptRef.current = '';

    if (!recognitionRef.current) {
      const rec = new Ctor();
      const browserLang = typeof navigator !== 'undefined' ? navigator.language : '';
      rec.lang = browserLang?.toLowerCase().startsWith('ru') ? 'ru-RU' : browserLang || 'ru-RU';
      rec.continuous = true;
      rec.interimResults = true;
      rec.onresult = (event: any) => {
        let interim = '';
        for (let i = event.resultIndex; i < event.results.length; i += 1) {
          const value = event.results[i][0].transcript || '';
          if (event.results[i].isFinal) {
            finalTranscriptRef.current = `${finalTranscriptRef.current} ${value}`.trim();
          } else {
            interim += value;
          }
        }
        setTranscript(`${finalTranscriptRef.current} ${interim}`.trim());
      };
      rec.onerror = (event: any) => {
        setError(mapSpeechError(event?.error || 'unknown'));
        setIsListening(false);
      };
      rec.onend = () => {
        setIsListening(false);
      };
      recognitionRef.current = rec;
    }

    try {
      recognitionRef.current.start();
      setIsListening(true);
    } catch (e) {
      setError(`Не удалось запустить микрофон: ${e instanceof Error ? e.message : String(e)}`);
      setIsListening(false);
    }
  };

  const stopListening = () => {
    try {
      recognitionRef.current?.stop();
    } catch {
      // stop may throw when recognition is already stopped
    }
    setIsListening(false);
  };

  const sendTextCommand = async (options?: { resumeListening?: boolean }) => {
    const text = transcript.trim();
    if (!text || isProcessing) return;

    stopListening();
    setError('');
    setIsProcessing(true);
    try {
      const result = await askAI(text, conversationId);
      setResponse(result.response || 'Пустой ответ от модели.');
      if (result.conversation_id) {
        setConversationId(result.conversation_id);
      }

      if (autoSpeak && result.response) {
        if (voiceMode === 'server' && serverVoiceAvailable) {
          try {
            await speakResponseServer(result.response);
          } catch (err) {
            const detail = err instanceof Error ? err.message : String(err);
            if (isServerVoiceUnsupported(detail)) {
              downgradeToBrowserVoice(`Серверный голос отключен: ${detail}`);
            }
            speakResponseBrowser(result.response, () => {
              if (options?.resumeListening && voiceSupported) {
                startListening();
              }
            });
          }
        } else {
          speakResponseBrowser(result.response, () => {
            if (options?.resumeListening && voiceSupported) {
              startListening();
            }
          });
        }
      } else if (options?.resumeListening && voiceSupported) {
        startListening();
      }
    } catch (e) {
      const detail = e instanceof Error ? e.message : String(e);
      if (isServerVoiceUnsupported(detail)) {
        downgradeToBrowserVoice(`Серверный голос отключен: ${detail}`);
      }
      setError(detail);
    } finally {
      setIsProcessing(false);
    }
  };

  const startServerRecording = async () => {
    if (isProcessing || isRecordingServer) {
      return;
    }
    if (!serverVoiceAvailable) {
      setError('Серверный голос пока недоступен.');
      return;
    }
    if (!recorderSupported) {
      setError('Этот браузер не поддерживает MediaRecorder.');
      return;
    }
    setError('');

    try {
      const stream = await navigator.mediaDevices.getUserMedia({
        audio: {
          noiseSuppression: true,
          echoCancellation: true,
          autoGainControl: true,
        },
      });
      mediaStreamRef.current = stream;

      const preferredMime = [
        'audio/webm;codecs=opus',
        'audio/webm',
        'audio/mp4',
        'audio/ogg;codecs=opus',
      ];
      const pickedMime = preferredMime.find((value) => MediaRecorder.isTypeSupported(value));
      const recorder = pickedMime ? new MediaRecorder(stream, { mimeType: pickedMime }) : new MediaRecorder(stream);
      mediaRecorderRef.current = recorder;
      mediaChunksRef.current = [];

      recorder.ondataavailable = (event: BlobEvent) => {
        if (event.data && event.data.size > 0) {
          mediaChunksRef.current.push(event.data);
        }
      };

      recorder.onstop = () => {
        const blob = new Blob(mediaChunksRef.current, { type: recorder.mimeType || 'audio/webm' });
        mediaChunksRef.current = [];
        mediaStreamRef.current?.getTracks().forEach((track) => track.stop());
        mediaStreamRef.current = null;
        setIsRecordingServer(false);
        void sendServerTurn(blob, recorder.mimeType || 'audio/webm');
      };

      recorder.start(250);
      setIsRecordingServer(true);
    } catch (e) {
      mediaStreamRef.current?.getTracks().forEach((track) => track.stop());
      mediaStreamRef.current = null;
      setIsRecordingServer(false);
      setError(`Не удалось начать запись: ${e instanceof Error ? e.message : String(e)}`);
    }
  };

  const stopServerRecording = () => {
    try {
      const recorder = mediaRecorderRef.current;
      if (recorder && recorder.state !== 'inactive') {
        recorder.stop();
      } else {
        mediaStreamRef.current?.getTracks().forEach((track) => track.stop());
        mediaStreamRef.current = null;
        setIsRecordingServer(false);
      }
    } catch (e) {
      setIsRecordingServer(false);
      setError(`Ошибка остановки записи: ${e instanceof Error ? e.message : String(e)}`);
    }
  };

  const sendServerTurn = async (blob: Blob, mimeType: string) => {
    if (!serverVoiceAvailable || isProcessing) {
      return;
    }
    setError('');
    setIsProcessing(true);
    try {
      const audioBase64 = await blobToBase64(blob);
      const resp = await fetch('/api/v1/ai/voice/chat-turn', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          audio_base64: audioBase64,
          mime_type: mimeType,
          conversation_id: conversationId,
          temperature: 0.6,
          voice: serverVoiceName,
          audio_format: 'mp3',
          speak: autoSpeak,
        }),
      });
      if (!resp.ok) {
        const detail = await parseErrorDetail(resp, `Voice chat-turn error (${resp.status})`);
        throw new Error(detail);
      }

      const payload = (await resp.json()) as VoiceChatTurnResponse;
      setTranscript(payload.transcript || '');
      setResponse(payload.response || 'Пустой ответ от модели.');
      if (payload.conversation_id) {
        setConversationId(payload.conversation_id);
      }

      if (autoSpeak && payload.audio_base64 && payload.audio_mime) {
        await playServerAudio(payload.audio_base64, payload.audio_mime);
      } else if (autoSpeak && payload.response) {
        speakResponseBrowser(payload.response);
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setIsProcessing(false);
    }
  };

  const speakAnswer = async () => {
    const text = response.trim();
    if (!text) {
      return;
    }
    try {
      if (voiceMode === 'server' && serverVoiceAvailable) {
        try {
          await speakResponseServer(text);
          return;
        } catch (err) {
          const detail = err instanceof Error ? err.message : String(err);
          if (isServerVoiceUnsupported(detail)) {
            downgradeToBrowserVoice(`Серверный голос отключен: ${detail}`);
          }
          speakResponseBrowser(text);
          return;
        }
      }
      speakResponseBrowser(text);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  const statusText =
    voiceMode === 'server'
      ? isRecordingServer
        ? 'Запись...'
        : isProcessing
          ? 'Обработка...'
          : isSpeaking
            ? 'Озвучиваю...'
            : 'Ожидание'
      : isListening
        ? 'Слушаю...'
        : isProcessing
          ? 'Обработка...'
          : isSpeaking
            ? 'Озвучиваю...'
            : 'Ожидание';

  return (
    <div className="voice-tab">
      <div className="voice-head">
        <h1>Голос</h1>
        <div className={`voice-status ${isListening || isRecordingServer ? 'live' : ''}`}>
          <Mic size={14} />
          {statusText}
        </div>
      </div>

      <div className="voice-panel">
        <div className="voice-mode-switch" role="tablist" aria-label="Режим голоса">
          <button
            type="button"
            role="tab"
            className={`voice-mode-btn ${voiceMode === 'server' ? 'active' : ''}`}
            aria-selected={voiceMode === 'server'}
            onClick={() => setVoiceMode('server')}
            disabled={!serverVoiceAvailable}
          >
            Серверный
          </button>
          <button
            type="button"
            role="tab"
            className={`voice-mode-btn ${voiceMode === 'browser' ? 'active' : ''}`}
            aria-selected={voiceMode === 'browser'}
            onClick={() => setVoiceMode('browser')}
          >
            Браузерный
          </button>
        </div>

        <p className={`voice-mode-hint ${serverVoiceAvailable ? 'ok' : ''}`}>
          {serverVoiceAvailable
            ? `Серверный голос: ${serverVoiceDetail}`
            : `Серверный голос: ${serverVoiceDetail}`}
        </p>

        <textarea
          className="voice-input"
          value={transcript}
          onChange={(e) => setTranscript(e.target.value)}
          placeholder={
            voiceMode === 'server'
              ? 'Нажмите "Начать запись", скажите фразу, затем "Остановить и отправить"...'
              : 'Скажите команду голосом или введите текст вручную...'
          }
        />

        <div className="voice-actions">
          {voiceMode === 'server' ? (
            <>
              {!isRecordingServer ? (
                <button
                  type="button"
                  className="voice-btn primary"
                  onClick={() => void startServerRecording()}
                  disabled={!serverVoiceAvailable || !recorderSupported || isProcessing}
                >
                  <Mic size={16} />
                  Начать запись
                </button>
              ) : (
                <button type="button" className="voice-btn danger" onClick={stopServerRecording}>
                  <Square size={16} />
                  Остановить и отправить
                </button>
              )}
            </>
          ) : !isListening ? (
            <button type="button" className="voice-btn primary" onClick={startListening} disabled={!voiceSupported || isProcessing}>
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
            onClick={() => void sendTextCommand()}
            disabled={isProcessing || !transcript.trim()}
          >
            {isProcessing ? <Loader2 size={16} className="spin" /> : <Send size={16} />}
            Отправить текст
          </button>

          {voiceMode === 'browser' && (
            <button
              type="button"
              className="voice-btn"
              onClick={() => void sendTextCommand({ resumeListening: true })}
              disabled={isProcessing || !transcript.trim() || !voiceSupported}
            >
              <Mic size={16} />
              Отправить и слушать
            </button>
          )}

          <button
            type="button"
            className={`voice-btn ${autoSpeak ? 'primary' : ''}`}
            onClick={() => setAutoSpeak((v) => !v)}
            disabled={voiceMode === 'server' ? !serverVoiceAvailable : !speechSupported}
          >
            <Volume2 size={16} />
            {autoSpeak ? 'Автоозвучка ON' : 'Автоозвучка OFF'}
          </button>

          <button type="button" className="voice-btn" onClick={() => void speakAnswer()} disabled={!response.trim() || isSpeaking}>
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
          max-width: 980px;
          margin: 0 auto;
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
          font-size: 24px;
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

        .voice-mode-switch {
          display: grid;
          grid-template-columns: 1fr 1fr;
          gap: 6px;
          margin-bottom: 8px;
        }

        .voice-mode-btn {
          min-height: 34px;
          border: 1px solid var(--border-primary);
          border-radius: 9px;
          background: var(--bg-input);
          color: var(--text-muted);
          font-size: 13px;
          font-weight: 600;
          cursor: pointer;
          transition: all 0.15s;
        }

        .voice-mode-btn.active {
          border-color: var(--border-accent);
          background: var(--accent-bg);
          color: var(--accent-primary);
        }

        .voice-mode-btn:disabled {
          opacity: 0.45;
          cursor: not-allowed;
        }

        .voice-mode-hint {
          margin: 0 0 10px;
          font-size: 12px;
          color: var(--text-muted);
        }

        .voice-mode-hint.ok {
          color: var(--text-secondary);
        }

        .voice-input {
          width: 100%;
          min-height: 170px;
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
          display: grid;
          grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
          gap: 8px;
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

        @media (max-width: 700px) {
          .voice-tab {
            padding: 14px 12px 24px;
          }

          .voice-head {
            flex-direction: column;
            align-items: flex-start;
            gap: 8px;
          }

          .voice-head h1 {
            font-size: 26px;
          }

          .voice-panel,
          .voice-output {
            padding: 12px;
            border-radius: 12px;
          }

          .voice-input {
            font-size: 16px;
            min-height: 150px;
          }

          .voice-actions {
            grid-template-columns: 1fr;
          }

          .voice-btn {
            width: 100%;
            justify-content: center;
            min-height: 44px;
            font-size: 14px;
          }
        }
      `}</style>
    </div>
  );
};
