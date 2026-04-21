import React, { useState, useEffect, useRef } from 'react';
import {
  MantineProvider,
  Container,
  TextInput,
  Button,
  Paper,
  Text,
  ScrollArea,
  Group,
  Stack,
  Code,
  Badge,
  createTheme
} from '@mantine/core';
import { KolibriBridge } from './lib/kolibriBridge';
import type { KolibriBridgeResult } from './lib/kolibriBridge';
import '@mantine/core/styles.css';

const theme = createTheme({
  primaryColor: 'cyan',
});

interface Message {
  role: 'user' | 'assistant';
  content: string;
  stats?: KolibriBridgeResult;
}

export default function App() {
  const [messages, setMessages] = useState<Message[]>([]);
  const [input, setInput] = useState('');
  const [bridge, setBridge] = useState<KolibriBridge | null>(null);
  const viewport = useRef<HTMLDivElement>(null);

  useEffect(() => {
    KolibriBridge.load().then(setBridge);
    setMessages([{ role: 'assistant', content: 'Привет! Я Kolibri AI. Ядро C-Core готово.' }]);
  }, []);

  const scrollToBottom = () => {
    if (viewport.current) {
      viewport.current.scrollTo({ top: viewport.current.scrollHeight, behavior: 'smooth' });
    }
  };

  const handleSend = () => {
    if (!input.trim() || !bridge) return;
    const userMsg: Message = { role: 'user', content: input };
    setMessages(prev => [...prev, userMsg]);

    const result = bridge.query(input);
    setMessages(prev => [...prev, {
      role: 'assistant',
      content: result.response,
      stats: result
    }]);
    setInput('');
    setTimeout(scrollToBottom, 50);
  };

  return (
    <MantineProvider theme={theme} defaultColorScheme="dark">
      <Container size="sm" py="xl" style={{ height: '100vh', display: 'flex', flexDirection: 'column' }}>
        <Group mb="md" justify="space-between">
          <Group>
            <span style={{ fontSize: '32px' }}>🐦</span>
            <div>
              <Text size="xl" fw={900} c="cyan">KOLIBRI AI</Text>
              <Text size="xs" c="dimmed">C-Core Native Reasoning</Text>
            </div>
          </Group>
          {bridge ? <Badge color="green">ONLINE</Badge> : <Badge color="yellow">LOADING...</Badge>}
        </Group>

        <Paper withBorder p="md" style={{ flex: 1, display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
          <ScrollArea viewportRef={viewport} style={{ flex: 1 }} p="sm">
            <Stack>
              {messages.map((m, i) => (
                <div key={i} style={{ alignSelf: m.role === 'user' ? 'flex-end' : 'flex-start', maxWidth: '90%' }}>
                  <Paper p="sm" radius="md" bg={m.role === 'user' ? 'blue.9' : 'gray.9'}>
                    <Text size="sm">{m.content}</Text>
                    {m.stats && (
                      <Stack mt="xs" gap={2}>
                        <Text size="min" c="dimmed" style={{ fontSize: '10px' }}>
                          ⚡ {m.stats.duration_ms.toFixed(2)}ms | 🎯 {m.stats.confidence}
                        </Text>
                        {m.stats.thinking && <Code block style={{ fontSize: '9px' }}>{m.stats.thinking}</Code>}
                      </Stack>
                    )}
                  </Paper>
                </div>
              ))}
            </Stack>
          </ScrollArea>

          <Group mt="md">
            <TextInput
              placeholder="Спроси о проекте..."
              style={{ flex: 1 }}
              value={input}
              onChange={(e) => setInput(e.currentTarget.value)}
              onKeyDown={(e) => e.key === 'Enter' && handleSend()}
            />
            <Button onClick={handleSend} color="cyan">Отправить</Button>
          </Group>
        </Paper>
      </Container>
    </MantineProvider>
  );
}
