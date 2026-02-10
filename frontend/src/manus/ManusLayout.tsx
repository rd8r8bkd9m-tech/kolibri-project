/**
 * ManusLayout.tsx
 * 
 * Layout в стиле Manus.im — чистый, светлый, минимальный.
 * Левый сайдбар + основной контент.
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
  Globe,
  Archive
} from 'lucide-react';

export type TabId = 'chat' | 'tasks' | 'crawler' | 'knowledge' | 'archiver' | 'terminal' | 'settings';

interface Tab {
  id: TabId;
  label: string;
  icon: ReactNode;
}

const TABS: Tab[] = [
  { id: 'chat', label: 'Чат', icon: <MessageSquare size={18} /> },
  { id: 'crawler', label: 'AI Агент', icon: <Globe size={18} /> },
  { id: 'tasks', label: 'Задачи', icon: <ListTodo size={18} /> },
  { id: 'knowledge', label: 'Знания', icon: <Database size={18} /> },
  { id: 'archiver', label: 'Архиватор', icon: <Archive size={18} /> },
  { id: 'terminal', label: 'Терминал', icon: <TerminalIcon size={18} /> },
  { id: 'settings', label: 'Настройки', icon: <Settings size={18} /> },
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
      {/* Боковая навигация */}
      <nav className={`manus-sidebar ${sidebarCollapsed ? 'collapsed' : ''}`}>
        <div className="manus-sidebar-header">
          <div className="manus-logo">
            <div className="manus-logo-icon">
              <Zap size={16} />
            </div>
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
            </button>
          ))}
        </div>

        <button className="manus-sidebar-toggle" onClick={onToggleSidebar}>
          {sidebarCollapsed ? <ChevronRight size={14} /> : <ChevronLeft size={14} />}
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
          background: var(--bg-primary);
          color: var(--text-primary);
          font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
          overflow: hidden;
        }

        .manus-sidebar {
          display: flex;
          flex-direction: column;
          width: 220px;
          background: var(--bg-secondary);
          border-right: 1px solid var(--border-primary);
          z-index: 10;
          transition: width 0.2s ease;
          position: relative;
          flex-shrink: 0;
        }

        .manus-sidebar.collapsed {
          width: 60px;
        }

        .manus-sidebar-header {
          padding: 18px 16px;
          border-bottom: 1px solid var(--border-primary);
        }

        .manus-logo {
          display: flex;
          align-items: center;
          gap: 10px;
        }

        .manus-logo-icon {
          width: 28px;
          height: 28px;
          border-radius: 8px;
          background: var(--accent-primary);
          color: var(--bg-primary);
          display: flex;
          align-items: center;
          justify-content: center;
          flex-shrink: 0;
        }

        .manus-logo-text {
          font-size: 16px;
          font-weight: 600;
          color: var(--text-primary);
          white-space: nowrap;
        }

        .manus-nav-items {
          flex: 1;
          padding: 10px 8px;
          display: flex;
          flex-direction: column;
          gap: 2px;
        }

        .manus-nav-item {
          display: flex;
          align-items: center;
          gap: 10px;
          padding: 10px 12px;
          border-radius: 8px;
          background: transparent;
          border: none;
          color: var(--text-secondary);
          cursor: pointer;
          transition: all 0.15s ease;
          text-align: left;
          width: 100%;
          font-size: 13px;
          font-weight: 400;
        }

        .manus-nav-item:hover {
          background: var(--bg-hover);
          color: var(--text-primary);
        }

        .manus-nav-item.active {
          background: var(--accent-bg);
          color: var(--accent-primary);
          font-weight: 500;
        }

        .manus-nav-icon {
          display: flex;
          align-items: center;
          justify-content: center;
          flex-shrink: 0;
          width: 20px;
        }

        .manus-nav-label {
          white-space: nowrap;
        }

        .manus-sidebar-toggle {
          position: absolute;
          right: -12px;
          top: 50%;
          transform: translateY(-50%);
          width: 24px;
          height: 24px;
          border-radius: 50%;
          background: var(--bg-secondary);
          border: 1px solid var(--border-primary);
          color: var(--text-muted);
          cursor: pointer;
          display: flex;
          align-items: center;
          justify-content: center;
          transition: all 0.15s ease;
          z-index: 20;
          box-shadow: var(--shadow-card);
        }

        .manus-sidebar-toggle:hover {
          background: var(--bg-hover);
          color: var(--text-primary);
        }

        .manus-content {
          flex: 1;
          display: flex;
          flex-direction: column;
          overflow: hidden;
          z-index: 1;
          position: relative;
          min-width: 0;
        }
      `}</style>
    </div>
  );
};
