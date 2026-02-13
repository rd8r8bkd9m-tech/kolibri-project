/**
 * ManusWelcome.tsx
 * 
 * Экран приветствия с предложениями для пользователя.
 * Отображается когда нет активного диалога.
 */

import { Sparkles, Code, FileText, Lightbulb, Zap, Brain } from 'lucide-react';

interface Suggestion {
  icon: React.ReactNode;
  title: string;
  description: string;
  prompt: string;
}

interface ManusWelcomeProps {
  onSuggestionClick: (prompt: string) => void;
}

const suggestions: Suggestion[] = [
  {
    icon: <Code size={20} />,
    title: 'Анализ кода',
    description: 'Проверить и улучшить структуру проекта',
    prompt: 'Проанализируй архитектуру проекта и предложи улучшения',
  },
  {
    icon: <FileText size={20} />,
    title: 'Документация',
    description: 'Создать README или описание API',
    prompt: 'Создай подробную документацию для текущего проекта',
  },
  {
    icon: <Lightbulb size={20} />,
    title: 'Идеи',
    description: 'Получить креативные предложения',
    prompt: 'Предложи новые фичи для улучшения продукта',
  },
  {
    icon: <Zap size={20} />,
    title: 'Оптимизация',
    description: 'Ускорить работу приложения',
    prompt: 'Найди узкие места в производительности и оптимизируй их',
  },
  {
    icon: <Brain size={20} />,
    title: 'AI Агент',
    description: 'Запустить автономную задачу',
    prompt: 'Исследуй кодовую базу и создай план развития',
  },
  {
    icon: <Sparkles size={20} />,
    title: 'Рефакторинг',
    description: 'Улучшить качество кода',
    prompt: 'Проведи рефакторинг с применением лучших практик',
  },
];

export const ManusWelcome = ({ onSuggestionClick }: ManusWelcomeProps) => {
  return (
    <div className="manus-welcome">
      <div className="manus-welcome-content">
        <div className="manus-welcome-logo">
          <div className="manus-welcome-icon">
            <Sparkles size={48} />
          </div>
        </div>

        <h1 className="manus-welcome-title">
          Привет! Я Колибри AI
        </h1>
        
        <p className="manus-welcome-subtitle">
          Ваш интеллектуальный ассистент для разработки.<br />
          Чем могу помочь сегодня?
        </p>

        <div className="manus-welcome-suggestions">
          {suggestions.map((suggestion, index) => (
            <button
              key={index}
              className="manus-suggestion-card"
              onClick={() => onSuggestionClick(suggestion.prompt)}
              style={{ animationDelay: `${index * 0.1}s` }}
            >
              <div className="manus-suggestion-icon">
                {suggestion.icon}
              </div>
              <div className="manus-suggestion-text">
                <span className="manus-suggestion-title">{suggestion.title}</span>
                <span className="manus-suggestion-desc">{suggestion.description}</span>
              </div>
            </button>
          ))}
        </div>

        <div className="manus-welcome-features">
          <div className="manus-feature">
            <span className="manus-feature-dot" />
            Автономная работа
          </div>
          <div className="manus-feature">
            <span className="manus-feature-dot" />
            64-битные геномы
          </div>
          <div className="manus-feature">
            <span className="manus-feature-dot" />
            Number-Thinking
          </div>
        </div>
      </div>

      <style>{`
        .manus-welcome {
          display: flex;
          align-items: center;
          justify-content: center;
          min-height: 100%;
          padding: 48px 24px 120px;
          animation: fadeIn 0.6s ease;
        }

        .manus-welcome-content {
          max-width: 700px;
          text-align: center;
        }

        .manus-welcome-logo {
          display: flex;
          justify-content: center;
          margin-bottom: 24px;
        }

        .manus-welcome-icon {
          display: flex;
          align-items: center;
          justify-content: center;
          width: 96px;
          height: 96px;
          background: linear-gradient(135deg, rgba(99, 102, 241, 0.2) 0%, rgba(139, 92, 246, 0.2) 100%);
          border: 1px solid rgba(99, 102, 241, 0.3);
          border-radius: 28px;
          color: #a78bfa;
          animation: float 3s ease-in-out infinite;
        }

        .manus-welcome-title {
          font-size: 36px;
          font-weight: 700;
          background: linear-gradient(135deg, #e4e4e7 0%, #a1a1aa 100%);
          -webkit-background-clip: text;
          -webkit-text-fill-color: transparent;
          background-clip: text;
          margin: 0 0 12px;
          letter-spacing: -0.02em;
        }

        .manus-welcome-subtitle {
          font-size: 16px;
          color: #71717a;
          line-height: 1.6;
          margin: 0 0 40px;
        }

        .manus-welcome-suggestions {
          display: grid;
          grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
          gap: 12px;
          margin-bottom: 48px;
        }

        .manus-suggestion-card {
          display: flex;
          align-items: center;
          gap: 12px;
          padding: 16px;
          background: rgba(25, 25, 35, 0.6);
          border: 1px solid rgba(255, 255, 255, 0.06);
          border-radius: 14px;
          cursor: pointer;
          transition: all 0.2s ease;
          text-align: left;
          animation: slideUp 0.5s ease both;
        }

        .manus-suggestion-card:hover {
          background: rgba(99, 102, 241, 0.1);
          border-color: rgba(99, 102, 241, 0.2);
          transform: translateY(-2px);
        }

        .manus-suggestion-icon {
          display: flex;
          align-items: center;
          justify-content: center;
          width: 40px;
          height: 40px;
          background: rgba(99, 102, 241, 0.1);
          border-radius: 10px;
          color: #818cf8;
          flex-shrink: 0;
        }

        .manus-suggestion-text {
          display: flex;
          flex-direction: column;
          gap: 2px;
          overflow: hidden;
        }

        .manus-suggestion-title {
          font-size: 14px;
          font-weight: 600;
          color: #e4e4e7;
        }

        .manus-suggestion-desc {
          font-size: 12px;
          color: #52525b;
          white-space: nowrap;
          overflow: hidden;
          text-overflow: ellipsis;
        }

        .manus-welcome-features {
          display: flex;
          justify-content: center;
          gap: 24px;
          flex-wrap: wrap;
        }

        .manus-feature {
          display: flex;
          align-items: center;
          gap: 8px;
          font-size: 13px;
          color: #52525b;
        }

        .manus-feature-dot {
          width: 6px;
          height: 6px;
          background: linear-gradient(135deg, #6366f1 0%, #8b5cf6 100%);
          border-radius: 50%;
        }

        @keyframes float {
          0%, 100% { transform: translateY(0); }
          50% { transform: translateY(-10px); }
        }

        @keyframes fadeIn {
          from { opacity: 0; }
          to { opacity: 1; }
        }

        @keyframes slideUp {
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

export default ManusWelcome;
