import { Group, Badge, Text, Tooltip, Stack, ActionIcon, Popover, Divider, ScrollArea } from "@mantine/core";
import { Dna, Database, Globe, Fingerprint, Info } from "lucide-react";

interface DigitalGenomeProps {
  sources: string[];
  hits: number;
  confidence: number;
}

export function DigitalGenome({ sources, hits, confidence }: DigitalGenomeProps) {
  if (!sources || sources.length === 0) return null;

  return (
    <Popover width={350} position="bottom-start" withArrow shadow="md">
      <Popover.Target>
        <Badge 
          variant="dot" 
          color="indigo" 
          size="xs" 
          style={{ cursor: 'pointer' }}
          leftSection={<Dna size={10} />}
        >
          Digital Genome
        </Badge>
      </Popover.Target>
      <Popover.Dropdown p="sm">
        <Stack gap="xs">
          <Group justify="space-between">
            <Group gap="xs">
              <Fingerprint size={16} color="var(--mantine-color-indigo-6)" />
              <Text fw={600} size="sm">Отпечаток знаний</Text>
            </Group>
            <Badge size="xs" color="indigo" variant="light">{((confidence || 0) * 100).toFixed(0)}% trust</Badge>
          </Group>
          
          <Divider />
          
          <Text size="xs" c="dimmed" mb={4}>Ответ синтезирован на основе {hits} фрагментов памяти:</Text>
          
          <ScrollArea h={120} offsetScrollbars>
            <Stack gap={4}>
              {(Array.isArray(sources) ? sources : (console.log('DEBUG_DATA: sources', sources), [])).map((src, i) => (
                <Group key={i} gap="xs" wrap="nowrap">
                  {src.startsWith('http') ? <Globe size={12} color="blue" /> : <Database size={12} color="gray" />}
                  <Text size="10px" truncate="end" c="dimmed" style={{ flex: 1 }}>
                    {src}
                  </Text>
                </Group>
              ))}
            </Stack>
          </ScrollArea>
          
          <Group gap={4} mt="xs">
            <Info size={12} color="gray" />
            <Text size="10px" c="dimmed" fs="italic">
              Kolibri гарантирует прослеживаемость каждого факта в этом ответе.
            </Text>
          </Group>
        </Stack>
      </Popover.Dropdown>
    </Popover>
  );
}
