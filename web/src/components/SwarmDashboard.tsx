import { useState, useEffect } from "react";
import { Stack, Group, Paper, Text, Button, Badge, SimpleGrid, RingProgress, Center, Title, Alert, ThemeIcon } from "@mantine/core";
import { Network, Server, Play, Activity, Cpu, AlertCircle, RefreshCw } from "lucide-react";
import { getSwarmStatus, startSwarm, SwarmStatusResponse } from "../api";
import { useInterval } from "@mantine/hooks";

export function SwarmDashboard() {
  const [status, setStatus] = useState<SwarmStatusResponse | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const fetchStatus = async () => {
    try {
      const data = await getSwarmStatus();
      setStatus(data);
      setError(null);
    } catch (err: any) {
      // Don't show loud error if it's just offline
      if (err.message.includes("404")) {
        setStatus({ active: false });
      } else {
        setError(err.message);
      }
    }
  };

  const interval = useInterval(fetchStatus, 3000);

  useEffect(() => {
    fetchStatus();
    interval.start();
    return interval.stop;
  }, []);

  const handleStart = async () => {
    setLoading(true);
    try {
      await startSwarm();
      await fetchStatus();
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  };

  const renderActiveDashboard = (data: SwarmStatusResponse) => {
    const nodes = data.nodes_active || data.active_nodes || data.total_nodes || 0;
    const isUplift = nodes > 1;

    return (
      <Stack gap="lg">
        <Alert color="teal" icon={<Activity size={16} />} title="Swarm Network Online">
          Децентрализованная сеть знаний Kolibri активна. Узлы обмениваются kpack-данными.
        </Alert>

        <SimpleGrid cols={{ base: 1, sm: 3 }} spacing="md">
          <Paper withBorder p="md" radius="md">
            <Group justify="space-between" mb="xs">
              <Text size="sm" c="dimmed" fw={500}>Активные узлы</Text>
              <ThemeIcon variant="light" color="indigo"><Server size={14}/></ThemeIcon>
            </Group>
            <Group align="flex-end" gap="xs">
              <Text fw={700} size="xl">{nodes}</Text>
              <Text size="sm" c="dimmed" mb={4}>/ {data.total_nodes || 50}</Text>
            </Group>
          </Paper>

          <Paper withBorder p="md" radius="md">
            <Group justify="space-between" mb="xs">
              <Text size="sm" c="dimmed" fw={500}>Эпоха обучения (Gen)</Text>
              <ThemeIcon variant="light" color="blue"><RefreshCw size={14}/></ThemeIcon>
            </Group>
            <Text fw={700} size="xl">{data.generation || data.current_generation || 0}</Text>
          </Paper>

          <Paper withBorder p="md" radius="md">
            <Group justify="space-between" mb="xs">
              <Text size="sm" c="dimmed" fw={500}>Лучший Фитнес</Text>
              <ThemeIcon variant="light" color="teal"><Activity size={14}/></ThemeIcon>
            </Group>
            <Text fw={700} size="xl">
              {data.best_fitness ? data.best_fitness.toFixed(4) : "0.0000"}
            </Text>
          </Paper>
        </SimpleGrid>

        <Paper withBorder p="md" radius="md" bg="var(--mantine-color-indigo-0)">
          <Group justify="space-between">
            <Stack gap={4}>
              <Group gap="xs">
                <Network size={18} color="var(--mantine-color-indigo-6)" />
                <Title order={5}>Swarm Uplift Status</Title>
              </Group>
              <Text size="sm" c="dimmed">Эффект масштаба от объединения вычислительных мощностей.</Text>
            </Stack>
            {isUplift ? (
              <Badge size="lg" color="teal" variant="filled">Active: +{((nodes || 0) * 0.8).toFixed(1)}x Speed</Badge>
            ) : (
              <Badge size="lg" color="orange" variant="light">Waiting for Peers</Badge>
            )}
          </Group>
        </Paper>
      </Stack>
    );
  };

  return (
    <Stack gap="md" p="md">
      <Group justify="space-between">
        <Group gap="sm">
          <ThemeIcon size="lg" radius="md" color="indigo" variant="light">
            <Network size={20} />
          </ThemeIcon>
          <div>
            <Title order={3}>Swarm Network</Title>
            <Text size="xs" c="dimmed">Decentralized Knowledge Validation</Text>
          </div>
        </Group>
        
        {status?.active && (
          <Badge color="teal" variant="dot" size="lg">Online</Badge>
        )}
      </Group>

      {error && (
        <Alert color="red" title="Error" icon={<AlertCircle size={16} />}>
          {error}
        </Alert>
      )}

      {!status ? (
        <Center p="xl"><Text c="dimmed">Подключение к сети...</Text></Center>
      ) : status.active ? (
        renderActiveDashboard(status)
      ) : (
        <Paper withBorder p="xl" radius="md" style={{ textAlign: 'center' }}>
          <Stack align="center" gap="md">
            <ThemeIcon size={64} radius="100%" color="gray" variant="light">
              <Cpu size={32} />
            </ThemeIcon>
            <Title order={4}>Сеть Swarm не запущена</Title>
            <Text c="dimmed" size="sm" maw={400}>
              Локальный узел находится в спящем режиме. Запустите Swarm Runtime, чтобы начать децентрализованный поиск формул и обмен kpack-знаниями.
            </Text>
            <Button 
              size="md" 
              color="indigo" 
              leftSection={<Play size={16} />} 
              onClick={handleStart}
              loading={loading}
              mt="sm"
            >
              Bootstrap Swarm Node
            </Button>
          </Stack>
        </Paper>
      )}
    </Stack>
  );
}
