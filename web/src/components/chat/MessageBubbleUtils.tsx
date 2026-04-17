import { Paper, Box, Text, Stack } from "@mantine/core";
import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";
import { CompositionResult } from "../../api";

export function CompositionView({ composition }: { composition: CompositionResult }) {
  return (
    <Paper withBorder p="xs" radius="md" mt="xs" style={{ backgroundColor: 'var(--mantine-color-dark-8)' }}>
      <Text size="xs" fw={600} mb={4} c="dimmed" tt="uppercase">Синтез из {composition.fragments.length} источников:</Text>
      <Stack gap="xs">
        {(Array.isArray(composition.fragments) ? composition.fragments : (console.log('DEBUG_DATA: fragments', composition.fragments), [])).map((f, i) => (
          <Box key={i} p={6} style={{ borderLeft: '3px solid var(--mantine-color-indigo-5)', backgroundColor: 'var(--mantine-color-dark-7)' }}>
            <Text size="xs" fw={500} c="white">{f.question}</Text>
            <Text size="xs" c="gray.4">{f.answer}</Text>
          </Box>
        ))}
      </Stack>
    </Paper>
  );
}

export function MarkdownContent({ content, isUser }: { content: string; isUser: boolean }) {
  if (isUser) return <Text size="md">{content}</Text>;
  
  return (
    <Box className="markdown-content">
      <ReactMarkdown remarkPlugins={[remarkGfm]}>{content}</ReactMarkdown>
    </Box>
  );
}
