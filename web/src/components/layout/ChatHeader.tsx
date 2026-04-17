import { Group, Burger, ActionIcon, Badge, Text, Tooltip } from "@mantine/core";
import { Sun, Moon, Settings, Cpu } from "lucide-react";

export function ChatHeader({ opened, toggle, useLocalModel, isDark, toggleColorScheme, openSettings, bridgeStatus, profile }: any) {
  return (
    <Group h={60} px={{ base: "sm", sm: "md" }} justify="space-between" className="topbar">
      <Group gap="sm" wrap="nowrap">
        <Burger opened={opened} onClick={toggle} size="sm" />
        <Badge variant="light" color={useLocalModel ? "teal" : "indigo"}>{useLocalModel ? "Local Core" : "Hybrid"}</Badge>
        <Badge visibleFrom="xs" variant="dot" color={bridgeStatus === "ready" ? "teal" : "orange"} leftSection={<Cpu size={12} />}>
          WASM {bridgeStatus}
        </Badge>
        <Text visibleFrom="sm" size="sm" c="dimmed">Профиль: {profile}</Text>
      </Group>
      <Group gap="sm">
        <Tooltip label="Сменить тему">
          <ActionIcon variant="subtle" onClick={toggleColorScheme}>{isDark ? <Sun size={18} /> : <Moon size={18} />}</ActionIcon>
        </Tooltip>
        <Tooltip label="Настройки">
          <ActionIcon variant="subtle" onClick={openSettings}><Settings size={18} /></ActionIcon>
        </Tooltip>
      </Group>
    </Group>
  );
}
