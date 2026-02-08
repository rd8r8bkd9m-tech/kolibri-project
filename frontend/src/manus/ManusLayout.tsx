/**
 * ManusLayout.tsx
 * 
 * Единый layout для всего приложения в стиле Manus.
 * Все вкладки используют этот layout.
 */

import { ReactNode } from 'react';
import { 
  MessageSquare, 
  ListTodo, 
  Database, 
  Terminal as TerminalIcon, 
  Settings, 
  Zap,
  ChevronLeft,
  ChevronRight,
  Globe
} from 'lucide-react';

export type TabId = 'chat' | 'tasks' | 'crawler' | 'knowledge' | 'terminal' | 'settings';

interface Tab {
  id: TabId;
  label: string;
  icon: ReactNode;
}

const TABS: Tab[] = [
  { id: 'chat', label: 'Чат', icon: <MessageSquare size={20} /> },
  { id: 'crawler', label: 'AI Агент', icon: <Globe size={20} /> },
  { id: 'tasks', label: 'Задачи', icon: <ListTodo size={20} /> },
  { id: 'knowledge', label: 'Знания', icon: <Database size={20} /> },
  { id: 'terminal', label: 'Терминал', icon: <TerminalIcon size={20} /> },
  { id: 'settings', label: 'Настройки', icon: <Settings size={20} /> },
];

interface ManusLayoutProps {
  activeTab: TabId;
  onTabChange: (tab: TabId) => void;
  sidebarCollapsed: boolean;
  onToggleSidebar: () => void;
  children: ReactNode;
}

export const ManusLayout = ({
  activeTab,
  onTabChange,
  sidebarCollapsed,
  onToggleSidebar,
  children
}: ManusLayoutProps) => {
  return (
    <div className="manus-layout">
      {/* Фоновый градиент */}
      <div className="manus-bg-gradient" />
      
      {/* Боковая навигация */}
      <nav className={`manus-sidebar ${sidebarCollapsed ? 'collapsed' : ''}`}>
        <div className="manus-sidebar-header">
          <div className="manus-logo">
            <Zap className="manus-logo-icon" />
            {!sidebarCollapsed && <span className="manus-logo-text">Kolibri</span>}
          </div>
        </div>

        <div className="manus-nav-items">
          {TABS.map(tab => (
            <button
              key={tab.id}
              className={`manus-nav-item ${activeTab === tab.id ? 'active' : ''}`}
              onClick={() => onTabChange(tab.id)}
              title={sidebarCollapsed ? tab.label : undefined}
            >
              <span className="manus-nav-icon">{tab.icon}</span>
              {!sidebarCollapsed && <span className="manus-nav-label">{tab.label}</span>}
              {activeTab === tab.id && <div className="manus-nav-indicator" />}
            </button>
          ))}
        </div>

        <button className="manus-sidebar-toggle" onClick={onToggleSidebar}>
          {sidebarCollapsed ? <ChevronRight size={16} /> : <ChevronLeft size={16} />}
        </button>
      </nav>

      {/* Основной контент */}
      <main className="manus-content">
        {children}
      </main>

      <style>{`
        .manus-layout {
          display: flex;
          height: 100vh;
          width: 100vw;
          background: var(--bg-primary, #09090b);
          color: var(--text-primary, #fafafa);
          font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
          overflow: hidden;
        }

        .manus-bg-gradient {
          position: fixed;
          inset: 0;
          background: var(--gradient-bg);
          pointer-events: none;
          z-index: 0;
        }

        .manus-sidebar {
          display: flex;
          flex-direction: column;
          width: 240px;
          background: var(--bg-secondary, rgba(24, 24, 27, 0.6));
          backdrop-filter: blur(20px);
          border-right: 1px solid var(--border-primary, rgba(255, 255, 255, 0.06));
          z-index: 10;
          transition: width 0.2s ease;
          position: relative;
        }

        .manus-sidebar.collapsed {
          width: 64px;
        }

        .manus-sidebar-header {
          padding: 20px 16px;
          border-bottom: 1px solid var(--border-primary, rgba(255, 255, 255, 0.06));
        }

        .manus-logo {
          display: flex;
          align-items: center;
          gap: 12px;
        }

        .manus-logo-icon {
          width: 28px;
          height: 28px;
          color: var(--accent-primary, #818cf8);
          flex-shrink: 0;
        }

        .manus-logo-text {
          font-size: 18px;
          font-weight: 600;
          background: var(--accent-gradient-text, linear-gradient(135deg, #818cf8, #c084fc));
          -webkit-background-clip: text;
          -webkit-text-fill-color: transparent;
          white-space: nowrap;
        }

        .manus-nav-items {
          flex: 1;
          padding: 12px 8px;
          display: flex;
          flex-direction: column;
          gap: 4px;
        }

        .manus-nav-item {
          display: flex;
          align-items: center;
          gap: 12px;
          padding: 12px;
          border-radius: 10px;
          background: transparent;
          border: none;
          color: var(--text-secondary, #a1a1aa);
          cursor: pointer;
          transition: all 0.15s ease;
          position: relative;
          text-align: left;
          width: 100%;
        }

        .manus-nav-item:hover {
          background: var(--bg-hover, rgba(255, 255, 255, 0.05));
          color: var(--text-primary, #fafafa);
        }

        .manus-nav-item.active {
          background: var(--accent-bg, rgba(99, 102, 241, 0.15));
          color: var(--accent-primary, #818cf8);
        }

        .manus-nav-icon {
          display: flex;
          align-items: center;
          justify-content: center;
          flex-shrink: 0;
        }

        .manus-nav-label {
          font-size: 14px;
          font-weight: 500;
          white-space: nowrap;
        }

        .manus-nav-indicator {
          position: absolute;
          left: 0;
          top: 50%;
          transform: translateY(-50%);
          width: 3px;
          height: 20px;
          background: linear-gradient(180deg, var(--accent-primary, #818cf8), var(--accent-secondary, #c084fc));
          border-radius: 0 3px 3px 0;
        }

        .manus-sidebar-toggle {
          position: absolute;
          right: -12px;
          top: 50%;
          transform: translateY(-50%);
          width: 24px;
          height: 24px;
          border-radius: 50%;
          background: var(--bg-tertiary, #27272a);
          border: 1px solid var(--border-hover, rgba(255, 255, 255, 0.1));
          color: var(--text-secondary, #a1a1aa);
          cursor: pointer;
          display: flex;
          align-items: center;
          justify-content: center;
          transition: all 0.15s ease;
          z-index: 20;
        }

        .manus-sidebar-toggle:hover {
          background: var(--bg-hover, #3f3f46);
          color: var(--text-primary, #fafafa);
        }

        .manus-content {
          flex: 1;
          display: flex;
          flex-direction: column;
          overflow: hidden;
          z-index: 1;
          position: relative;
        }
      `}</style>
    </div>
  );
};
