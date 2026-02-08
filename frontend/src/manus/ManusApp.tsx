/**
 * ManusApp.tsx
 * 
 * Главный компонент интерфейса в стиле Manus AI.
 * Минималистичный дизайн с фокусом на взаимодействии с AI.
 */

import { useState, useCallback } from 'react';
import { ManusChat } from './ManusChat';
import { ManusTaskPanel } from './ManusTaskPanel';
import { ManusHeader } from './ManusHeader';
import { ManusInputBar } from './ManusInputBar';
import { ManusWelcome } from './ManusWelcome';
import { useManusAgent } from './useManusAgent';

export interface Task {
  id: string;
  title: string;
  status: 'pending' | 'running' | 'completed' | 'failed';
  progress?: number;
  startedAt?: Date;
  completedAt?: Date;
}

export interface Message {
  id: string;
  role: 'user' | 'assistant' | 'system';
  content: string;
  timestamp: Date;
  tasks?: Task[];
  thinking?: boolean;
}

export const ManusApp = () => {
  const [sidebarOpen, setSidebarOpen] = useState(true);
  const [inputValue, setInputValue] = useState('');
  
  const {
    messages,
    tasks,
    isProcessing,
    isThinking,
    currentPhase,
    sendMessage,
    stopGeneration,
    clearHistory,
  } = useManusAgent();

  const handleSend = useCallback(() => {
    if (!inputValue.trim() || isProcessing) return;
    sendMessage(inputValue);
    setInputValue('');
  }, [inputValue, isProcessing, sendMessage]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      handleSend();
    }
  }, [handleSend]);

  return (
    <div className="manus-app">
      {/* Фоновый градиент */}
      <div className="manus-bg" />
      
      {/* Заголовок */}
      <ManusHeader 
        onToggleSidebar={() => setSidebarOpen(!sidebarOpen)}
        onClearHistory={clearHistory}
      />

      {/* Основной контент */}
      <div className="manus-main">
        {/* Панель задач (слева) */}
        {sidebarOpen && (
          <ManusTaskPanel 
            tasks={tasks}
            currentPhase={currentPhase}
            isProcessing={isProcessing}
          />
        )}

        {/* Область чата */}
        <div className="manus-chat-area">
          {messages.length === 0 ? (
            <ManusWelcome onSuggestionClick={setInputValue} />
          ) : (
            <ManusChat 
              messages={messages}
              isThinking={isThinking}
            />
          )}

          {/* Индикатор "думает" */}
          {isThinking && (
            <div className="manus-thinking">
              <div className="manus-thinking-dots">
                <span />
                <span />
                <span />
              </div>
              <span className="manus-thinking-text">
                {currentPhase || 'Анализирую...'}
              </span>
            </div>
          )}
        </div>
      </div>

      {/* Нижняя панель ввода */}
      <ManusInputBar
        value={inputValue}
        onChange={setInputValue}
        onSend={handleSend}
        onKeyDown={handleKeyDown}
        onStop={stopGeneration}
        isProcessing={isProcessing}
        placeholder="Спросите что-нибудь или дайте задание..."
      />

      <style>{`
        .manus-app {
          display: flex;
          flex-direction: column;
          height: 100vh;
          width: 100vw;
          background: #0a0a0f;
          color: #e4e4e7;
          font-family: 'Inter', -apple-system, BlinkMacSystemFont, sans-serif;
          overflow: hidden;
        }

        .manus-bg {
          position: fixed;
          inset: 0;
          background: 
            radial-gradient(ellipse 80% 50% at 50% -20%, rgba(120, 119, 198, 0.15), transparent),
            radial-gradient(ellipse 60% 40% at 80% 50%, rgba(78, 81, 102, 0.1), transparent),
            radial-gradient(ellipse 50% 30% at 20% 80%, rgba(99, 102, 241, 0.08), transparent);
          pointer-events: none;
          z-index: 0;
        }

        .manus-main {
          display: flex;
          flex: 1;
          overflow: hidden;
          position: relative;
          z-index: 1;
        }

        .manus-chat-area {
          flex: 1;
          display: flex;
          flex-direction: column;
          overflow-y: auto;
          padding: 24px;
          padding-bottom: 120px;
        }

        .manus-thinking {
          display: flex;
          align-items: center;
          gap: 12px;
          padding: 16px 24px;
          margin: 8px 0;
          background: rgba(99, 102, 241, 0.1);
          border: 1px solid rgba(99, 102, 241, 0.2);
          border-radius: 16px;
          animation: fadeIn 0.3s ease;
        }

        .manus-thinking-dots {
          display: flex;
          gap: 4px;
        }

        .manus-thinking-dots span {
          width: 8px;
          height: 8px;
          background: #6366f1;
          border-radius: 50%;
          animation: bounce 1.4s infinite ease-in-out;
        }

        .manus-thinking-dots span:nth-child(1) { animation-delay: 0s; }
        .manus-thinking-dots span:nth-child(2) { animation-delay: 0.2s; }
        .manus-thinking-dots span:nth-child(3) { animation-delay: 0.4s; }

        .manus-thinking-text {
          color: #a5b4fc;
          font-size: 14px;
        }

        @keyframes bounce {
          0%, 80%, 100% { transform: scale(0.6); opacity: 0.5; }
          40% { transform: scale(1); opacity: 1; }
        }

        @keyframes fadeIn {
          from { opacity: 0; transform: translateY(10px); }
          to { opacity: 1; transform: translateY(0); }
        }
      `}</style>
    </div>
  );
};

export default ManusApp;
