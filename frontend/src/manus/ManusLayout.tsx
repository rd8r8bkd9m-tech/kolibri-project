/**
 * ManusLayout.tsx
 *
 * Основной layout Kolibri: рабочий sidebar, поиск по чатам, реальные записи истории.
 */

import { ReactNode, useEffect, useMemo, useState } from 'react';
import {
  Archive,
  ChevronLeft,
  ChevronRight,
  Database,
  Globe,
  ListTodo,
  Menu,
  MessageSquare,
  Mic,
  Search,
  Settings,
  Terminal as TerminalIcon,
  X,
  Zap,
} from 'lucide-react';

export type TabId =
  | 'chat'
  | 'voice'
  | 'tasks'
  | 'crawler'
  | 'knowledge'
  | 'archiver'
  | 'terminal'
  | 'settings';

export interface ChatHistoryItem {
  id: string;
  title: string;
  preview: string;
  updatedAt: number;
  unread?: boolean;
}

interface SidebarItem {
  id: TabId;
  label: string;
  icon: ReactNode;
}

const SIDEBAR_ITEMS: SidebarItem[] = [
  { id: 'chat', label: 'Чаты', icon: <MessageSquare size={18} /> },
  { id: 'voice', label: 'Голос', icon: <Mic size={18} /> },
  { id: 'tasks', label: 'Задачи', icon: <ListTodo size={18} /> },
  { id: 'crawler', label: 'Агент', icon: <Globe size={18} /> },
  { id: 'knowledge', label: 'Проекты', icon: <Database size={18} /> },
  { id: 'archiver', label: 'Calibre Архиватор', icon: <Archive size={18} /> },
  { id: 'terminal', label: 'Терминал', icon: <TerminalIcon size={18} /> },
  { id: 'settings', label: 'Настройки', icon: <Settings size={18} /> },
];

interface ManusLayoutProps {
  activeTab: TabId;
  onTabChange: (tab: TabId) => void;
  sidebarCollapsed: boolean;
  onToggleSidebar: () => void;
  onNewChat: () => void;
  chatHistory: ChatHistoryItem[];
  onOpenChat: (chatId: string) => void;
  children: ReactNode;
}

function formatHistoryTime(value: number): string {
  const dt = new Date(value);
  return dt.toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit' });
}

export const ManusLayout = ({
  activeTab,
  onTabChange,
  sidebarCollapsed,
  onToggleSidebar,
  onNewChat,
  chatHistory,
  onOpenChat,
  children,
}: ManusLayoutProps) => {
  const [searchText, setSearchText] = useState('');
  const [mobileMenuOpen, setMobileMenuOpen] = useState(false);

  useEffect(() => {
    if (typeof window === 'undefined' || typeof window.matchMedia !== 'function') return undefined;
    const media = window.matchMedia('(max-width: 760px)');
    const ensureExpandedOnMobile = () => {
      if (media.matches && sidebarCollapsed) {
        onToggleSidebar();
      }
    };
    ensureExpandedOnMobile();
    media.addEventListener('change', ensureExpandedOnMobile);
    return () => media.removeEventListener('change', ensureExpandedOnMobile);
  }, [sidebarCollapsed, onToggleSidebar]);

  useEffect(() => {
    if (!mobileMenuOpen || typeof window === 'undefined') return undefined;
    const handleEscape = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        setMobileMenuOpen(false);
      }
    };
    window.addEventListener('keydown', handleEscape);
    return () => window.removeEventListener('keydown', handleEscape);
  }, [mobileMenuOpen]);

  const visibleHistory = useMemo(() => {
    const q = searchText.trim().toLowerCase();
    if (!q) return chatHistory;
    return chatHistory.filter((item) => {
      return item.title.toLowerCase().includes(q) || item.preview.toLowerCase().includes(q);
    });
  }, [chatHistory, searchText]);

  const handleTabChange = (tab: TabId) => {
    onTabChange(tab);
    setMobileMenuOpen(false);
  };

  return (
    <div className="kolibri-layout">
      <aside className={`kolibri-sidebar ${sidebarCollapsed ? 'collapsed' : ''} ${mobileMenuOpen ? 'mobile-open' : ''}`}>
        <div className="kolibri-brand">
          <button type="button" className="kolibri-brand-icon" onClick={() => handleTabChange('chat')}>
            <Zap size={16} />
          </button>
          {!sidebarCollapsed && <span className="kolibri-brand-text">Kolibri</span>}
          {!sidebarCollapsed && (
            <button type="button" className="new-chat-btn" onClick={onNewChat}>
              Новый чат
            </button>
          )}
        </div>

        {!sidebarCollapsed && (
          <label className="kolibri-search">
            <Search size={16} />
            <input
              type="text"
              value={searchText}
              onChange={(e) => setSearchText(e.target.value)}
              placeholder="Поиск чатов"
            />
          </label>
        )}

        <nav className="kolibri-nav">
          {SIDEBAR_ITEMS.map((item) => (
            <button
              key={item.id}
              type="button"
              className={`kolibri-nav-item ${activeTab === item.id ? 'active' : ''}`}
              onClick={() => handleTabChange(item.id)}
              title={item.label}
            >
              <span className="kolibri-nav-icon">{item.icon}</span>
              {!sidebarCollapsed && <span className="kolibri-nav-label">{item.label}</span>}
            </button>
          ))}
        </nav>

        {!sidebarCollapsed && (
          <div className="kolibri-history">
            <div className="kolibri-history-title">Сегодня</div>
            {visibleHistory.length === 0 ? (
              <div className="history-empty">
                {searchText.trim() ? 'Нет совпадений' : 'Новых чатов пока нет'}
              </div>
            ) : (
              visibleHistory.slice(0, 24).map((chat) => (
                <button
                  key={chat.id}
                  type="button"
                  className="kolibri-history-item"
                  onClick={() => {
                    handleTabChange('chat');
                    onOpenChat(chat.id);
                    setMobileMenuOpen(false);
                  }}
                  title={chat.title}
                >
                  <span className="history-avatar">{chat.title.slice(0, 1).toUpperCase()}</span>
                  <span className="history-main">
                    <span className="history-name">{chat.title}</span>
                    <span className="history-preview">{chat.preview}</span>
                  </span>
                  <span className={`history-time ${chat.unread ? 'unread' : ''}`}>
                    {formatHistoryTime(chat.updatedAt)}
                  </span>
                </button>
              ))
            )}
          </div>
        )}

        <button type="button" className="kolibri-collapse" onClick={onToggleSidebar} title="Свернуть">
          {sidebarCollapsed ? <ChevronRight size={16} /> : <ChevronLeft size={16} />}
        </button>
      </aside>

      {mobileMenuOpen && (
        <button
          type="button"
          className="kolibri-mobile-overlay"
          aria-label="Закрыть меню"
          onClick={() => setMobileMenuOpen(false)}
        />
      )}

      <button
        type="button"
        className={`kolibri-mobile-toggle ${mobileMenuOpen ? 'open' : ''}`}
        aria-label={mobileMenuOpen ? 'Закрыть меню' : 'Открыть меню'}
        onClick={() => setMobileMenuOpen((prev) => !prev)}
      >
        {mobileMenuOpen ? <X size={16} /> : <Menu size={16} />}
      </button>

      <main className="kolibri-content">{children}</main>

      <style>{`
        .kolibri-layout {
          display: flex;
          width: 100%;
          height: 100vh;
          height: 100svh;
          position: relative;
          background: var(--bg-primary);
          color: var(--text-primary);
          font-family: 'Inter', 'SF Pro Text', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
          overflow: hidden;
        }

        .kolibri-sidebar {
          display: flex;
          flex-direction: column;
          width: clamp(260px, 20vw, 340px);
          background: var(--bg-secondary);
          border-right: 1px solid var(--border-primary);
          padding: 0;
          transition: width 0.2s ease, min-width 0.2s ease;
          flex-shrink: 0;
        }

        .kolibri-sidebar.collapsed {
          width: 72px;
          min-width: 72px;
        }

        .kolibri-brand {
          display: flex;
          align-items: center;
          gap: 10px;
          min-height: 52px;
          padding: 0 12px;
          border-bottom: 1px solid var(--border-primary);
        }

        .kolibri-brand-icon {
          width: 30px;
          height: 30px;
          border-radius: 8px;
          border: 1px solid var(--border-accent);
          background: var(--accent-bg);
          color: var(--accent-primary);
          display: inline-flex;
          align-items: center;
          justify-content: center;
          cursor: pointer;
        }

        .kolibri-brand-text {
          font-size: 14px;
          font-weight: 600;
          color: var(--text-primary);
          letter-spacing: 0.01em;
        }

        .new-chat-btn {
          margin-left: auto;
          height: 30px;
          border-radius: 8px;
          border: 1px solid var(--border-accent);
          background: var(--accent-bg);
          color: var(--accent-primary);
          font-size: 12px;
          font-weight: 600;
          padding: 0 10px;
          cursor: pointer;
        }

        .kolibri-search {
          min-height: 42px;
          margin: 10px 8px 6px;
          border: 1px solid var(--border-primary);
          border-radius: 999px;
          background: var(--bg-overlay);
          display: flex;
          align-items: center;
          gap: 8px;
          padding: 0 12px;
          color: var(--text-muted);
          font-size: 13px;
        }

        .kolibri-search input {
          width: 100%;
          border: 0;
          outline: 0;
          background: transparent;
          color: var(--text-primary);
          font-size: 13px;
        }

        .kolibri-search input::placeholder {
          color: var(--text-muted);
        }

        .kolibri-nav {
          flex: 0 0 auto;
          display: flex;
          flex-direction: column;
          padding: 8px;
          gap: 2px;
        }

        .kolibri-nav-item {
          width: 100%;
          min-height: 46px;
          border: 0;
          border-radius: 10px;
          background: transparent;
          color: var(--text-secondary);
          display: flex;
          align-items: center;
          gap: 10px;
          padding: 0 10px;
          text-align: left;
          font-size: 14px;
          font-weight: 500;
          cursor: pointer;
        }

        .kolibri-nav-item:hover {
          background: var(--bg-hover);
          color: var(--text-primary);
        }

        .kolibri-nav-item.active {
          background: var(--accent-bg);
          color: var(--accent-primary);
          border: 1px solid var(--border-accent);
        }

        .kolibri-nav-icon {
          width: 18px;
          height: 18px;
          display: inline-flex;
          align-items: center;
          justify-content: center;
          flex-shrink: 0;
        }

        .kolibri-nav-label {
          white-space: nowrap;
        }

        .kolibri-history {
          flex: 1;
          min-height: 0;
          overflow-y: auto;
          padding: 8px 8px 6px;
          border-top: 1px solid var(--border-primary);
        }

        .kolibri-history-title {
          font-size: 12px;
          color: var(--text-muted);
          padding: 4px 6px 8px;
        }

        .history-empty {
          font-size: 12px;
          color: var(--text-dimmed);
          padding: 8px 10px;
        }

        .kolibri-history-item {
          width: 100%;
          border: 0;
          background: transparent;
          display: grid;
          grid-template-columns: 30px minmax(0, 1fr) auto;
          align-items: center;
          gap: 8px;
          min-height: 44px;
          border-radius: 10px;
          padding: 4px 8px;
          color: var(--text-secondary);
          cursor: pointer;
          text-align: left;
        }

        .kolibri-history-item:hover {
          background: var(--bg-hover);
        }

        .history-avatar {
          width: 30px;
          height: 30px;
          border-radius: 999px;
          background: var(--bg-tertiary);
          border: 1px solid var(--border-primary);
          color: var(--text-secondary);
          font-size: 12px;
          font-weight: 600;
          display: inline-flex;
          align-items: center;
          justify-content: center;
        }

        .history-main {
          display: grid;
          min-width: 0;
          gap: 1px;
        }

        .history-name {
          overflow: hidden;
          white-space: nowrap;
          text-overflow: ellipsis;
          font-size: 12px;
          color: var(--text-primary);
        }

        .history-preview {
          overflow: hidden;
          white-space: nowrap;
          text-overflow: ellipsis;
          font-size: 11px;
          color: var(--text-dimmed);
        }

        .history-time {
          font-size: 11px;
          color: var(--text-dimmed);
          font-weight: 500;
          padding-left: 6px;
        }

        .history-time.unread {
          color: var(--accent-primary);
          font-weight: 700;
        }

        .kolibri-collapse {
          width: 100%;
          min-height: 42px;
          border: 0;
          border-top: 1px solid var(--border-primary);
          background: transparent;
          color: var(--text-muted);
          cursor: pointer;
          display: inline-flex;
          align-items: center;
          justify-content: center;
        }

        .kolibri-collapse:hover {
          background: var(--bg-hover);
          color: var(--text-primary);
        }

        .kolibri-content {
          flex: 1;
          min-width: 0;
          overflow: hidden;
          background: var(--bg-primary);
        }

        .kolibri-mobile-toggle {
          display: none;
        }

        .kolibri-mobile-overlay {
          display: none;
        }

        @media (max-width: 1024px) {
          .kolibri-sidebar {
            width: 260px;
            min-width: 260px;
          }
        }

        @media (max-width: 760px) {
          .kolibri-sidebar {
            position: fixed;
            inset: 0 auto 0 0;
            z-index: 40;
            width: min(92vw, 360px);
            max-width: 360px;
            height: 100%;
            border-bottom: 0;
            padding-top: env(safe-area-inset-top);
            transform: translateX(-100%);
            transition: transform 0.2s ease;
          }

          .kolibri-sidebar.mobile-open {
            transform: translateX(0);
          }

          .kolibri-sidebar.collapsed {
            width: min(92vw, 360px);
          }

          .kolibri-mobile-overlay {
            display: block;
            position: fixed;
            inset: 0;
            z-index: 30;
            border: 0;
            background: rgba(0, 0, 0, 0.6);
            cursor: pointer;
          }

          .kolibri-mobile-toggle {
            display: inline-flex;
            position: fixed;
            top: calc(10px + env(safe-area-inset-top));
            left: calc(10px + env(safe-area-inset-left));
            right: auto;
            z-index: 45;
            width: 34px;
            height: 34px;
            border-radius: 10px;
            border: 1px solid var(--border-primary);
            background: var(--bg-overlay);
            color: var(--text-secondary);
            align-items: center;
            justify-content: center;
            cursor: pointer;
          }

          .kolibri-mobile-toggle.open {
            border-color: var(--border-accent);
            color: var(--accent-primary);
            left: auto;
            right: calc(10px + env(safe-area-inset-right));
          }

          .kolibri-nav-item {
            min-height: 42px;
          }

          .kolibri-collapse {
            display: none;
          }
        }

        @media (max-width: 480px) {
          .kolibri-sidebar {
            width: 100vw;
            max-width: none;
          }
        }
      `}</style>
    </div>
  );
};
