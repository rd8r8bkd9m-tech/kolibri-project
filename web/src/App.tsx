import { useState, useRef, useEffect } from "react";
import {
  AppShell,
  Burger,
  Group,
  TextInput,
  ActionIcon,
  ScrollArea,
  Text,
  Stack,
  Paper,
  Box,
  ThemeIcon,
  Title,
  Center,
  Loader,
  Badge,
  Drawer,
  useMantineColorScheme,
  Switch,
  Divider,
  Progress,
  Tooltip,
  Alert
} from "@mantine/core";
import { useDisclosure, useInterval } from "@mantine/hooks";
import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";
import { sendChatMessage, sendChatMessageStream } from "./api";
import { KolibriBridge, KolibriHealth, KolibriBridgeResult } from "./lib/kolibriBridge";
import { MessageSquare, Send, Bot, User, BrainCircuit, Code, Image as ImageIcon, LayoutDashboard, Presentation, Sun, Moon, Settings, Cpu, Zap, Activity, Info } from "lucide-react";

import "./index.css";

interface Message {
  id: string;
  role: "user" | "assistant";
  content: string;
  time: string;
  meta?: {
    confidence: number;
    method: string;
    duration_ms: number;
    knowledge_hits?: number;
    source?: "local" | "server";
  };
}

const SUGGESTIONS = [
  { icon: <BrainCircuit size={18} />, text: "Глубокое исследование" },
  { icon: <ImageIcon size={18} />, text: "Создать изображение" },
  { icon: <Code size={18} />, text: "Веб-разработка" },
  { icon: <LayoutDashboard size={18} />, text: "Создать дашборд" },
  { icon: <Presentation size={18} />, text: "Слайды" },
];

function MarkdownContent({ content, isUser }: { content: string; isUser: boolean }) {
  if (isUser) return <Text size="md">{content}</Text>;
  
  return (
    <Box className="markdown-content">
      <ReactMarkdown remarkPlugins={[remarkGfm]}>{content}</ReactMarkdown>
    </Box>
  );
}

export default function App() {
  const [opened, { toggle }] = useDisclosure(true);
  const [settingsOpened, { open: openSettings, close: closeSettings }] = useDisclosure(false);
  const { colorScheme, toggleColorScheme } = useMantineColorScheme();
  const isDark = colorScheme === 'dark';

  const [input, setInput] = useState("");
  const [messages, setMessages] = useState<Message[]>([]);
  const [isLoading, setIsLoading] = useState(false);
  const [useLocalModel, setUseLocalModel] = useState(false);
  const [useStreaming, setUseStreaming] = useState(true);
  const [bridge, setBridge] = useState<KolibriBridge | null>(null);
  const [health, setHealth] = useState<KolibriHealth | null>(null);
  const [progress, setProgress] = useState({ state: "idle", value: 0, detail: "" });
  const [thinking, setThinking] = useState("");
  
  const scrollRef = useRef<HTMLDivElement>(null);

  const scrollToBottom = () => {
    if (scrollRef.current) {
      scrollRef.current.scrollTo({ top: scrollRef.current.scrollHeight, behavior: "smooth" });
    }
  };

  useEffect(() => {
    scrollToBottom();
  }, [messages, isLoading]);

  // Load Bridge
  useEffect(() => {
    KolibriBridge.load("/kolibri.wasm")
      .then(b => {
        setBridge(b);
        console.log("Kolibri WASM Bridge Loaded");
      })
      .catch(err => console.error("Failed to load WASM:", err));
  }, []);

  // Update Progress/Health interval
  const interval = useInterval(() => {
    if (bridge && bridge.isReady()) {
      if (isLoading && useLocalModel) {
        setProgress(bridge.getProgress());
        setThinking(bridge.getThinking());
      }
      if (settingsOpened) {
        setHealth(bridge.health());
      }
    }
  }, 200);

  useEffect(() => {
    interval.start();
    return interval.stop;
  }, [bridge, isLoading, useLocalModel, settingsOpened]);

  const handleNewChat = () => {
    setMessages([]);
    setInput("");
    if (scrollRef.current) {
      scrollRef.current.scrollTo({ top: 0, behavior: "smooth" });
    }
  };

  const handleSubmit = async () => {
    if (!input.trim() || isLoading) return;

    const userMsg: Message = {
      id: Date.now().toString(),
      role: "user",
      content: input.trim(),
      time: new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }),
    };

    setMessages((prev) => [...prev, userMsg]);
    setInput("");
    setIsLoading(true);

    try {
      if (useLocalModel && bridge) {
        // Use Local WASM Core
        const convId = "default";
        const result: KolibriBridgeResult = bridge.sendMessage(convId, userMsg.content);
        
        const assistantMsg: Message = {
          id: (Date.now() + 1).toString(),
          role: "assistant",
          content: result.response || "Пустой ответ",
          time: new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }),
          meta: {
            confidence: result.confidence,
            method: result.method,
            duration_ms: result.duration_ms,
            source: "local"
          },
        };
        setMessages((prev) => [...prev, assistantMsg]);
      } else {
        // Use Hybrid Server API
        if (useStreaming) {
          // Streaming mode
          let assistantContent = "";
          const assistantMsgId = (Date.now() + 1).toString();
          
          const assistantMsg: Message = {
            id: assistantMsgId,
            role: "assistant",
            content: "",
            time: new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }),
            meta: {
              confidence: 0,
              method: "",
              duration_ms: 0,
              source: "server"
            },
          };
          setMessages((prev) => [...prev, assistantMsg]);

          await sendChatMessageStream(
            {
              message: userMsg.content,
              model: "hybrid",
            },
            (token) => {
              assistantContent += token;
              setMessages((prev) => prev.map(msg => 
                msg.id === assistantMsgId 
                  ? { ...msg, content: assistantContent }
                  : msg
              ));
            },
            (thinking) => {
              setThinking(thinking);
            },
            (doneData) => {
              setMessages((prev) => prev.map(msg => 
                msg.id === assistantMsgId 
                  ? { 
                      ...msg, 
                      content: assistantContent,
                      meta: {
                        confidence: doneData.confidence || 0,
                        method: doneData.method || "",
                        duration_ms: doneData.duration_ms || 0,
                        knowledge_hits: doneData.knowledge_hits || 0,
                        source: "server"
                      }
                    }
                  : msg
              ));
            },
            (error) => {
              setMessages((prev) => prev.map(msg => 
                msg.id === assistantMsgId 
                  ? { ...msg, content: `**Ошибка:** ${error}` }
                  : msg
              ));
            }
          );
        } else {
          // Regular mode
          const response = await sendChatMessage({
            message: userMsg.content,
            model: "hybrid",
          });

          const assistantMsg: Message = {
            id: (Date.now() + 1).toString(),
            role: "assistant",
            content: response.response || "Пустой ответ",
            time: new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }),
            meta: {
              confidence: response.confidence,
              method: response.method,
              duration_ms: response.duration_ms,
              knowledge_hits: response.knowledge_hits,
              source: "server"
            },
          };
          setMessages((prev) => [...prev, assistantMsg]);
        }
      }
    } catch (err: any) {
      const errorMsg: Message = {
        id: (Date.now() + 1).toString(),
        role: "assistant",
        content: `**Ошибка:** ${err.message || "Не удалось получить ответ."}`,
        time: new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }),
      };
      setMessages((prev) => [...prev, errorMsg]);
    } finally {
      setIsLoading(false);
      setThinking("");
      setProgress({ state: "idle", value: 0, detail: "" });
    }
  };

  return (
    <AppShell
      navbar={{
        width: 260,
        breakpoint: "sm",
        collapsed: { mobile: !opened, desktop: !opened },
      }}
      padding="0"
    >
      <AppShell.Navbar p="md" bg={isDark ? "dark.8" : "gray.0"} style={{ borderRight: `1px solid var(--mantine-color-${isDark ? 'dark-6' : 'gray-3'})`, transition: "background-color 0.2s" }}>
        <Group justify="space-between" mb="xl">
          <Group gap="sm">
            <ThemeIcon size={28} radius="md" color="indigo">
              <Bot size={18} />
            </ThemeIcon>
            <Text fw={600} size="sm" c={isDark ? "white" : "black"}>Kolibri AI</Text>
          </Group>
          <Burger opened={opened} onClick={toggle} hiddenFrom="sm" size="sm" color="gray" />
        </Group>

        <Stack gap="xs" flex={1}>
          <Text size="xs" fw={500} c="dimmed" tt="uppercase" lts={1}>Сегодня</Text>
          <Paper
            p="sm"
            radius="md"
            bg={isDark ? "dark.6" : "white"}
            className={isDark ? "hover-paper-dark" : "hover-paper-light"}
            onClick={handleNewChat}
            style={{ cursor: 'pointer' }}
            withBorder={!isDark}
          >
            <Group gap="sm" wrap="nowrap">
              <MessageSquare size={16} color="var(--mantine-color-gray-5)" />
              <Text size="sm" truncate="end" c={isDark ? "white" : "black"}>Новый чат</Text>
            </Group>
          </Paper>
        </Stack>
      </AppShell.Navbar>

      <AppShell.Main style={{ display: 'flex', flexDirection: 'column', height: '100vh', backgroundColor: isDark ? 'var(--mantine-color-dark-7)' : 'white', transition: "background-color 0.2s" }}>
        {/* Header */}
        <Group h={52} px="md" justify="space-between" style={{ borderBottom: `1px solid var(--mantine-color-${isDark ? 'dark-6' : 'gray-2'})`, backgroundColor: isDark ? 'var(--mantine-color-dark-7)' : 'white', transition: "background-color 0.2s" }}>
          <Group>
            <Burger opened={opened} onClick={toggle} size="sm" color="gray" />
            <Badge variant="light" color={useLocalModel ? "teal" : "indigo"} radius="sm">
              {useLocalModel ? "Local Core (WASM)" : "Hybrid Model"}
            </Badge>
          </Group>
          <Group gap="sm">
            <ActionIcon variant="subtle" color={isDark ? "gray" : "dark"} onClick={() => toggleColorScheme()} title="Сменить тему">
              {isDark ? <Sun size={18} /> : <Moon size={18} />}
            </ActionIcon>
            <ActionIcon variant="subtle" color={isDark ? "gray" : "dark"} onClick={openSettings} title="Настройки">
              <Settings size={18} />
            </ActionIcon>
          </Group>
        </Group>

        {/* Chat Area */}
        <ScrollArea flex={1} viewportRef={scrollRef} p="xl">
          <Box maw={768} mx="auto">
            {messages.length === 0 ? (
              <Center style={{ flexDirection: "column", height: "60vh" }}>
                <Title order={1} mb="xl" c={isDark ? "white" : "black"}>Чем я могу помочь?</Title>
                <Group justify="center">
                  {SUGGESTIONS.map((item, i) => (
                    <Paper
                      key={i}
                      p="sm"
                      radius="md"
                      bg={isDark ? "dark.6" : "white"}
                      className={isDark ? "hover-paper-dark" : "hover-paper-light"}
                      style={{ cursor: 'pointer' }}
                      withBorder={!isDark}
                      onClick={() => setInput(item.text)}
                    >
                      <Group gap="sm">
                        {item.icon}
                        <Text size="sm" c={isDark ? "white" : "black"}>{item.text}</Text>
                      </Group>
                    </Paper>
                  ))}
                </Group>
              </Center>
            ) : (
              <Stack gap="xl">
                {messages.map((msg) => (
                  <Group key={msg.id} align="flex-start" justify={msg.role === "user" ? "flex-end" : "flex-start"} wrap="nowrap">
                    {msg.role === "assistant" && (
                      <ThemeIcon size={32} radius="xl" color="indigo" mt={4}>
                        <Bot size={18} />
                      </ThemeIcon>
                    )}
                    
                    <Box maw="80%">
                      <Paper
                        p="md"
                        radius="lg"
                        bg={msg.role === "user" ? "indigo.6" : (isDark ? "dark.6" : "gray.0")}
                        withBorder={msg.role === "assistant" && !isDark}
                        style={{
                          borderBottomRightRadius: msg.role === "user" ? 4 : undefined,
                          borderBottomLeftRadius: msg.role === "assistant" ? 4 : undefined,
                          color: msg.role === "user" ? "white" : (isDark ? "white" : "black")
                        }}
                      >
                        <MarkdownContent content={msg.content} isUser={msg.role === "user"} />
                      </Paper>

                      {msg.meta && (
                        <Group gap="xs" mt="xs" px="xs">
                          <Tooltip label={msg.meta.source === 'local' ? "Обработано локально в браузере" : "Обработано на сервере"}>
                            <Badge size="xs" variant="light" color={msg.meta.source === 'local' ? "teal" : "green"} radius="sm">
                              {msg.meta.method} {msg.meta.source === 'local' && '(WASM)'}
                            </Badge>
                          </Tooltip>
                          <Text size="xs" c="dimmed">{msg.meta.duration_ms.toFixed(0)}ms</Text>
                          {msg.meta.knowledge_hits && msg.meta.knowledge_hits > 0 && (
                            <Badge size="xs" variant="dot" color="blue">
                              {msg.meta.knowledge_hits} facts
                            </Badge>
                          )}
                        </Group>
                      )}
                    </Box>

                    {msg.role === "user" && (
                      <ThemeIcon size={32} radius="xl" color={isDark ? "dark.4" : "gray.2"} mt={4}>
                        <User size={18} color={isDark ? "white" : "black"} />
                      </ThemeIcon>
                    )}
                  </Group>
                ))}
                {isLoading && (
                  <Group align="flex-start" wrap="nowrap">
                    <ThemeIcon size={32} radius="xl" color="indigo" mt={4}>
                      <Bot size={18} />
                    </ThemeIcon>
                    <Stack gap={4} flex={1}>
                      <Paper p="md" radius="lg" bg={isDark ? "dark.6" : "white"} withBorder={!isDark} style={{ borderBottomLeftRadius: 4, alignSelf: 'flex-start' }}>
                        <Stack gap="xs">
                          <Group gap="sm">
                            <Loader color="indigo" size="xs" type="dots" />
                            {thinking && <Text size="xs" c="dimmed" fs="italic">{thinking}...</Text>}
                          </Group>
                          {useLocalModel && progress.value > 0 && (
                            <Box w={200}>
                              <Text size="10px" c="dimmed" mb={2} tt="uppercase">{progress.state}: {progress.detail}</Text>
                              <Progress value={progress.value * 100} size="xs" color="teal" animated />
                            </Box>
                          )}
                        </Stack>
                      </Paper>
                    </Stack>
                  </Group>
                )}
              </Stack>
            )}
          </Box>
        </ScrollArea>

        {/* Input Area */}
        <Box p="md" style={{ borderTop: `1px solid var(--mantine-color-${isDark ? 'dark-6' : 'gray-2'})`, backgroundColor: isDark ? 'var(--mantine-color-dark-7)' : 'white', transition: "background-color 0.2s" }}>
          <Box maw={768} mx="auto">
            <Paper p={4} radius="xl" bg={isDark ? "dark.6" : "gray.0"} withBorder={!isDark} style={{ border: isDark ? `1px solid var(--mantine-color-dark-5)` : undefined }}>
              <Group wrap="nowrap" gap="xs">
                <TextInput
                  flex={1}
                  placeholder={useLocalModel ? "Спросите локальное ядро..." : "Спросите Kolibri..."}
                  value={input}
                  onChange={(e) => setInput(e.currentTarget.value)}
                  onKeyDown={(e) => e.key === "Enter" && !e.shiftKey && (e.preventDefault(), handleSubmit())}
                  variant="unstyled"
                  px="md"
                  styles={{ input: { color: isDark ? 'white' : 'black' } }}
                />
                <ActionIcon
                  size={36}
                  radius="xl"
                  color={useLocalModel ? "teal" : "indigo"}
                  variant="filled"
                  onClick={handleSubmit}
                  disabled={!input.trim() || isLoading}
                  className="hover-action"
                >
                  <Send size={16} />
                </ActionIcon>
              </Group>
            </Paper>
            <Center mt="xs">
              <Text size="xs" c="dimmed">Kolibri AI {useLocalModel ? 'Local' : 'Hybrid'} Core. Проверяйте важную информацию.</Text>
            </Center>
          </Box>
        </Box>
      </AppShell.Main>

      {/* Settings Drawer */}
      <Drawer opened={settingsOpened} onClose={closeSettings} title={<Text fw={600} c={isDark ? "white" : "black"}>Настройки Kolibri Hive-Mind</Text>} position="right" size="md">
        <ScrollArea h="calc(100vh - 80px)">
          <Stack gap="md" p="xs">
            <Paper p="sm" withBorder radius="md" bg={isDark ? "dark.7" : "gray.0"}>
              <Group justify="space-between">
                <Group gap="sm">
                  <ThemeIcon variant="light" color="blue"><Sun size={16}/></ThemeIcon>
                  <Text size="sm" fw={500} c={isDark ? "white" : "black"}>Темная тема</Text>
                </Group>
                <Switch checked={isDark} onChange={() => toggleColorScheme()} color="indigo" />
              </Group>
            </Paper>

            <Paper p="sm" withBorder radius="md" bg={isDark ? "dark.7" : "gray.0"}>
              <Stack gap="sm">
                <Group justify="space-between">
                  <Group gap="sm">
                    <ThemeIcon variant="light" color="teal"><Cpu size={16}/></ThemeIcon>
                    <Text size="sm" fw={500} c={isDark ? "white" : "black"}>Локальное ядро (WASM)</Text>
                  </Group>
                  <Switch 
                    checked={useLocalModel} 
                    onChange={(e) => setUseLocalModel(e.currentTarget.checked)} 
                    disabled={!bridge}
                    color="teal" 
                  />
                </Group>
                <Group justify="space-between">
                  <Group gap="sm">
                    <ThemeIcon variant="light" color="blue"><Zap size={16}/></ThemeIcon>
                    <Text size="sm" fw={500} c={isDark ? "white" : "black"}>Потоковый ответ</Text>
                  </Group>
                  <Switch 
                    checked={useStreaming} 
                    onChange={(e) => setUseStreaming(e.currentTarget.checked)} 
                    disabled={useLocalModel}
                    color="blue" 
                  />
                </Group>
                <Text size="xs" c="dimmed">Показывать ответ по словам в реальном времени для лучшего пользовательского опыта.</Text>
                {!bridge && (
                  <Alert variant="light" color="orange" title="Загрузка ядра" icon={<Info size={16}/>}>
                    WASM модуль еще не загружен. Пожалуйста, подождите.
                  </Alert>
                )}
              </Stack>
            </Paper>

            <Paper p="sm" withBorder radius="md" bg={isDark ? "dark.7" : "gray.0"}>
              <Stack gap="xs">
                <Group gap="sm" mb={4}>
                  <ThemeIcon variant="light" color="orange"><Zap size={16}/></ThemeIcon>
                  <Text size="sm" fw={500} c={isDark ? "white" : "black"}>Оптимизация</Text>
                </Group>
                <Switch defaultChecked label="Фрактальная память (Fractal Memory)" color="indigo" size="xs" />
                <Switch defaultChecked label="Активное обучение (Auto-Learner)" color="indigo" size="xs" />
                <Switch defaultChecked label="SIMD ускорение (SSE2/WASM)" color="indigo" size="xs" />
              </Stack>
            </Paper>

            {health && (
              <Paper p="sm" withBorder radius="md" bg={isDark ? "dark.7" : "gray.0"}>
                <Stack gap="xs">
                  <Group gap="sm" mb={4}>
                    <ThemeIcon variant="light" color="red"><Activity size={16}/></ThemeIcon>
                    <Text size="sm" fw={500} c={isDark ? "white" : "black"}>System Health</Text>
                  </Group>
                  <Group justify="space-between">
                    <Text size="xs" c="dimmed">Статус:</Text>
                    <Badge color="green" size="xs">{health.status}</Badge>
                  </Group>
                  <Group justify="space-between">
                    <Text size="xs" c="dimmed">Uptime:</Text>
                    <Text size="xs" fw={500} c={isDark ? "white" : "black"}>{(health.uptime_ms / 1000).toFixed(1)}s</Text>
                  </Group>
                  <Group justify="space-between">
                    <Text size="xs" c="dimmed">Запросов:</Text>
                    <Text size="xs" fw={500} c={isDark ? "white" : "black"}>{health.queries_processed}</Text>
                  </Group>
                  <Group justify="space-between">
                    <Text size="xs" c="dimmed">Ср. ответ:</Text>
                    <Text size="xs" fw={500} c={isDark ? "white" : "black"}>{health.avg_response_ms.toFixed(1)}ms</Text>
                  </Group>
                  <Group justify="space-between">
                    <Text size="xs" c="dimmed">Диалогов:</Text>
                    <Text size="xs" fw={500} c={isDark ? "white" : "black"}>{health.conversations_active}</Text>
                  </Group>
                </Stack>
              </Paper>
            )}

            <Divider my="sm" />
            <Stack gap={4} align="center">
              <Text size="xs" c="dimmed">Kolibri Hive-Mind v1.1.0</Text>
              <Text size="xs" c="dimmed" fs="italic">Unified Organization of Intelligence</Text>
            </Stack>
          </Stack>
        </ScrollArea>
      </Drawer>
    </AppShell>
  );
}
