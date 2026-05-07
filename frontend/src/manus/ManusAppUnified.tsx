/**
 * ManusAppUnified.tsx
 * 
 * Единая точка входа в приложение Колибри.
 * Все вкладки в едином стиле Manus.
 */

import { lazy, Suspense, useState } from 'react';
import { ManusLayout, TabId, type ChatHistoryItem } from './ManusLayout';
import { ChatTab } from './tabs/ChatTab';

const VoiceTab = lazy(() => import('./tabs/VoiceTab').then((module) => ({ default: module.VoiceTab })));
const CoreTab = lazy(() => import('./tabs/CoreTab').then((module) => ({ default: module.CoreTab })));
const GPUStoreTab = lazy(() => import('./tabs/GPUStoreTab').then((module) => ({ default: module.GPUStoreTab })));
const FactoryTab = lazy(() => import('./tabs/FactoryTab').then((module) => ({ default: module.FactoryTab })));
const CrawlerTab = lazy(() => import('./tabs/CrawlerTab').then((module) => ({ default: module.CrawlerTab })));
const TasksTab = lazy(() => import('./tabs/TasksTab').then((module) => ({ default: module.TasksTab })));
const KnowledgeTab = lazy(() => import('./tabs/KnowledgeTab').then((module) => ({ default: module.KnowledgeTab })));
const TerminalTab = lazy(() => import('./tabs/TerminalTab').then((module) => ({ default: module.TerminalTab })));
const ArchiverTab = lazy(() => import('./tabs/ArchiverTab').then((module) => ({ default: module.ArchiverTab })));
const SettingsTab = lazy(() => import('./tabs/SettingsTab').then((module) => ({ default: module.SettingsTab })));

const CHAT_HISTORY_KEY = 'kolibri-chat-history-v1';

const TabLoading = () => (
  <div className="gx-tab-loading" role="status" aria-live="polite">
    Загрузка модуля...
  </div>
);

function loadChatHistory(): ChatHistoryItem[] {
  try {
    const raw = localStorage.getItem(CHAT_HISTORY_KEY);
    if (!raw) return [];
    const parsed = JSON.parse(raw) as ChatHistoryItem[];
    return Array.isArray(parsed) ? parsed : [];
  } catch {
    return [];
  }
}

export const ManusAppUnified = () => {
  const [activeTab, setActiveTab] = useState<TabId>('chat');
  const [sidebarCollapsed, setSidebarCollapsed] = useState(false);
  const [chatHistory, setChatHistory] = useState<ChatHistoryItem[]>(() => loadChatHistory());
  const [activeChatId, setActiveChatId] = useState<string>(() => {
    const history = loadChatHistory();
    return history[0]?.id || `chat-${Date.now()}`;
  });
  const [chatResetToken, setChatResetToken] = useState(0);

  const upsertChatHistory = (item: ChatHistoryItem) => {
    setChatHistory((prev) => {
      const next = [item, ...prev.filter((x) => x.id !== item.id)].slice(0, 50);
      try {
        localStorage.setItem(CHAT_HISTORY_KEY, JSON.stringify(next));
      } catch {
        // ignore storage errors
      }
      return next;
    });
  };

  const handleNewChat = () => {
    setActiveTab('chat');
    setActiveChatId(`chat-${Date.now()}`);
    setChatResetToken((v) => v + 1);
  };

  const renderTab = () => {
    switch (activeTab) {
      case 'chat':
        return (
          <ChatTab
            resetToken={chatResetToken}
            activeChatId={activeChatId}
            onChatActivity={upsertChatHistory}
            onNavigate={setActiveTab}
          />
        );
      case 'voice':
        return <VoiceTab />;
      case 'core':
        return <CoreTab />;
      case 'gpu':
        return <GPUStoreTab />;
      case 'factory':
        return <FactoryTab />;
      case 'crawler':
        return <CrawlerTab />;
      case 'tasks':
        return <TasksTab onClose={() => setActiveTab('chat')} />;
      case 'knowledge':
        return <KnowledgeTab />;
      case 'terminal':
        return <TerminalTab />;
      case 'archiver':
        return <ArchiverTab />;
      case 'settings':
        return <SettingsTab onClose={() => setActiveTab('chat')} />;
      default:
        return (
          <ChatTab
            resetToken={chatResetToken}
            activeChatId={activeChatId}
            onChatActivity={upsertChatHistory}
            onNavigate={setActiveTab}
          />
        );
    }
  };

  return (
    <ManusLayout
      activeTab={activeTab}
      onTabChange={setActiveTab}
      sidebarCollapsed={sidebarCollapsed}
      onToggleSidebar={() => setSidebarCollapsed(!sidebarCollapsed)}
      onNewChat={handleNewChat}
      chatHistory={chatHistory}
      onOpenChat={(chatId) => {
        setActiveChatId(chatId);
        setActiveTab('chat');
      }}
    >
      <Suspense fallback={<TabLoading />}>{renderTab()}</Suspense>
    </ManusLayout>
  );
};
