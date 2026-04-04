import { useQuery } from "@tanstack/react-query";
import {
  fetchAccountPreferences,
  fetchAccountProfile,
  fetchAuthStatus,
  fetchConversationSessions,
  fetchConversationTurns,
} from "@/api";

export const accountQueryKeys = {
  authStatus: ["auth-status"] as const,
  accountProfile: ["account-profile"] as const,
  accountPreferences: ["account-preferences"] as const,
  conversationSessions: ["conversation-sessions"] as const,
  conversationTurnsRoot: (conversationId: string) => ["conversation-turns", conversationId] as const,
  conversationTurns: (conversationId: string, limit: number) => ["conversation-turns", conversationId, limit] as const,
};

export function useAuthStatusQuery() {
  return useQuery({
    queryKey: accountQueryKeys.authStatus,
    queryFn: fetchAuthStatus,
    retry: 0,
  });
}

export function useAccountProfileQuery() {
  return useQuery({
    queryKey: accountQueryKeys.accountProfile,
    queryFn: fetchAccountProfile,
    retry: 0,
  });
}

export function useAccountPreferencesQuery() {
  return useQuery({
    queryKey: accountQueryKeys.accountPreferences,
    queryFn: fetchAccountPreferences,
    retry: 0,
  });
}

export function useConversationSessionsQuery(limit = 100) {
  return useQuery({
    queryKey: [...accountQueryKeys.conversationSessions, limit] as const,
    queryFn: () => fetchConversationSessions(limit),
    retry: 0,
  });
}

export function useConversationTurnsQuery(conversationId: string, limit = 120) {
  return useQuery({
    queryKey: accountQueryKeys.conversationTurns(conversationId, limit),
    queryFn: () => fetchConversationTurns(conversationId, limit),
    enabled: Boolean(conversationId),
    retry: 0,
  });
}
