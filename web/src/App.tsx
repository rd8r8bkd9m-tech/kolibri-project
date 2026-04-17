import { useState, useRef, useEffect } from "react";
import { AppShell, ScrollArea, Box, Center, Stack, Title, Text, SimpleGrid, Paper, Group, ThemeIcon, Badge, Modal, Switch, SegmentedControl, Alert } from "@mantine/core";
import { useDisclosure } from "@mantine/hooks";
import { sendChatMessageStream, getConversations, getConversation, Conversation } from "./api";
import { KolibriBridge } from "./lib/kolibriBridge";
import { Message } from "./types/chat";
import { MessageBubble } from "./components/chat/MessageBubble";
import { ChatSidebar } from "./components/layout/ChatSidebar";
import { ChatHeader } from "./components/layout/ChatHeader";
import { ChatInput } from "./components/chat/ChatInput";
import { useMantineColorScheme } from "@mantine/core";
import { Activity, Brain, Cpu, Database, MessageSquare, Network, ShieldCheck, Sparkles } from "lucide-react";

import "./index.css";

export default function App() {
  const [opened, { toggle }] = useDisclosure(true);
  const [settingsOpened, { open: openSettings, close: closeSettings }] = useDisclosure(false);
  const { colorScheme, toggleColorScheme } = useMantineColorScheme();
  const isDark = colorScheme === 'dark';

  const [input, setInput] = useState("");
  const [messages, setMessages] = useState<Message[]>([]);
  const [conversations, setConversations] = useState<Conversation[]>([]);
  const [activeConversationId, setActiveConversationId] = useState<string | null>(null);
  
  const [isLoading, setIsLoading] = useState(false);
  const [useLocalModel, setUseLocalModel] = useState(false);
  const [profile, setProfile] = useState<"fast" | "balanced" | "deep">("balanced");
  const [bridge, setBridge] = useState<KolibriBridge | null>(null);
  const [bridgeStatus, setBridgeStatus] = useState<"loading" | "ready" | "offline">("loading");
  const [thinking, setThinking] = useState("");
  const [streamingMessageId, setStreamingMessageId] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  
  const activeRequestRef = useRef<AbortController | null>(null);
  const viewportRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    getConversations().then(data => setConversations(Array.isArray(data) ? data : [])).catch((err) => setError(err.message));
    KolibriBridge.load("/kolibri.wasm")
      .then((loadedBridge) => {
        setBridge(loadedBridge);
        setBridgeStatus("ready");
      })
      .catch((err) => {
        console.error(err);
        setBridgeStatus("offline");
      });
  }, []);

  useEffect(() => {
    viewportRef.current?.scrollTo({ top: viewportRef.current.scrollHeight, behavior: "smooth" });
  }, [messages, thinking]);

  const handleSelectChat = async (id: string) => {
    try {
      setError(null);
      setActiveConversationId(id);
      const detail = await getConversation(id);
      const msgs = Array.isArray(detail.messages) ? detail.messages : [];
      setMessages(msgs.map((m, i) => ({
        id: i.toString(),
        role: m.role,
        content: m.content,
        time: new Date(m.timestamp).toLocaleTimeString(),
        meta: { composition: m.composition, confidence: 0, method: "server", duration_ms: 0 }
      })));
    } catch (err) {
      setError(err instanceof Error ? err.message : "Не удалось открыть диалог");
    }
  };

  const handleNewChat = () => { setActiveConversationId(null); setMessages([]); setInput(""); setError(null); };

  const handleCancelResponse = () => {
    activeRequestRef.current?.abort();
    setIsLoading(false);
    setStreamingMessageId(null);
  };

  const handleSubmit = async () => {
    if (!input.trim() || isLoading) return;

    const userMsg: Message = { id: Date.now().toString(), role: "user", content: input.trim(), time: new Date().toLocaleTimeString() };
    setMessages((prev) => [...prev, userMsg]);
    setInput("");
    setIsLoading(true);
    setThinking("");
    setError(null);

    const assistantMsgId = (Date.now() + 1).toString();
    setStreamingMessageId(assistantMsgId);
    setMessages((prev) => [...prev, { id: assistantMsgId, role: "assistant", content: "", time: new Date().toLocaleTimeString() }]);

    activeRequestRef.current = new AbortController();

    if (useLocalModel && bridge) {
      // Local implementation placeholder
    } else {
        try {
            await sendChatMessageStream(
                { message: userMsg.content, conversation_id: activeConversationId || "new", profile, memory_enabled: true },
                (token) => {
                    setMessages((prev) => (Array.isArray(prev) ? prev : []).map(m => m.id === assistantMsgId ? { ...m, content: m.content + token } : m));
                },
                (t) => setThinking(t),
                (data) => {
                    if (data.conversation_id && data.conversation_id !== "new") {
                      setActiveConversationId(data.conversation_id);
                    }
                    setMessages((prev) => prev.map((m) => m.id === assistantMsgId ? {
                      ...m,
                      meta: {
                        confidence: data.confidence ?? 0,
                        method: data.method ?? "stream",
                        duration_ms: data.duration_ms ?? 0,
                        knowledge_hits: data.knowledge_hits,
                        sources: data.sources,
                        source: "server",
                        thinking: data.thinking,
                        cognitive: data.cognitive,
                        self_check: data.self_check,
                      },
                    } : m));
                    setIsLoading(false);
                    setStreamingMessageId(null);
                    setThinking("");
                    getConversations().then(data => setConversations(Array.isArray(data) ? data : [])).catch(console.error);
                },
                (error) => {
                    setError(error);
                    setMessages((prev) => prev.map((m) => m.id === assistantMsgId && !m.content ? { ...m, content: `Ошибка: ${error}` } : m));
                    setIsLoading(false);
                    setStreamingMessageId(null);
                },
                activeRequestRef.current.signal
            );
        } catch (e) {
            if (e instanceof Error && e.name !== "AbortError") {
              setError(e.message);
              setMessages((prev) => prev.map((m) => m.id === assistantMsgId && !m.content ? { ...m, content: `Ошибка: ${e.message}` } : m));
            }
            setIsLoading(false);
            setStreamingMessageId(null);
        }
    }
  };

  const EmptyState = () => (
    <Center mih="100%" py="xl">
      <Stack gap="xl" w="100%" maw={920}>
        <Stack gap="xs" ta="center" align="center">
          <ThemeIcon size={62} radius="xl" variant="gradient" gradient={{ from: "indigo", to: "teal" }}>
            <Sparkles size={30} />
          </ThemeIcon>
          <Title order={1} className="hero-title">Kolibri AI</Title>
          <Text c="dimmed" maw={620}>
            Гибридный чат для локального WASM-ядра, серверного синтеза, проверки ответов и работы с базой знаний.
          </Text>
          <Group gap="xs" justify="center">
            <Badge variant="light" color={bridgeStatus === "ready" ? "teal" : "orange"} leftSection={<Cpu size={12} />}>
              WASM {bridgeStatus === "ready" ? "ready" : bridgeStatus}
            </Badge>
            <Badge variant="light" color="indigo" leftSection={<Brain size={12} />}>{profile}</Badge>
            <Badge variant="light" color="cyan" leftSection={<Network size={12} />}>streaming</Badge>
          </Group>
        </Stack>

        <SimpleGrid cols={{ base: 1, sm: 2, md: 4 }} spacing="md">
          {[
            { title: "Спросить ядро", body: "Задайте вопрос и получите потоковый ответ с метриками.", Icon: MessageSquare, color: "indigo" },
            { title: "Проверить факты", body: "Ответы показывают источники, self-check и уверенность.", Icon: ShieldCheck, color: "teal" },
            { title: "Работать глубже", body: "Переключайте профиль между fast, balanced и deep.", Icon: Activity, color: "blue" },
            { title: "Память проекта", body: "Диалоги сохраняются и быстро открываются в истории.", Icon: Database, color: "grape" },
          ].map(({ title, body, Icon, color }) => (
            <Paper key={title} withBorder p="md" radius="lg" className="feature-tile">
              <ThemeIcon variant="light" color={color} radius="md" mb="sm">
                <Icon size={18} />
              </ThemeIcon>
              <Text fw={700} size="sm">{title}</Text>
              <Text c="dimmed" size="xs" mt={6}>{body}</Text>
            </Paper>
          ))}
        </SimpleGrid>
      </Stack>
    </Center>
  );

  return (
    <AppShell navbar={{ width: 292, breakpoint: "sm", collapsed: { mobile: !opened, desktop: !opened } }} padding="0" className="app-shell">
      <ChatSidebar isDark={isDark} conversations={conversations} activeConversationId={activeConversationId} onNewChat={handleNewChat} onSelectChat={handleSelectChat} />
      <AppShell.Main className="main-surface">
        <ChatHeader opened={opened} toggle={toggle} useLocalModel={useLocalModel} isDark={isDark} toggleColorScheme={toggleColorScheme} openSettings={openSettings} bridgeStatus={bridgeStatus} profile={profile} />
        
        <ScrollArea flex={1} p={{ base: "md", sm: "xl" }} viewportRef={viewportRef}>
          <Box maw={900} mx="auto" h="100%">
            {error && (
              <Alert color="red" variant="light" mb="md" title="Проблема соединения">
                {error}
              </Alert>
            )}
            {messages.length === 0 ? <EmptyState /> : (
              <Stack gap="lg" py="md">
                {(Array.isArray(messages) ? messages : []).map((msg) => (
                  <MessageBubble key={msg.id} msg={msg} isDark={isDark} thinking={thinking} streamingMessageId={streamingMessageId} />
                ))}
              </Stack>
            )}
          </Box>
        </ScrollArea>
        
        <ChatInput input={input} setInput={setInput} onSubmit={handleSubmit} onCancel={handleCancelResponse} isLoading={isLoading} isDark={isDark} useLocalModel={useLocalModel} />
      </AppShell.Main>
      <Modal opened={settingsOpened} onClose={closeSettings} title="Настройки сессии" centered>
        <Stack>
          <Switch
            checked={useLocalModel}
            onChange={(event) => setUseLocalModel(event.currentTarget.checked)}
            label="Использовать локальное WASM-ядро"
            description={bridgeStatus === "ready" ? "Локальное ядро загружено" : "Локальное ядро пока недоступно"}
            disabled={bridgeStatus !== "ready"}
          />
          <Box>
            <Text size="sm" fw={600} mb="xs">Профиль ответа</Text>
            <SegmentedControl
              fullWidth
              value={profile}
              onChange={(value) => setProfile(value as "fast" | "balanced" | "deep")}
              data={[
                { label: "Fast", value: "fast" },
                { label: "Balanced", value: "balanced" },
                { label: "Deep", value: "deep" },
              ]}
            />
          </Box>
        </Stack>
      </Modal>
    </AppShell>
  );
}
