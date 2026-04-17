import { useState, useEffect } from "react";
import {
  Stack,
  Group,
  Text,
  Button,
  Paper,
  Progress,
  Badge,
  Table,
  ScrollArea,
  Title,
  RingProgress,
  Center,
  ThemeIcon,
  Alert,
  Tabs,
  Accordion
} from "@mantine/core";
import { Zap, Activity, Check, AlertTriangle, Clock, BarChart, History } from "lucide-react";
import { runQualityBenchmark, getQualityBenchmarkLatest, getQualityBenchmarkHistory, QualityBenchmarkResponse, QualityBenchmarkHistoryResponse } from "../api";

export function BenchmarkPanel() {
  const [latest, setLatest] = useState<QualityBenchmarkResponse | null>(null);
  const [history, setHistory] = useState<QualityBenchmarkHistoryResponse | null>(null);
  const [isRunning, setIsRunning] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const fetchLatest = async () => {
    try {
      const data = await getQualityBenchmarkLatest();
      setLatest(data);
    } catch (err: any) {
      console.warn("No latest benchmark found", err);
    }
  };

  const fetchHistory = async () => {
    try {
      const data = await getQualityBenchmarkHistory(10);
      setHistory(data);
    } catch (err: any) {
      console.warn("Failed to fetch history", err);
    }
  };

  useEffect(() => {
    fetchLatest();
    fetchHistory();
  }, []);

  const handleRun = async () => {
    setIsRunning(true);
    setError(null);
    try {
      const data = await runQualityBenchmark();
      setLatest(data);
      fetchHistory();
    } catch (err: any) {
      setError(err.message);
    } finally {
      setIsRunning(false);
    }
  };

  const renderStats = (report: QualityBenchmarkResponse) => {
    const scorePct = Math.round(report.score * 100);
    const passRatePct = Math.round(report.pass_rate * 100);

    return (
      <Stack gap="md">
        <Group grow>
          <Paper p="md" withBorder radius="md">
            <Stack gap={4} align="center">
              <RingProgress
                size={120}
                thickness={12}
                roundCaps
                sections={[{ value: scorePct, color: scorePct > 80 ? "teal" : "orange" }]}
                label={
                  <Center>
                    <Text fw={700} size="xl">{scorePct}%</Text>
                  </Center>
                }
              />
              <Text size="sm" fw={500} c="dimmed">Overall Score</Text>
            </Stack>
          </Paper>

          <Paper p="md" withBorder radius="md">
            <Stack gap="xs">
              <Group justify="space-between">
                <Text size="sm" c="dimmed">Pass Rate</Text>
                <Text fw={600}>{passRatePct}%</Text>
              </Group>
              <Progress value={passRatePct} color="indigo" size="sm" radius="xl" />
              
              <Group justify="space-between" mt="sm">
                <Text size="sm" c="dimmed">Latency (P95)</Text>
                <Text fw={600}>{(report.latency_p95_ms || 0).toFixed(0)}ms</Text>
              </Group>
              <Group justify="space-between">
                <Text size="sm" c="dimmed">Total Cases</Text>
                <Text fw={600}>{report.total}</Text>
              </Group>
            </Stack>
          </Paper>
        </Group>


        {report.categories && report.categories.length > 0 && (
          <>
            <Title order={5}>Categories</Title>
            <Stack gap="xs">
              {(Array.isArray(report.categories) ? report.categories : (console.log('DEBUG_DATA: categories', report.categories), [])).map((cat) => (
                <Paper key={cat.category} p="xs" withBorder radius="md">
                  <Group justify="space-between">
                    <Group gap="sm">
                      <ThemeIcon variant="light" size="sm" color={(cat.pass_rate || 0) > 0.8 ? "teal" : "orange"}>
                        <Activity size={12} />
                      </ThemeIcon>
                      <Text size="sm" fw={500}>{cat.category}</Text>
                    </Group>
                    <Group gap="xs">
                      <Text size="xs" c="dimmed">{cat.passed}/{cat.total}</Text>
                      <Badge size="xs" variant="light" color={(cat.pass_rate || 0) > 0.8 ? "teal" : "orange"}>
                        {((cat.pass_rate || 0) * 100).toFixed(0)}%
                      </Badge>
                    </Group>
                  </Group>
                  <Progress value={(cat.pass_rate || 0) * 100} color={(cat.pass_rate || 0) > 0.8 ? "teal" : "orange"} size="xs" mt="xs" />
                </Paper>
              ))}
            </Stack>
          </>
        )}
        </Stack>
        );
        };

        return (
        <Stack gap="md">
        <Group justify="space-between">
        <Title order={4}>Cognitive Benchmarks</Title>
        <Button 
          loading={isRunning} 
          onClick={handleRun} 
          leftSection={<Zap size={16} />}
          variant="filled"
          color="indigo"
          size="xs"
        >
          Run Benchmark
        </Button>
        </Group>

        {error && (
        <Alert color="red" title="Benchmark Error" icon={<AlertTriangle size={16} />}>
          {error}
        </Alert>
        )}

        {isRunning && (
        <Alert color="blue" title="Running..." icon={<Clock size={16} />}>
          Kolibri is processing benchmark questions. This may take up to 30 seconds.
        </Alert>
        )}

        <Tabs defaultValue="latest">
        <Tabs.List>
          <Tabs.Tab value="latest" leftSection={<BarChart size={14} />}>Latest Result</Tabs.Tab>
          <Tabs.Tab value="history" leftSection={<History size={14} />}>History</Tabs.Tab>
        </Tabs.List>

        <Tabs.Panel value="latest" pt="md">
          {latest ? (
            <Stack gap="md">
              {renderStats(latest)}

              {latest.details && latest.details.length > 0 && (
                <>
                  <Title order={5} mt="md">Details</Title>
                  <Accordion variant="separated">
                    {(Array.isArray(latest.details) ? latest.details : (console.log('DEBUG_DATA: details', latest.details), [])).map((point) => (
                      <Accordion.Item key={point.id} value={point.id}>
                        <Accordion.Control>
                          <Group justify="space-between" pr="md">
                            <Group gap="sm">
                              {point.passed ? <Check size={14} color="var(--mantine-color-teal-6)" /> : <AlertTriangle size={14} color="var(--mantine-color-red-6)" />}
                              <Text size="sm" fw={500}>{point.question}</Text>
                            </Group>
                            <Badge size="xs" variant="outline" color={point.passed ? "teal" : "red"}>
                              {point.category}
                            </Badge>
                          </Group>
                        </Accordion.Control>
                        <Accordion.Panel>
                          <Stack gap="xs">
                            {point.expected && (
                              <Text size="xs" c="dimmed">Expected: {point.expected}</Text>
                            )}
                            {point.reason && (
                              <Text size="sm" c={point.passed ? "teal" : "red"}>{point.reason}</Text>
                            )}
                            <Text size="xs" c="dimmed">Duration: {(point.duration_ms || 0).toFixed(0)}ms</Text>
                          </Stack>
                        </Accordion.Panel>
                      </Accordion.Item>
                    ))}
                  </Accordion>
                </>
              )}
            </Stack>
          ) : (
            <Center py="xl">
              <Stack align="center" gap="xs">
                <Text c="dimmed">No benchmark results yet.</Text>
                <Button variant="outline" size="xs" onClick={handleRun}>Run First Test</Button>
              </Stack>
            </Center>
          )}
        </Tabs.Panel>

        <Tabs.Panel value="history" pt="md">
          {history && history.items && history.items.length > 0 ? (
            <ScrollArea>
              <Table verticalSpacing="xs">
                <Table.Thead>
                  <Table.Tr>
                    <Table.Th>Date</Table.Th>
                    <Table.Th>Score</Table.Th>
                    <Table.Th>Pass Rate</Table.Th>
                    <Table.Th>P95 Latency</Table.Th>
                  </Table.Tr>
                </Table.Thead>
                <Table.Tbody>
                  {(history.items || []).map((item) => (
                    <Table.Tr key={item.run_id}>
                      <Table.Td>
                        <Text size="xs">{new Date(item.finished_at * 1000).toLocaleString()}</Text>
                      </Table.Td>
                      <Table.Td>
                        <Badge variant="light" color={(item.score || 0) > 0.8 ? "teal" : "orange"}>
                          {((item.score || 0) * 100).toFixed(1)}%
                        </Badge>
                      </Table.Td>
                      <Table.Td>
                        <Text size="xs">{((item.pass_rate || 0) * 100).toFixed(0)}% ({item.passed}/{item.total})</Text>
                      </Table.Td>
                      <Table.Td>
                        <Text size="xs">{(item.latency_p95_ms || 0).toFixed(0)}ms</Text>
                      </Table.Td>
                    </Table.Tr>
                  ))}
                </Table.Tbody>
              </Table>
            </ScrollArea>
          ) : (
            <Center py="xl">
              <Text c="dimmed">History is empty.</Text>
            </Center>
          )}
        </Tabs.Panel>
        </Tabs>
        </Stack>
        );
        }
