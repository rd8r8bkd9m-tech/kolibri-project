export interface ParsedSseEvent {
  eventType: string;
  payload: Record<string, unknown>;
}

export function consumeSseBuffer(input: string): {
  events: ParsedSseEvent[];
  remainder: string;
};

export function extractStreamText(payload: Record<string, unknown>): string;
