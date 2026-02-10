/**
 * ManusAppUnified.tsx
 * 
 * Единая точка входа в приложение Kolibri.
 * Все вкладки в едином стиле Manus.
 */

import { useState } from 'react';
import { ManusLayout, TabId } from './ManusLayout';
import { ChatTab } from './tabs/ChatTab';
import { CrawlerTab } from './tabs/CrawlerTab';
import { TasksTab } from './tabs/TasksTab';
import { KnowledgeTab } from './tabs/KnowledgeTab';
import { TerminalTab } from './tabs/TerminalTab';
import { SettingsTab } from './tabs/SettingsTab';
import { ArchiverTab } from './tabs/ArchiverTab';
import { ThemeProvider } from './ThemeContext';

export const ManusAppUnified = () => {
  const [activeTab, setActiveTab] = useState<TabId>('chat');
  const [sidebarCollapsed, setSidebarCollapsed] = useState(false);

  const renderTab = () => {
    switch (activeTab) {
      case 'chat':
        return <ChatTab />;
      case 'crawler':
        return <CrawlerTab />;
      case 'tasks':
        return <TasksTab />;
      case 'knowledge':
        return <KnowledgeTab />;
      case 'terminal':
        return <TerminalTab />;
      case 'archiver':
        return <ArchiverTab />;
      case 'settings':
        return <SettingsTab />;
      default:
        return <ChatTab />;
    }
  };

  return (
    <ThemeProvider>
      <ManusLayout
        activeTab={activeTab}
        onTabChange={setActiveTab}
        sidebarCollapsed={sidebarCollapsed}
        onToggleSidebar={() => setSidebarCollapsed(!sidebarCollapsed)}
      >
        {renderTab()}
      </ManusLayout>
    </ThemeProvider>
  );
};
