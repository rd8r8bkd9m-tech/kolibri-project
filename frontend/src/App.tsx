import { useMemo } from "react";
import AppShell from "./components/AppShell";
import ChatInput from "./components/ChatInput";
import ChatView from "./components/ChatView";
import InspectorPanel from "./components/InspectorPanel";
import NavigationRail from "./components/NavigationRail";
import TopBar from "./components/TopBar";
import WelcomeScreen from "./components/WelcomeScreen";
import useKolibriChat from "./core/useKolibriChat";

const App = () => {
  const {
    messages,
    draft,
    mode,
    isProcessing,
    bridgeReady,
    conversationId,
    conversationTitle,
    knowledgeStatus,
    knowledgeError,
    statusLoading,
    latestAssistantMessage,
    metrics,
    setDraft,
    setMode,
    renameConversation,
    sendMessage,
    resetConversation,
    refreshKnowledgeStatus,
  } = useKolibriChat();

  const mainContent = useMemo(() => {
    if (!messages.length) {
      return <WelcomeScreen onSuggestionSelect={setDraft} />;
    }

    return <ChatView messages={messages} isLoading={isProcessing} conversationId={conversationId} />;
  }, [conversationId, isProcessing, messages, setDraft]);

  return (
    <AppShell
      navigation={
        <NavigationRail
          onCreateConversation={resetConversation}
          isBusy={isProcessing}
          metrics={metrics}
        />
      }
      header={
        <TopBar
          title={conversationTitle}
          onTitleChange={renameConversation}
          isProcessing={isProcessing}
          bridgeReady={bridgeReady}
          knowledgeStatus={knowledgeStatus}
          metrics={metrics}
          onRefreshKnowledge={refreshKnowledgeStatus}
          isKnowledgeLoading={statusLoading}
        />
      }
      inspector={
        <InspectorPanel
          status={knowledgeStatus}
          error={knowledgeError}
          isLoading={statusLoading}
          metrics={metrics}
          latestAssistantMessage={latestAssistantMessage}
          onRefresh={refreshKnowledgeStatus}
        />
      }
    >
      <div className="flex h-full flex-1 flex-col gap-6">
        <div className="flex-1">{mainContent}</div>
        <ChatInput
          value={draft}
          mode={mode}
          isBusy={isProcessing || !bridgeReady}
          onChange={setDraft}
          onModeChange={setMode}
          onSubmit={sendMessage}
          onReset={resetConversation}
        />
      </div>
    </AppShell>
  );
};

export default App;
