import { Badge, Tooltip, Group, Text, Popover, Stack, ThemeIcon } from "@mantine/core";
import { ShieldCheck, ShieldAlert, Info, Brain, Zap } from "lucide-react";

interface SelfCheckProps {
  check?: {
    passed: boolean;
    reason: string;
    logic_score: number;
    hallucination_risk: number;
    verified_by?: string;
  };
}

export function SelfCheckBadge({ check }: SelfCheckProps) {
  if (!check) return null;

  const passed = check.passed;
  const color = passed ? "teal" : "red";
  const Icon = passed ? ShieldCheck : ShieldAlert;

  return (
    <Popover width={300} position="bottom" withArrow shadow="md">
      <Popover.Target>
        <Badge 
          size="xs" 
          variant="filled" 
          color={color} 
          radius="sm" 
          leftSection={<Icon size={10} />}
          style={{ cursor: 'pointer' }}
        >
          {passed ? "Verified" : "Logic Warning"}
        </Badge>
      </Popover.Target>
      <Popover.Dropdown p="md">
        <Stack gap="xs">
          <Group gap="sm">
            <ThemeIcon color={color} variant="light" size="sm">
              <Brain size={12} />
            </ThemeIcon>
            <Text fw={600} size="sm">Self-Verification Result</Text>
          </Group>
          
          <Text size="xs" c={passed ? "teal" : "red"} fw={500}>
            {check.reason}
          </Text>

          <Group justify="space-between" mt="xs">
            <Text size="xs" c="dimmed">Logic Score:</Text>
            <Text size="xs" fw={600}>{((check.logic_score || 0) * 100).toFixed(0)}%</Text>
          </Group>
          
          <Group justify="space-between">
            <Text size="xs" c="dimmed">Hallucination Risk:</Text>
            <Text size="xs" fw={600} c={(check.hallucination_risk || 0) > 0.3 ? "orange" : "dimmed"}>
              {((check.hallucination_risk || 0) * 100).toFixed(0)}%
            </Text>
          </Group>

          {check.verified_by && (
            <Group gap={4} mt="xs">
              <Zap size={10} color="var(--mantine-color-indigo-6)" />
              <Text size="10px" c="dimmed" tt="uppercase">Verified by: {check.verified_by}</Text>
            </Group>
          )}
        </Stack>
      </Popover.Dropdown>
    </Popover>
  );
}
