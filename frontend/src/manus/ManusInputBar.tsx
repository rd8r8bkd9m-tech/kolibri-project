/**
 * ManusInputBar.tsx
 * 
 * Панель ввода сообщений в стиле Manus AI.
 * Закреплена внизу экрана с градиентным фоном.
 */

import { Send, Square, Paperclip, Mic } from 'lucide-react';

interface ManusInputBarProps {
  value: string;
  onChange: (value: string) => void;
  onSend: () => void;
  onKeyDown: (e: React.KeyboardEvent) => void;
  onStop: () => void;
  isProcessing: boolean;
  placeholder?: string;
}

export const ManusInputBar = ({
  value,
  onChange,
  onSend,
  onKeyDown,
  onStop,
  isProcessing,
  placeholder = 'Введите сообщение...',
}: ManusInputBarProps) => {
  return (
    <div className="manus-input-container">
      <div className="manus-input-bar">
        <div className="manus-input-left">
          <button className="manus-input-action" title="Прикрепить файл">
            <Paperclip size={18} />
          </button>
        </div>

        <div className="manus-input-wrapper">
          <textarea
            className="manus-input"
            value={value}
            onChange={(e) => onChange(e.target.value)}
            onKeyDown={onKeyDown}
            placeholder={placeholder}
            rows={1}
            disabled={isProcessing}
          />
        </div>

        <div className="manus-input-right">
          <button className="manus-input-action" title="Голосовой ввод">
            <Mic size={18} />
          </button>
          
          {isProcessing ? (
            <button 
              className="manus-input-stop"
              onClick={onStop}
              title="Остановить"
            >
              <Square size={16} />
            </button>
          ) : (
            <button 
              className="manus-input-send"
              onClick={onSend}
              disabled={!value.trim()}
              title="Отправить"
            >
              <Send size={18} />
            </button>
          )}
        </div>
      </div>

      <div className="manus-input-hint">
        <span>Enter</span> — отправить · <span>Shift + Enter</span> — новая строка
      </div>

      <style>{`
        .manus-input-container {
          position: fixed;
          bottom: 0;
          left: 0;
          right: 0;
          padding: 16px 24px 24px;
          background: linear-gradient(to top, rgba(10, 10, 15, 1) 0%, rgba(10, 10, 15, 0.95) 50%, transparent 100%);
          z-index: 20;
        }

        .manus-input-bar {
          display: flex;
          align-items: flex-end;
          gap: 12px;
          max-width: 900px;
          margin: 0 auto;
          padding: 12px 16px;
          background: rgba(25, 25, 35, 0.9);
          border: 1px solid rgba(255, 255, 255, 0.1);
          border-radius: 20px;
          backdrop-filter: blur(20px);
          box-shadow: 
            0 0 0 1px rgba(255, 255, 255, 0.05),
            0 20px 50px -10px rgba(0, 0, 0, 0.5);
        }

        .manus-input-left,
        .manus-input-right {
          display: flex;
          align-items: center;
          gap: 4px;
        }

        .manus-input-wrapper {
          flex: 1;
        }

        .manus-input {
          width: 100%;
          background: transparent;
          border: none;
          outline: none;
          color: #e4e4e7;
          font-size: 15px;
          line-height: 1.5;
          resize: none;
          max-height: 150px;
          font-family: inherit;
        }

        .manus-input::placeholder {
          color: #52525b;
        }

        .manus-input:disabled {
          opacity: 0.5;
        }

        .manus-input-action {
          display: flex;
          align-items: center;
          justify-content: center;
          width: 36px;
          height: 36px;
          background: transparent;
          border: none;
          border-radius: 10px;
          color: #52525b;
          cursor: pointer;
          transition: all 0.2s ease;
        }

        .manus-input-action:hover {
          background: rgba(255, 255, 255, 0.05);
          color: #a1a1aa;
        }

        .manus-input-send {
          display: flex;
          align-items: center;
          justify-content: center;
          width: 40px;
          height: 40px;
          background: linear-gradient(135deg, #6366f1 0%, #8b5cf6 100%);
          border: none;
          border-radius: 12px;
          color: white;
          cursor: pointer;
          transition: all 0.2s ease;
        }

        .manus-input-send:hover:not(:disabled) {
          transform: scale(1.05);
          box-shadow: 0 4px 20px rgba(99, 102, 241, 0.4);
        }

        .manus-input-send:disabled {
          opacity: 0.3;
          cursor: not-allowed;
        }

        .manus-input-stop {
          display: flex;
          align-items: center;
          justify-content: center;
          width: 40px;
          height: 40px;
          background: rgba(239, 68, 68, 0.2);
          border: 1px solid rgba(239, 68, 68, 0.3);
          border-radius: 12px;
          color: #ef4444;
          cursor: pointer;
          transition: all 0.2s ease;
          animation: pulse 1s infinite;
        }

        .manus-input-stop:hover {
          background: rgba(239, 68, 68, 0.3);
        }

        .manus-input-hint {
          text-align: center;
          margin-top: 12px;
          font-size: 12px;
          color: #3f3f46;
        }

        .manus-input-hint span {
          display: inline-block;
          padding: 2px 6px;
          background: rgba(255, 255, 255, 0.05);
          border-radius: 4px;
          font-family: monospace;
        }

        @keyframes pulse {
          0%, 100% { opacity: 1; }
          50% { opacity: 0.7; }
        }
      `}</style>
    </div>
  );
};

export default ManusInputBar;
