#!/usr/bin/env node
const http = require('http');

const question = process.argv.slice(2).join(' ');
if (!question) {
  console.error('Usage: node scripts/generate_script.js <question>');
  process.exit(1);
}

function fetchKnowledge(q) {
  return new Promise((resolve, reject) => {
    const url = new URL('http://127.0.0.1:8000/api/knowledge/search');
    url.searchParams.set('q', q);
    url.searchParams.set('limit', '3');
    http.get(url, (res) => {
      let data = '';
      res.on('data', (chunk) => (data += chunk));
      res.on('end', () => {
        try {
          const parsed = JSON.parse(data);
          resolve(Array.isArray(parsed.snippets) ? parsed.snippets : []);
        } catch (error) {
          reject(error);
        }
      });
    }).on('error', reject);
  });
}

function escapeScriptString(value) {
  return value
    .replace(/\\/g, '\\\\')
    .replace(/"/g, '\\"')
    .replace(/\r?\n/g, '\\n');
}

async function main() {
  const context = await fetchKnowledge(question);
  const trimmed = question.trim();
  const lines = ['начало:'];
  lines.push(`    переменная question = "${escapeScriptString(trimmed)}"`);
  lines.push(`    показать "Вопрос: ${escapeScriptString(trimmed)}"`);
  const uniqueAnswers = new Set();
  context.forEach((snippet, index) => {
    const answer = (snippet.content || '').trim();
    if (!answer) {
      return;
    }
    const normalised = answer.length > 400 ? `${answer.slice(0, 397)}…` : answer;
    if (uniqueAnswers.has(normalised)) {
      return;
    }
    uniqueAnswers.add(normalised);
    const source = snippet.source || snippet.id || `source_${index + 1}`;
    lines.push(`    переменная source_${index + 1} = "${escapeScriptString(source)}"`);
    lines.push(`    обучить связь "${escapeScriptString(trimmed)}" -> "${escapeScriptString(normalised)}"`);
    const sourceLabel = snippet.title || snippet.id || source;
    lines.push(`    показать "Источник ${index + 1}: ${escapeScriptString(sourceLabel)}"`);
  });
  if (uniqueAnswers.size === 0) {
    lines.push(`    обучить связь "${escapeScriptString(trimmed)}" -> "${escapeScriptString('Информации недостаточно')}"`);
  }
  lines.push('    создать формулу ответ из "ассоциация"');
  lines.push('    вызвать эволюцию');
  lines.push(`    оценить ответ на задаче "${escapeScriptString(trimmed)}"`);
  lines.push('    показать итог');
  lines.push('конец.');
  console.log(lines.join('\n'));
}

main().catch((error) => {
  console.error('Error:', error);
  process.exit(1);
});
