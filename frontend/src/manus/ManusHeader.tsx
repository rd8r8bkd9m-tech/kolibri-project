/**
 * ManusHeader.tsx
 * 
 * Минималистичный заголовок в стиле Manus AI.
 */

import { Menu, Trash2, Sparkles } from 'lucide-react';

interface ManusHeaderProps {
  onToggleSidebar: () => void;
  onClearHistory: () => void;
}

export const ManusHeader = ({ onToggleSidebar, onClearHistory }: ManusHeaderProps) => {
  return (
    <header className="manus-header">
      <div className="manus-header-left">
        <button 
          className="manus-icon-btn"
          onClick={onToggleSidebar}
          title="Переключить панель"
        >
          <Menu size={20} />
        </button>
        
        <div className="manus-logo">
          <Sparkles size={24} className="manus-logo-icon" />
          <span className="manus-logo-text">Колибри</span>
          <span className="manus-logo-badge">AI</span>
        </div>
      </div>

      <div className="manus-header-right">
        <button 
          className="manus-icon-btn manus-icon-btn-danger"
          onClick={onClearHistory}
          title="Очистить историю"
        >
          <Trash2 size={18} />
        </button>
      </div>

      <style>{`
        .manus-header {
          display: flex;
          align-items: center;
          justify-content: space-between;
          padding: 12px 20px;
          background: rgba(10, 10, 15, 0.8);
          backdrop-filter: blur(20px);
          border-bottom: 1px solid rgba(255, 255, 255, 0.06);
          z-index: 10;
        }

        .manus-header-left {
          display: flex;
          align-items: center;
          gap: 16px;
        }

        .manus-header-right {
          display: flex;
          align-items: center;
          gap: 8px;
        }

        .manus-logo {
          display: flex;
          align-items: center;
          gap: 8px;
        }

        .manus-logo-icon {
          color: #6366f1;
          animation: pulse 2s infinite;
        }

        .manus-logo-text {
          font-size: 20px;
          font-weight: 600;
          background: linear-gradient(135deg, #e4e4e7 0%, #a1a1aa 100%);
          -webkit-background-clip: text;
          -webkit-text-fill-color: transparent;
          background-clip: text;
        }

        .manus-logo-badge {
          font-size: 10px;
          font-weight: 600;
          padding: 2px 6px;
          background: linear-gradient(135deg, #6366f1 0%, #8b5cf6 100%);
          color: white;
          border-radius: 4px;
          text-transform: uppercase;
        }

        .manus-icon-btn {
          display: flex;
          align-items: center;
          justify-content: center;
          width: 36px;
          height: 36px;
          background: transparent;
          border: 1px solid rgba(255, 255, 255, 0.1);
          border-radius: 10px;
          color: #71717a;
          cursor: pointer;
          transition: all 0.2s ease;
        }

        .manus-icon-btn:hover {
          background: rgba(255, 255, 255, 0.05);
          border-color: rgba(255, 255, 255, 0.15);
          color: #e4e4e7;
        }

        .manus-icon-btn-danger:hover {
          background: rgba(239, 68, 68, 0.1);
          border-color: rgba(239, 68, 68, 0.3);
          color: #ef4444;
        }

        @keyframes pulse {
          0%, 100% { opacity: 1; }
          50% { opacity: 0.7; }
        }
      `}</style>
    </header>
  );
};

export default ManusHeader;
