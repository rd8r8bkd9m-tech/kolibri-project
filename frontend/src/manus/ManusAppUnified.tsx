/**
 * ManusAppUnified.tsx
 * 
 * Единая точка входа в приложение Kolibri.
 * Все вкладки в едином стиле Manus.
 */

import { useState } from 'react';
import { ManusLayout, TabId, type ChatHistoryItem } from './ManusLayout';
import { ChatTab } from './tabs/ChatTab';
import { CrawlerTab } from './tabs/CrawlerTab';
import { TasksTab } from './tabs/TasksTab';
import { KnowledgeTab } from './tabs/KnowledgeTab';
import { TerminalTab } from './tabs/TerminalTab';
import { SettingsTab } from './tabs/SettingsTab';
import { ArchiverTab } from './tabs/ArchiverTab';
import { VoiceTab } from './tabs/VoiceTab';

const CHAT_HISTORY_KEY = 'kolibri-chat-history-v1';

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
          />
        );
      case 'voice':
        return <VoiceTab />;
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
      {renderTab()}
    </ManusLayout>
  );
};
