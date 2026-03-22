export function cleanAnswer(raw: string): string {
  return raw
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line.length > 0 && !line.startsWith("[Колибри] Режим установлен:"))
    .join("\n")
    .trim();
}

function normalizeFocusStem(token: string): string {
  const source = token.toLowerCase().trim();
  if (!source) return "";
  return source
    .replace(/(иями|ями|ами|ием|иеми|иями|ией|ией|ого|ему|ому|ыми|ими|ой|ей|ом|ем|ам|ям|ах|ях|ию|ью|ия|ие|ий|ая|ое|ые|ую|юю|у|ю|а|я|ы|и|е|о)$/u, "")
    .trim();
}

function extractDefinitionFocusStems(prompt: string): string[] {
  const normalized = prompt.trim().toLowerCase();
  if (!normalized) return [];
  const match = normalized.match(
    /(?:что\s+такое|кто\s+так(?:ой|ая|ие)|объясни|поясни|обьясни|расскажи(?:\s+подробно)?\s+(?:о|об|про)|что\s+ты\s+знаешь\s+(?:о|об|про)|что\s+изучает|чем\s+занимается|как\s+устроен(?:а|о)?|почему\s+важ(?:ен|на|но)|зачем\s+нуж(?:ен|на|но))\s+(.+)$/u,
  );
  if (!match) return [];
  return Array.from(
    new Set(
      match[1]
        .replace(/[?!.,:;]/gu, " ")
        .split(/\s+/u)
        .map((token) => normalizeFocusStem(token))
        .filter((token) => token.length >= 4),
    ),
  );
}

export function stripPresentationPrefix(text: string): string {
  let current = cleanAnswer(text);
  for (let i = 0; i < 12; i += 1) {
    const next = current
      .replace(/^(Журнал:\s*)/i, "")
      .replace(/^(Представлю это как короткий рассказ\.\s*)/i, "")
      .replace(/^(Отвечу мягко и бережно\.\s*)/i, "")
      .replace(/^(•\s*)/, "")
      .replace(/^[\p{Emoji_Presentation}\p{Extended_Pictographic}\s]+/gu, "")
      .trim();
    if (next === current) break;
    current = next;
  }
  return current;
}

export function sanitizeAssistantText(text: string): string {
  const cleaned = cleanAnswer(text);
  const stripped = stripPresentationPrefix(cleaned).trim();
  if (!stripped) return stripped;
  const journalCount = (cleaned.match(/Журнал:\s*/gi) ?? []).length;
  const hasJournal = /^Журнал:\s*/i.test(cleaned);
  const hasStoryPrefix = /Представлю это как короткий рассказ\./i.test(cleaned);
  if (journalCount > 1 || (hasJournal && hasStoryPrefix) || isLikelyGibberish(stripped)) {
    return "Не удалось получить корректный ответ. Повторите запрос.";
  }
  return stripped;
}

export function sanitizeContextText(text: string): string {
  return stripPresentationPrefix(text).trim();
}

function isSocialPrompt(prompt: string): boolean {
  const normalized = prompt.trim().toLowerCase();
  if (!normalized) return false;
  if (/^(привет|здравствуй|здравствуйте|добрый день|добрый вечер|доброе утро|пока|спасибо)$/.test(normalized)) {
    return true;
  }
  if (/^(как дела|как ты|как дела колибри|что делаешь)$/.test(normalized)) {
    return true;
  }
  return normalized.length <= 3 && /^[а-яё]+$/i.test(normalized);
}

export function isLikelyGibberish(text: string): boolean {
  const value = stripPresentationPrefix(text);
  const hasCyrillic = /[А-Яа-яЁё]/.test(value);
  const hasDigits = /\d/.test(value);
  const lettersOnly = value.replace(/[^A-Za-zА-Яа-яЁё]/g, "");
  const latinLetters = (lettersOnly.match(/[A-Za-z]/g) ?? []).length;
  const cyrLetters = (lettersOnly.match(/[А-Яа-яЁё]/g) ?? []).length;
  if (hasDigits || hasCyrillic) return false;
  return latinLetters >= 6 && cyrLetters === 0;
}

export function looksWeak(prompt: string, text: string): boolean {
  const low = text.toLowerCase();
  const core = stripPresentationPrefix(text);
  if (
    !text.trim() ||
    low.includes("ещё учится") ||
    low.includes("не готов ответить") ||
    low.includes("завершил работу без вывода")
  ) {
    return true;
  }

  const promptHasCyrillic = /[А-Яа-яЁё]/.test(prompt);
  const coreHasCyrillic = /[А-Яа-яЁё]/.test(core);
  const coreHasDigits = /\d/.test(core);
  if (promptHasCyrillic && !coreHasCyrillic && !coreHasDigits) {
    return true;
  }

  if (promptHasCyrillic && isLikelyGibberish(core)) {
    return true;
  }

  if (isSocialPrompt(prompt)) {
    const conversational =
      /(привет|здрав|хорош|норм|отлич|спасибо|рад|помочь|дела|чем могу)/i.test(core);
    if (!conversational) {
      return true;
    }
  }

  const focusStems = extractDefinitionFocusStems(prompt);
  if (focusStems.length > 0) {
    const answerLow = core.toLowerCase();
    const hasFocus = focusStems.some((stem) => answerLow.includes(stem));
    if (!hasFocus) {
      return true;
    }
  }

  return false;
}

export function sanitizeAssistantTurn(prompt: string, text: string): string {
  const sanitized = sanitizeAssistantText(text);
  if (!sanitized) return sanitized;
  if (looksWeak(prompt, sanitized)) {
    return "Не удалось получить корректный ответ. Повторите запрос.";
  }
  return sanitized;
}
