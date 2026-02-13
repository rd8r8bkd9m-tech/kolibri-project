/**
 * ManusLayout.tsx
 *
 * Unified application shell.
 * Desktop: classic sidebar + content.
 * Mobile: compact top bar + full-screen drawer.
 */

import { ReactNode, useEffect, useMemo, useState } from 'react';
import {
  Archive,
  Bot,
  ChevronDown,
  ChevronLeft,
  ChevronRight,
  Database,
  Globe,
  ListTodo,
  Menu,
  MessageSquare,
  Mic,
  PenSquare,
  Search,
  Settings,
  Terminal as TerminalIcon,
  User,
} from 'lucide-react';
import { KolibriBrandMark } from './components/KolibriBrandMark';

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
  { id: 'voice', label: 'Компаньоны', icon: <Mic size={18} /> },
  { id: 'tasks', label: 'Задачи', icon: <ListTodo size={18} /> },
  { id: 'crawler', label: 'AI Агент', icon: <Globe size={18} /> },
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

const MOBILE_TOPBAR_TABS: TabId[] = ['chat', 'crawler', 'voice'];

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

  const showExpandedSidebar = !sidebarCollapsed;
  const showMobileTopbar = mobileViewport && MOBILE_TOPBAR_TABS.includes(activeTab);

  return (
    <div className="gx-shell">
      {!mobileViewport && (
        <aside className={`gx-sidebar ${sidebarCollapsed ? 'is-collapsed' : ''}`}>
          <div className="gx-sidebar-top">
            <div className="gx-brand-row">
              <button type="button" className="gx-brand" onClick={() => handleTabChange('chat')}>
                <span className="gx-brand-badge" aria-hidden="true">
                  <KolibriBrandMark size={18} />
                </span>
                {showExpandedSidebar && (
                  <span className="gx-brand-label-wrap">
                    <span className="gx-brand-title">Колибри</span>
                    <span className="gx-brand-subtitle">AI Workspace</span>
                  </span>
                )}
              </button>

              <button
                type="button"
                className="gx-collapse-btn"
                onClick={onToggleSidebar}
                aria-label={sidebarCollapsed ? 'Развернуть сайдбар' : 'Свернуть сайдбар'}
              >
                {sidebarCollapsed ? <ChevronRight size={16} /> : <ChevronLeft size={16} />}
              </button>
            </div>

            {showExpandedSidebar && (
              <>
                <button type="button" className="gx-new-chat" onClick={onNewChat}>
                  <PenSquare size={16} />
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
                  {visibleHistory.slice(0, 60).map((chat) => (
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
      )}

      {mobileViewport && (
        <aside className={`gx-mobile-drawer ${mobileMenuOpen ? 'is-open' : ''}`}>
          <div className="gx-mobile-drawer-shell">
            <button
              type="button"
              className="gx-mobile-profile"
              onClick={() => handleTabChange('settings')}
              aria-label="Профиль и настройки"
            >
              <span className="gx-mobile-profile-avatar" aria-hidden="true">
                <User size={22} />
              </span>
              <span className="gx-mobile-profile-meta">
                <strong>Vladislav Kochurov</strong>
                <span>Аккаунт</span>
              </span>
              <span className="gx-mobile-profile-open" aria-hidden="true">
                <ChevronRight size={19} />
              </span>
            </button>

            <div className="gx-mobile-shortcuts">
              <button type="button" className="gx-mobile-shortcut" onClick={() => handleTabChange('tasks')}>
                <ListTodo size={21} />
                <span>Задачи</span>
              </button>
              <button type="button" className="gx-mobile-shortcut" onClick={() => handleTabChange('voice')}>
                <Bot size={21} />
                <span>Компаньоны</span>
              </button>
            </div>

            <div className="gx-mobile-conversations-head">
              <span>Разговоры</span>
              <ChevronDown size={20} />
            </div>

            <div className="gx-mobile-conversations-list">
              {visibleHistory.length === 0 ? (
                <div className="gx-mobile-conversation-empty">Разговоры появятся после первых сообщений</div>
              ) : (
                visibleHistory.slice(0, 120).map((chat) => (
                  <button
                    key={chat.id}
                    type="button"
                    className="gx-mobile-conversation-item"
                    onClick={() => {
                      onOpenChat(chat.id);
                      handleTabChange('chat');
                    }}
                  >
                    <span className="gx-mobile-conversation-title">{chat.title}</span>
                    <span className="gx-mobile-conversation-time">
                      {chat.updatedAt
                        ? new Date(chat.updatedAt).toLocaleDateString('ru-RU', {
                            day: '2-digit',
                            month: '2-digit',
                          })
                        : 'Сегодня'}
                    </span>
                  </button>
                ))
              )}
            </div>

            <div className="gx-mobile-drawer-bottom">
              <label className="gx-mobile-drawer-search" aria-label="Поиск">
                <Search size={20} />
                <input
                  type="search"
                  value={searchText}
                  onChange={(event) => setSearchText(event.target.value)}
                  placeholder="Поиск"
                />
              </label>

              <button
                type="button"
                className="gx-mobile-drawer-action"
                onClick={() => handleTabChange('settings')}
                aria-label="Настройки"
              >
                <Settings size={21} />
              </button>

              <button
                type="button"
                className="gx-mobile-drawer-action"
                onClick={() => {
                  onNewChat();
                  setMobileMenuOpen(false);
                }}
                aria-label="Новый чат"
              >
                <PenSquare size={21} />
              </button>
            </div>
          </div>
        </aside>
      )}

      {mobileMenuOpen && mobileViewport && (
        <button
          type="button"
          className="gx-mobile-overlay"
          onClick={() => setMobileMenuOpen(false)}
          aria-label="Закрыть меню"
        />
      )}

      <main className="gx-content">
        {showMobileTopbar && (
          <div className="gx-mobile-topbar gx-mobile-topbar-grok">
            <button
              type="button"
              className="gx-mobile-circle-btn"
              onClick={() => setMobileMenuOpen(true)}
              aria-label="Открыть меню"
            >
              <Menu size={23} />
            </button>

            <div className="gx-mobile-segment" role="tablist" aria-label="Режим">
              <button
                type="button"
                role="tab"
                className={`gx-mobile-segment-btn ${activeTab !== 'crawler' ? 'is-active' : ''}`}
                aria-selected={activeTab !== 'crawler'}
                onClick={() => handleTabChange('chat')}
              >
                Спросить
              </button>
              <button
                type="button"
                role="tab"
                className={`gx-mobile-segment-btn ${activeTab === 'crawler' ? 'is-active' : ''}`}
                aria-selected={activeTab === 'crawler'}
                onClick={() => handleTabChange('crawler')}
              >
                Imagine
              </button>
            </div>

            <button
              type="button"
              className="gx-mobile-circle-btn"
              onClick={onNewChat}
              aria-label="Новый чат"
            >
              <PenSquare size={22} />
            </button>
          </div>
        )}

        <div className="gx-content-body">{children}</div>
      </main>
    </div>
  );
};
