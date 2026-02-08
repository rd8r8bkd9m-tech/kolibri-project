/**
 * Manus-style UI Components
 * 
 * Экспорт всех компонентов для нового веб-интерфейса
 * в стиле Manus AI (manus.im)
 */

// Главный компонент - единая точка входа
export { ManusAppUnified } from './ManusAppUnified';

// Layout
export { ManusLayout, type TabId } from './ManusLayout';

// Вкладки
export { ChatTab } from './tabs/ChatTab';
export { TasksTab } from './tabs/TasksTab';
export { KnowledgeTab } from './tabs/KnowledgeTab';
export { TerminalTab } from './tabs/TerminalTab';
export { SettingsTab } from './tabs/SettingsTab';

// Старые компоненты (deprecated, для совместимости)
export { ManusApp, type Task, type Message } from './ManusApp';
export { ManusHeader } from './ManusHeader';
export { ManusTaskPanel } from './ManusTaskPanel';
export { ManusChat } from './ManusChat';
export { ManusInputBar } from './ManusInputBar';
export { ManusWelcome } from './ManusWelcome';
export { 
  useManusAgent,
  type Message as AgentMessage,
  type Task as AgentTask 
} from './useManusAgent';

