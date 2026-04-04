import { useEffect } from "react";
import { useChatStore } from "@/store/useChatStore";
import { useAccountPreferencesQuery, useAccountProfileQuery, useAuthStatusQuery } from "@/features/account/query";

export function useAccountBootstrap() {
  const theme = useChatStore((s) => s.theme);
  const setTheme = useChatStore((s) => s.setTheme);
  const model = useChatStore((s) => s.model);
  const setModel = useChatStore((s) => s.setModel);
  const persona = useChatStore((s) => s.persona);
  const setPersona = useChatStore((s) => s.setPersona);
  const memoryEnabled = useChatStore((s) => s.memoryEnabled);
  const setMemoryEnabled = useChatStore((s) => s.setMemoryEnabled);

  const authStatus = useAuthStatusQuery();
  const profileQuery = useAccountProfileQuery();
  const preferencesQuery = useAccountPreferencesQuery();

  useEffect(() => {
    const serverTheme = preferencesQuery.data?.theme;
    if (serverTheme && serverTheme !== theme) {
      setTheme(serverTheme);
    }

    const serverPersona = preferencesQuery.data?.persona;
    if (serverPersona && serverPersona !== persona) {
      setPersona(serverPersona);
    }

    const serverMemoryEnabled = preferencesQuery.data?.memory_enabled;
    if (typeof serverMemoryEnabled === "boolean" && serverMemoryEnabled !== memoryEnabled) {
      setMemoryEnabled(serverMemoryEnabled);
    }

    const serverModel = preferencesQuery.data?.model?.trim();
    if (serverModel && serverModel !== model) {
      setModel(serverModel as typeof model);
    }
  }, [
    memoryEnabled,
    model,
    persona,
    preferencesQuery.data?.memory_enabled,
    preferencesQuery.data?.model,
    preferencesQuery.data?.persona,
    preferencesQuery.data?.theme,
    setMemoryEnabled,
    setModel,
    setPersona,
    setTheme,
    theme,
  ]);

  return {
    authStatus,
    profileQuery,
    preferencesQuery,
  };
}
