import { Paper, Box, Text, Stack, Tooltip, Badge, Group, ThemeIcon, Loader } from "@mantine/core";
import { Bot, User } from "lucide-react";
import { Message } from "../../types/chat";
import { CompositionResult } from "../../api";
import { MarkdownContent, CompositionView } from "./MessageBubbleUtils";
import { SelfCheckBadge } from "../SelfCheckBadge";
import { DigitalGenome } from "../DigitalGenome";
import { ExplanationButton } from "../ExplanationButton";

interface MessageBubbleProps {
  msg: Message;
  isDark: boolean;
  thinking?: string;
  streamingMessageId: string | null;
}

export function MessageBubble({ msg, isDark, thinking, streamingMessageId }: MessageBubbleProps) {
  return (
    <Group align="flex-start" justify={msg.role === "user" ? "flex-end" : "flex-start"} wrap="nowrap">
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
          {msg.content ? (
            <MarkdownContent content={msg.content} isUser={msg.role === "user"} />
          ) : (
            <Group gap="sm">
              <Loader color="indigo" size="xs" type="dots" />
              {thinking && msg.id === streamingMessageId && (
                <Text size="xs" c="dimmed" fs="italic">{thinking}...</Text>
              )}
            </Group>
          )}
        </Paper>

        {msg.meta && (
          <Group gap="xs" mt="xs" px="xs">
            {msg.meta.composition && <CompositionView composition={msg.meta.composition} />}
            <Tooltip label={msg.meta.source === 'local' ? "Обработано локально в браузере" : "Обработано на сервере"}>
              <Badge size="xs" variant="light" color={msg.meta.source === 'local' ? "teal" : "green"} radius="sm">
                {msg.meta.method} {msg.meta.source === 'local' && '(WASM)'}
              </Badge>
            </Tooltip>
            {msg.meta.self_check && <SelfCheckBadge check={msg.meta.self_check} />}
            {msg.meta.sources && msg.meta.sources.length > 0 && (
              <DigitalGenome 
                sources={msg.meta.sources} 
                hits={msg.meta.knowledge_hits || 0} 
                confidence={msg.meta.confidence} 
              />
            )}
            <ExplanationButton 
              thinking={msg.meta.thinking} 
              cognitive={msg.meta.cognitive} 
              method={msg.meta.method} 
            />
            <Text size="xs" c="dimmed">{(msg.meta.duration_ms || 0).toFixed(0)}ms</Text>
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
  );
}
