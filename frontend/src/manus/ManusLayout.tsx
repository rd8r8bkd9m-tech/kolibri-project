/**
 * ManusLayout.tsx
 *
 * Unified app shell inspired by Grok/GPT information architecture:
 * sidebar + chat-first canvas + responsive mobile off-canvas navigation.
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
  Plus,
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
  { id: 'crawler', label: 'AI Агент', icon: <Globe size={18} /> },
  { id: 'knowledge', label: 'Знания', icon: <Database size={18} /> },
  { id: 'archiver', label: 'Архиватор', icon: <Archive size={18} /> },
  { id: 'terminal', label: 'Терминал', icon: <TerminalIcon size={18} /> },
  { id: 'settings', label: 'Настройки', icon: <Settings size={18} /> },
];

const MOBILE_PRIMARY_ITEMS: SidebarItem[] = [
  { id: 'chat', label: 'Чат', icon: <MessageSquare size={18} /> },
  { id: 'crawler', label: 'Агент', icon: <Globe size={18} /> },
  { id: 'voice', label: 'Голос', icon: <Mic size={18} /> },
  { id: 'archiver', label: 'Архив', icon: <Archive size={18} /> },
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

const formatHistoryTime = (value: number): string => {
  const date = new Date(value);
  return date.toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit' });
};

const isMobileViewport = (): boolean => {
  if (typeof window === 'undefined' || typeof window.matchMedia !== 'function') {
    return false;
  }
  return window.matchMedia('(max-width: 900px)').matches;
};

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
  const [mobileViewport, setMobileViewport] = useState(isMobileViewport);

  useEffect(() => {
    if (typeof window === 'undefined') {
      return undefined;
    }

    if (typeof window.matchMedia !== 'function') {
      setMobileViewport(false);
      return undefined;
    }

    const media = window.matchMedia('(max-width: 900px)');
    const syncViewport = () => {
      setMobileViewport(media.matches);
      if (!media.matches) {
        setMobileMenuOpen(false);
      }
    };

    syncViewport();

    if (typeof media.addEventListener === 'function') {
      media.addEventListener('change', syncViewport);
      return () => media.removeEventListener('change', syncViewport);
    }

    media.addListener(syncViewport);
    return () => media.removeListener(syncViewport);
  }, []);

  useEffect(() => {
    if (!mobileMenuOpen || typeof window === 'undefined') {
      return undefined;
    }

    const onEscape = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        setMobileMenuOpen(false);
      }
    };

    window.addEventListener('keydown', onEscape);
    return () => window.removeEventListener('keydown', onEscape);
  }, [mobileMenuOpen]);

  const visibleHistory = useMemo(() => {
    const query = searchText.trim().toLowerCase();
    if (!query) {
      return chatHistory;
    }
    return chatHistory.filter((item) => {
      return item.title.toLowerCase().includes(query) || item.preview.toLowerCase().includes(query);
    });
  }, [chatHistory, searchText]);

  const handleTabChange = (tab: TabId) => {
    onTabChange(tab);
    if (mobileViewport) {
      setMobileMenuOpen(false);
    }
  };

  const showExpandedSidebar = !sidebarCollapsed || mobileViewport;
  const activeTabLabel = SIDEBAR_ITEMS.find((item) => item.id === activeTab)?.label || 'Раздел';

  return (
    <div className="gx-shell">
      <aside
        className={`gx-sidebar ${sidebarCollapsed ? 'is-collapsed' : ''} ${mobileMenuOpen ? 'is-mobile-open' : ''}`}
      >
        <div className="gx-sidebar-top">
          <div className="gx-brand-row">
            <button type="button" className="gx-brand" onClick={() => handleTabChange('chat')}>
              <span className="gx-brand-badge" aria-hidden="true">
                <Zap size={14} />
              </span>
              {showExpandedSidebar && (
                <span className="gx-brand-label-wrap">
                  <span className="gx-brand-title">Kolibri</span>
                  <span className="gx-brand-subtitle">Assistant Workspace</span>
                </span>
              )}
            </button>

            {!mobileViewport && (
              <button
                type="button"
                className="gx-collapse-btn"
                onClick={onToggleSidebar}
                aria-label={sidebarCollapsed ? 'Развернуть сайдбар' : 'Свернуть сайдбар'}
              >
                {sidebarCollapsed ? <ChevronRight size={16} /> : <ChevronLeft size={16} />}
              </button>
            )}
          </div>

          {showExpandedSidebar && (
            <>
              <button type="button" className="gx-new-chat" onClick={onNewChat}>
                <Plus size={16} />
                <span>Новый чат</span>
              </button>

              <label className="gx-search" aria-label="Поиск чатов">
                <Search size={15} />
                <input
                  type="text"
                  value={searchText}
                  onChange={(event) => setSearchText(event.target.value)}
                  placeholder="Поиск по истории"
                />
              </label>
            </>
          )}
        </div>

        <nav className="gx-nav" aria-label="Разделы">
          {SIDEBAR_ITEMS.map((item) => (
            <button
              key={item.id}
              type="button"
              className={`gx-nav-item ${activeTab === item.id ? 'is-active' : ''}`}
              onClick={() => handleTabChange(item.id)}
              title={item.label}
            >
              <span className="gx-nav-icon" aria-hidden="true">
                {item.icon}
              </span>
              {showExpandedSidebar && <span className="gx-nav-label">{item.label}</span>}
            </button>
          ))}
        </nav>

        {showExpandedSidebar && (
          <section className="gx-history" aria-label="История чатов">
            <div className="gx-history-header">
              <span>История</span>
              <span>{visibleHistory.length}</span>
            </div>

            {visibleHistory.length === 0 ? (
              <p className="gx-history-empty">
                {searchText.trim() ? 'Совпадений не найдено' : 'Начните новый диалог'}
              </p>
            ) : (
              <div className="gx-history-list">
                {visibleHistory.slice(0, 40).map((chat) => (
                  <button
                    key={chat.id}
                    type="button"
                    className="gx-history-item"
                    onClick={() => {
                      onOpenChat(chat.id);
                      handleTabChange('chat');
                    }}
                    title={chat.title}
                  >
                    <span className="gx-history-avatar">{chat.title.slice(0, 1).toUpperCase()}</span>
                    <span className="gx-history-main">
                      <span className="gx-history-title">{chat.title}</span>
                      <span className="gx-history-preview">{chat.preview || 'Без текста'}</span>
                    </span>
                    <span className={`gx-history-time ${chat.unread ? 'is-unread' : ''}`}>
                      {formatHistoryTime(chat.updatedAt)}
                    </span>
                  </button>
                ))}
              </div>
            )}
          </section>
        )}
      </aside>

      {mobileMenuOpen && (
        <button
          type="button"
          className="gx-mobile-overlay"
          onClick={() => setMobileMenuOpen(false)}
          aria-label="Закрыть меню"
        />
      )}

      <main className="gx-content">
        {mobileViewport && (
          <div className="gx-mobile-topbar">
            <button
              type="button"
              className="gx-mobile-top-btn"
              onClick={() => setMobileMenuOpen(true)}
              aria-label="Открыть меню"
            >
              <Menu size={19} />
            </button>
            <button type="button" className="gx-mobile-top-brand" onClick={() => handleTabChange('chat')}>
              <span className="gx-mobile-top-brand-title">Kolibri</span>
              <span className="gx-mobile-top-brand-subtitle">{activeTabLabel}</span>
            </button>
            <button type="button" className="gx-mobile-top-btn is-accent" onClick={onNewChat} aria-label="Новый чат">
              <Plus size={18} />
            </button>
          </div>
        )}
        <div className="gx-content-body">{children}</div>
      </main>

      {mobileViewport && (
        <nav className="gx-mobile-bottom" aria-label="Быстрые разделы">
          {MOBILE_PRIMARY_ITEMS.map((item) => (
            <button
              key={item.id}
              type="button"
              className={`gx-mobile-bottom-item ${activeTab === item.id ? 'is-active' : ''}`}
              onClick={() => handleTabChange(item.id)}
              aria-label={item.label}
            >
              <span className="gx-mobile-bottom-icon" aria-hidden="true">
                {item.icon}
              </span>
              <span className="gx-mobile-bottom-label">{item.label}</span>
            </button>
          ))}
          <button
            type="button"
            className={`gx-mobile-bottom-item ${mobileMenuOpen ? 'is-active' : ''}`}
            onClick={() => setMobileMenuOpen((previous) => !previous)}
            aria-label={mobileMenuOpen ? 'Закрыть меню' : 'Открыть меню'}
          >
            <span className="gx-mobile-bottom-icon" aria-hidden="true">
              {mobileMenuOpen ? <X size={18} /> : <Menu size={18} />}
            </span>
            <span className="gx-mobile-bottom-label">Меню</span>
          </button>
        </nav>
      )}
    </div>
  );
};
