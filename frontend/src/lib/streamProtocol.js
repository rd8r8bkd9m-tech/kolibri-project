/**
 * @typedef {{eventType: string, payload: Record<string, unknown>}} ParsedSseEvent
 */

/**
 * Parse a buffered SSE stream into complete event payloads while preserving the tail remainder.
 * Supports both CRLF and LF separators.
 *
 * @param {string} input
 * @returns {{events: ParsedSseEvent[], remainder: string}}
 */
export function consumeSseBuffer(input) {
  const normalized = input.replace(/\r\n/g, "\n");
  const blocks = normalized.split("\n\n");
  const remainder = blocks.pop() ?? "";
  /** @type {ParsedSseEvent[]} */
  const events = [];

  for (const block of blocks) {
    const trimmed = block.trim();
    if (!trimmed) continue;

    let eventType = "";
    /** @type {string[]} */
    const dataLines = [];

    for (const line of trimmed.split("\n")) {
      if (line.startsWith("event:")) {
        eventType = line.slice("event:".length).trim();
      } else if (line.startsWith("data:")) {
        dataLines.push(line.slice("data:".length).trim());
      }
    }

    if (!eventType || dataLines.length === 0) continue;

    try {
      const payload = JSON.parse(dataLines.join("\n"));
      if (payload && typeof payload === "object") {
        events.push({ eventType, payload });
      }
    } catch {
      // Ignore malformed SSE chunks and continue parsing the rest.
    }
  }

  return { events, remainder };
}

/**
 * Normalize token payloads across legacy and canonical SSE contracts.
 *
 * @param {Record<string, unknown>} payload
 * @returns {string}
 */
export function extractStreamText(payload) {
  if (typeof payload.text === "string") return payload.text;
  if (typeof payload.token === "string") return payload.token;
  return "";
}
