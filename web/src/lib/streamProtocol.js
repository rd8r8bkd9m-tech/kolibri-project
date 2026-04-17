export function consumeSseBuffer(buffer) {
  const chunks = buffer.split(/\r?\n\r?\n/);
  const remainder = chunks.pop() ?? "";
  const events = chunks.map(parseSseBlock).filter(Boolean);

  return { events, remainder };
}

export function extractStreamText(payload) {
  if (typeof payload === "string") return payload;
  if (!payload || typeof payload !== "object") return "";

  if (typeof payload.text === "string") return payload.text;
  if (typeof payload.token === "string") return payload.token;
  if (typeof payload.delta === "string") return payload.delta;
  if (typeof payload.content === "string") return payload.content;
  if (typeof payload.response === "string") return payload.response;

  return "";
}

function parseSseBlock(block) {
  const lines = block.split(/\r?\n/);
  let eventType = "message";
  const dataLines = [];

  for (const line of lines) {
    if (!line || line.startsWith(":")) continue;

    const separatorIndex = line.indexOf(":");
    const field = separatorIndex === -1 ? line : line.slice(0, separatorIndex);
    const rawValue = separatorIndex === -1 ? "" : line.slice(separatorIndex + 1);
    const value = rawValue.startsWith(" ") ? rawValue.slice(1) : rawValue;

    if (field === "event") {
      eventType = value || "message";
    } else if (field === "data") {
      dataLines.push(value);
    }
  }

  if (dataLines.length === 0) return null;

  const rawData = dataLines.join("\n");
  let payload;

  try {
    payload = JSON.parse(rawData);
  } catch {
    payload = { raw: rawData };
  }

  return { eventType, payload };
}
