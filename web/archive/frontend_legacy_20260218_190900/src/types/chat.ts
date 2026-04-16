export type ChatRole = "user" | "assistant";

import type { KnowledgeSnippet } from "./knowledge";

export interface ChatMessage {
  id: string;
  role: ChatRole;
  content: string;
  timestamp: string;
  isoTimestamp?: string;
  modeLabel?: string;
  modeValue?: string;

  context?: KnowledgeSnippet[];
  contextError?: string;

}
