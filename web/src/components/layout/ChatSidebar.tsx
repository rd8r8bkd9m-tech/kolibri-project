import { AppShell, Group, ThemeIcon, Text, Paper, ScrollArea, Stack } from "@mantine/core";
import { Bot, MessageSquare, Plus } from "lucide-react";
import { Conversation } from "../../api";

interface ChatSidebarProps {
  isDark: boolean;
  conversations: Conversation[];
  activeConversationId: string | null;
  onNewChat: () => void;
  onSelectChat: (id: string) => void;
}

export function ChatSidebar({ isDark, conversations, activeConversationId, onNewChat, onSelectChat }: ChatSidebarProps) {
  return (
    <AppShell.Navbar p="md" className="glass-panel" style={{ borderRight: 'none' }}>
      <Group justify="space-between" mb="xl">
        <Group gap="sm">
          <ThemeIcon size={28} radius="md" color="indigo">
            <Bot size={18} />
          </ThemeIcon>
          <Text fw={600} size="sm" c={isDark ? "white" : "black"}>Kolibri AI</Text>
        </Group>
      </Group>

      <Stack gap="xs" flex={1}>
        <Text size="xs" fw={500} c="dimmed" tt="uppercase" lts={1}>История</Text>
        <Paper
          p="sm"
          radius="md"
          bg={isDark ? "rgba(255,255,255,0.05)" : "white"}
          onClick={onNewChat}
          style={{ cursor: 'pointer', transition: 'all 0.2s' }}
          className={isDark ? "hover-paper-dark" : "hover-paper-light"}
        >
          <Group gap="sm" wrap="nowrap">
            <Plus size={16} color="var(--mantine-color-gray-5)" />
            <Text size="sm" truncate="end" c={isDark ? "white" : "black"}>Новый чат</Text>
          </Group>
        </Paper>
        <ScrollArea flex={1}>
          <Stack gap={4}>
            {(Array.isArray(conversations) ? conversations : []).map(conv => (
              <Paper
                key={conv.id}
                p="sm"
                radius="md"
                bg={activeConversationId === conv.id ? (isDark ? "rgba(255,255,255,0.1)" : "gray.2") : "transparent"}
                className={isDark ? "hover-paper-dark" : "hover-paper-light"}
                onClick={() => onSelectChat(conv.id)}
                style={{ cursor: 'pointer', transition: 'all 0.2s' }}
              >
                <Text size="sm" truncate="end" c={isDark ? "white" : "black"}>{conv.title}</Text>
              </Paper>
            ))}
          </Stack>
        </ScrollArea>

      </Stack>
    </AppShell.Navbar>
  );
}
