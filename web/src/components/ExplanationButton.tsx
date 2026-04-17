import { ActionIcon, Modal, Text, Stack, Group, Title, ThemeIcon, Code, List, Divider, ScrollArea, Badge, Paper, Tooltip } from "@mantine/core";
import { useDisclosure } from "@mantine/hooks";
import { HelpCircle, BrainCircuit, StepForward, Terminal, Quote } from "lucide-react";

interface ExplanationButtonProps {
  thinking?: string;
  cognitive?: Record<string, any>;
  method?: string;
}

export function ExplanationButton({ thinking, cognitive, method }: ExplanationButtonProps) {
  const [opened, { open, close }] = useDisclosure(false);

  if (!thinking && !cognitive) return null;

  return (
    <>
      <Tooltip label="Как это работает?">
        <ActionIcon variant="subtle" size="xs" color="gray" onClick={open}>
          <HelpCircle size={14} />
        </ActionIcon>
      </Tooltip>

      <Modal opened={opened} onClose={close} title="Explainable AI: Процесс рассуждения" size="lg">
        <ScrollArea h={500} offsetScrollbars>
          <Stack gap="md">
            <Group>
              <ThemeIcon variant="light" color="indigo"><BrainCircuit size={18}/></ThemeIcon>
              <Text fw={600}>Метод: {method || "Нейро-символьное рассуждение"}</Text>
            </Group>

            <Divider label="Ход мыслей (Thinking Trace)" labelPosition="center" />
            
            <Paper p="sm" bg="gray.0" withBorder>
              <Stack gap="xs">
                {thinking ? (
                  (Array.isArray(thinking.split('\n')) ? thinking.split('\n') : (console.log('DEBUG_DATA: thinking', thinking), [])).map((step, i) => (
                    <Group key={i} gap="xs" align="flex-start" wrap="nowrap">
                      <StepForward size={14} color="var(--mantine-color-indigo-6)" style={{ marginTop: 4 }} />
                      <Text size="sm">{step}</Text>
                    </Group>
                  ))
                ) : (
                  <Text size="sm" c="dimmed">Процесс рассуждения скрыт или обработан атомарно.</Text>
                )}
              </Stack>
            </Paper>

            {cognitive && (
              <>
                <Divider label="Когнитивные метрики" labelPosition="center" />
                <Stack gap="xs">
                  {(Array.isArray(Object.entries(cognitive)) ? Object.entries(cognitive) : (console.log('DEBUG_DATA: cognitive', cognitive), [])).map(([key, val]) => (
                    <Group key={key} justify="space-between">
                      <Text size="xs" tt="uppercase" fw={700} c="dimmed">{key.replace(/_/g, ' ')}</Text>
                      <Badge variant="dot" color="blue" size="sm">{String(val)}</Badge>
                    </Group>
                  ))}
                </Stack>
              </>
            )}

            <Divider />
            
            <Group gap="xs">
              <Quote size={14} color="gray" />
              <Text size="xs" c="dimmed">
                Kolibri не просто генерирует текст. Каждое слово проходит через фильтр логического ядра и сверяется с базой знаний.
              </Text>
            </Group>
          </Stack>
        </ScrollArea>
      </Modal>
    </>
  );
}
