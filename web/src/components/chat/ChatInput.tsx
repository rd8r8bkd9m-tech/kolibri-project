import { Box, Paper, Group, ActionIcon, Tooltip, Textarea, Text, Badge } from "@mantine/core";
import { Send, Square, Zap } from "lucide-react";

export function ChatInput({ input, setInput, onSubmit, onCancel, isLoading, isDark, useLocalModel }: any) {
  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === "Enter") {
        if (e.ctrlKey) {
            onSubmit();
        } else if (!e.shiftKey) {
            e.preventDefault();
            onSubmit();
        }
    }
  };

  return (
    <Box p={{ base: "sm", sm: "md" }} className="composer-wrap">
      <Box maw={900} mx="auto">
        <Paper p="xs" radius="lg" bg={isDark ? "dark.7" : "white"} withBorder shadow={isDark ? "none" : "sm"} className="composer">
          <Group justify="space-between" px="xs" pb={6}>
            <Group gap={6}>
              <Zap size={14} color={useLocalModel ? "var(--mantine-color-teal-5)" : "var(--mantine-color-indigo-5)"} />
              <Text size="xs" c="dimmed">{useLocalModel ? "Локальный режим" : "Гибридный режим"}</Text>
            </Group>
            <Badge size="xs" variant="light" color={isLoading ? "orange" : "gray"}>
              {isLoading ? "генерация" : "Enter для отправки"}
            </Badge>
          </Group>
          <Group wrap="nowrap" gap="xs" align="flex-end">
            <Textarea
              flex={1}
              autosize
              minRows={1}
              maxRows={7}
              value={input}
              onChange={(e) => setInput(e.currentTarget.value)}
              variant="unstyled"
              px="xs"
              placeholder={useLocalModel ? "Спросите локальное ядро..." : "Спросите Kolibri..."}
              onKeyDown={handleKeyDown}
              disabled={isLoading}
            />
            <Tooltip label="Отправить">
              <ActionIcon size={40} radius="md" color={useLocalModel ? "teal" : "indigo"} onClick={onSubmit} disabled={!input.trim() || isLoading}><Send size={18} /></ActionIcon>
            </Tooltip>
            {isLoading && (
                <Tooltip label="Остановить ответ">
                    <ActionIcon size={40} radius="md" color="red" variant="light" onClick={onCancel}><Square size={14} fill="currentColor" /></ActionIcon>
                </Tooltip>
            )}
          </Group>
        </Paper>
      </Box>
    </Box>
  );
}
