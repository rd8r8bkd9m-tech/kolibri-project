export interface StreamEvent {
  eventType: string;
  payload: unknown;
}

export interface StreamParseResult {
  events: StreamEvent[];
  remainder: string;
}

export function consumeSseBuffer(buffer: string): StreamParseResult;

export function extractStreamText(payload: unknown): string;
