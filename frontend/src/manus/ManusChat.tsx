/**
 * ManusChat.tsx
 * 
 * Чат-интерфейс с анимациями сообщений.
 */

import { useRef, useEffect } from 'react';
import { User, Bot, Copy, Check } from 'lucide-react';
import { useState } from 'react';
import type { Message } from './ManusApp';

interface ManusChatProps {
  messages: Message[];
  isThinking: boolean;
}

const MessageBubble = ({ message }: { message: Message }) => {
  const [copied, setCopied] = useState(false);
  const isUser = message.role === 'user';

  const handleCopy = async () => {
    await navigator.clipboard.writeText(message.content);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  return (
    <div className={`manus-message ${isUser ? 'manus-message-user' : 'manus-message-assistant'}`}>
      <div className="manus-message-avatar">
        {isUser ? (
          <User size={18} />
        ) : (
          <Bot size={18} />
        )}
      </div>
      
      <div className="manus-message-content">
        <div className="manus-message-header">
          <span className="manus-message-role">
            {isUser ? 'Вы' : 'Колибри AI'}
          </span>
          <span className="manus-message-time">
            {message.timestamp.toLocaleTimeString('ru-RU', { 
              hour: '2-digit', 
              minute: '2-digit' 
            })}
          </span>
        </div>
        
        <div className="manus-message-text">
          {message.content}
        </div>

        {!isUser && (
          <div className="manus-message-actions">
            <button 
              className="manus-message-action"
              onClick={handleCopy}
              title="Копировать"
            >
              {copied ? <Check size={14} /> : <Copy size={14} />}
              <span>{copied ? 'Скопировано' : 'Копировать'}</span>
            </button>
          </div>
        )}
      </div>

      <style>{`
        .manus-message {
          display: flex;
          gap: 16px;
          padding: 20px 24px;
          animation: messageIn 0.4s ease;
        }

        .manus-message-user {
          background: rgba(99, 102, 241, 0.05);
          border-radius: 16px;
          margin: 8px 0;
        }

        .manus-message-assistant {
          margin: 8px 0;
        }

        .manus-message-avatar {
          width: 36px;
          height: 36px;
          display: flex;
          align-items: center;
          justify-content: center;
          border-radius: 10px;
          flex-shrink: 0;
        }

        .manus-message-user .manus-message-avatar {
          background: linear-gradient(135deg, #6366f1 0%, #8b5cf6 100%);
          color: white;
        }

        .manus-message-assistant .manus-message-avatar {
          background: rgba(255, 255, 255, 0.1);
          color: #a1a1aa;
        }

        .manus-message-content {
          flex: 1;
          min-width: 0;
        }

        .manus-message-header {
          display: flex;
          align-items: center;
          gap: 12px;
          margin-bottom: 8px;
        }

        .manus-message-role {
          font-size: 14px;
          font-weight: 600;
          color: #e4e4e7;
        }

        .manus-message-time {
          font-size: 12px;
          color: #52525b;
        }

        .manus-message-text {
          font-size: 15px;
          line-height: 1.7;
          color: #d4d4d8;
          white-space: pre-wrap;
          word-break: break-word;
        }

        .manus-message-actions {
          display: flex;
          gap: 8px;
          margin-top: 12px;
          opacity: 0;
          transition: opacity 0.2s ease;
        }

        .manus-message:hover .manus-message-actions {
          opacity: 1;
        }

        .manus-message-action {
          display: flex;
          align-items: center;
          gap: 6px;
          padding: 6px 12px;
          background: rgba(255, 255, 255, 0.05);
          border: 1px solid rgba(255, 255, 255, 0.1);
          border-radius: 8px;
          color: #71717a;
          font-size: 12px;
          cursor: pointer;
          transition: all 0.2s ease;
        }

        .manus-message-action:hover {
          background: rgba(255, 255, 255, 0.1);
          color: #e4e4e7;
        }

        @keyframes messageIn {
          from {
            opacity: 0;
            transform: translateY(20px);
          }
          to {
            opacity: 1;
            transform: translateY(0);
          }
        }
      `}</style>
    </div>
  );
};

export const ManusChat = ({ messages, isThinking }: ManusChatProps) => {
  const scrollRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (scrollRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
  }, [messages, isThinking]);

  return (
    <div className="manus-chat" ref={scrollRef}>
      {messages.map((message) => (
        <MessageBubble key={message.id} message={message} />
      ))}

      <style>{`
        .manus-chat {
          flex: 1;
          overflow-y: auto;
          scroll-behavior: smooth;
        }

        .manus-chat::-webkit-scrollbar {
          width: 6px;
        }

        .manus-chat::-webkit-scrollbar-track {
          background: transparent;
        }

        .manus-chat::-webkit-scrollbar-thumb {
          background: rgba(255, 255, 255, 0.1);
          border-radius: 3px;
        }

        .manus-chat::-webkit-scrollbar-thumb:hover {
          background: rgba(255, 255, 255, 0.2);
        }
      `}</style>
    </div>
  );
};

export default ManusChat;
