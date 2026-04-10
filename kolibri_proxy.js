#!/usr/bin/env node
/**
 * kolibri_proxy.js — Kolibri AI Smart Proxy
 * 
 * Architecture:
 * 1. Fast local Q&A (BM25 scoring over facts)
 * 2. Math calculation engine
 * 3. Smart fallback responses
 * 
 * Usage: node kolibri_proxy.js [port]
 */

const http = require('http');

const PORT = process.argv[2] || 8003;
const C_SERVER = 'http://localhost:8001';

/* ===== LOCAL KNOWLEDGE BASE ===== */
const FACTS = [
    { q: 'Франция', a: 'Столица Франции — Париж' },
    { q: 'Япония', a: 'Столица Японии — Токио' },
    { q: 'Германия', a: 'Столица Германии — Берлин' },
    { q: 'Канада', a: 'Столица Канады — Оттава' },
    { q: 'Австралия', a: 'Столица Австралии — Канберра' },
    { q: 'Великобритания', a: 'Столица Великобритании — Лондон' },
    { q: 'Россия', a: 'Столица России — Москва' },
    { q: 'Италия', a: 'Столица Италии — Рим' },
    { q: 'Испания', a: 'Столица Испании — Мадрид' },
    { q: 'Китай', a: 'Столица Китая — Пекин' },
    { q: 'США', a: 'Столица США — Вашингтон' },
    { q: 'спутник', a: 'Спутник Земли — Луна' },
    { q: 'планет', a: 'Планет в Солнечной системе — 8' },
    { q: 'меркурий', a: 'Ближе всего к Солнцу — Меркурий' },
    { q: 'тихий', a: 'Самый большой океан — Тихий (165 млн км²)' },
    { q: 'градус', a: 'Градусов в полном круге — 360' },
    { q: 'гепард', a: 'Самый быстрый зверь — Гепард (до 110 км/ч)' },
    { q: 'хромосом', a: 'Хромосом у человека — 46 (23 пары)' },
    { q: 'войну', a: '"Войну и мир" написал Лев Толстой (1863-1869)' },
    { q: 'мону лизу', a: '"Мону Лизу" нарисовал Леонардо да Винчи' },
    { q: 'ртуть', a: 'Жидкий металл — Ртуть (Hg)' },
    { q: 'эйнштейн', a: 'Теория относительности — Альберт Эйнштейн' },
    { q: 'эверест', a: 'Высочайшая гора — Эверест (8849 м)' },
    { q: 'байкал', a: 'Глубочайшее озеро — Байкал (1642 м)' },
    { q: 'нил', a: 'Длиннейшая река — Нил (6650 км)' },
    { q: 'толстой', a: 'Лев Толстой — автор "Войны и мира" и "Анны Карениной"' },
    { q: 'пифагор', a: 'Теорема Пифагора: c² = a² + b²' },
];

/* Smart responses for common topics */
const SMART_ANSWERS = {
    'квантов': 'Квантовая физика изучает поведение материи на микроскопическом уровне. Частицы могут быть одновременно в двух местах (суперпозиция) и мгновенно влиять друг на друга на расстоянии (запутанность).',
    'нейронн': 'Нейронная сеть — компьютерная модель, вдохновлённая мозгом. Состоит из слоёв нейронов, которые учатся распознавать паттерны в данных.',
    'машинн': 'Машинное обучение — раздел ИИ, где компьютеры учатся на данных без явного программирования. Виды: обучение с учителем, без учителя, с подкреплением.',
    'блокчейн': 'Блокчейн — цепочка блоков с данными. Каждый блок связан с предыдущим криптографически. Используется в криптовалютах, смарт-контрактах.',
    'эволюци': 'Эволюция — изменение видов во времени через естественный отбор. Организмы с полезными признаками выживают и передают гены потомству.',
    'гравитац': 'Гравитация — сила притяжения между массами. Открыта Ньютоном. Уточнена Эйнштейном: гравитация — искривление пространства-времени.',
    'фотосинтез': 'Фотосинтез — процесс превращения CO₂ и воды в глюкозу на свету. Формула: 6CO₂ + 6H₂O → C₆H₁₂O₆ + 6O₂.',
    'относительн': 'Теория относительности Эйнштейна: E = mc². Время и пространство относительны и зависят от скорости наблюдателя.',
};

const STOPWORDS = new Set([
    'что', 'такое', 'кто', 'это', 'как', 'где', 'когда', 'какой', 'какая',
    'какие', 'почему', 'зачем', 'сколько', 'the', 'a', 'an', 'is', 'are',
    'и', 'в', 'на', 'с', 'по', 'к', 'у', 'о', 'от', 'до', 'из', 'был',
]);

function tokenize(text) {
    return text.toLowerCase()
        .replace(/[^\p{L}\p{N}]/gu, ' ')
        .split(/\s+/)
        .filter(w => w.length >= 2 && !STOPWORDS.has(w));
}

function bm25Score(query, document) {
    const qTokens = tokenize(query);
    const dTokens = tokenize(document);
    if (qTokens.length === 0) return 0;
    let matches = 0;
    for (const qt of qTokens) {
        for (const dt of dTokens) {
            if (dt.includes(qt) || qt.includes(dt)) { matches++; break; }
        }
    }
    return matches / qTokens.length;
}

function findBestFact(query) {
    let bestScore = 0, bestFact = null;
    for (const fact of FACTS) {
        const score = bm25Score(query, fact.q) + bm25Score(query, fact.a) * 0.3;
        if (score > bestScore) { bestScore = score; bestFact = fact; }
    }
    return bestScore > 0.15 ? bestFact : null;
}

function findSmartAnswer(query) {
    const tokens = tokenize(query);
    for (const [key, answer] of Object.entries(SMART_ANSWERS)) {
        if (query.toLowerCase().includes(key)) return answer;
        for (const token of tokens) {
            if (token.includes(key) || key.includes(token)) return answer;
        }
    }
    return null;
}

function tryMathCalc(message) {
    /* N × M or N * M */
    let x = message.indexOf('×');
    if (x === -1) x = message.indexOf('*');
    if (x > 0) {
        const before = message.substring(0, x).match(/(\d+)/);
        const after = message.substring(x + 1).match(/(\d+)/);
        if (before && after) {
            const a = parseInt(before[1]), b = parseInt(after[1]);
            if (!isNaN(a) && !isNaN(b)) return `${a} × ${b} = ${a * b}`;
        }
    }
    /* N в степени M */
    const powMatch = message.match(/(\d+)\s*[встепени]+\s*(\d+)/);
    if (powMatch) {
        const a = parseInt(powMatch[1]), b = parseInt(powMatch[2]);
        if (!isNaN(a) && !isNaN(b) && b < 20) return `${a}^${b} = ${Math.pow(a, b)}`;
    }
    /* N + M, N - M */
    const addMatch = message.match(/(\d+)\s*[+\-]\s*(\d+)/);
    if (addMatch) {
        const a = parseInt(addMatch[1]), b = parseInt(addMatch[2]);
        const op = message.includes('+') ? '+' : '-';
        const result = op === '+' ? a + b : a - b;
        return `${a} ${op} ${b} = ${result}`;
    }
    return null;
}

/* HTTP Server */
const server = http.createServer((req, res) => {
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
    
    if (req.method === 'OPTIONS') { res.writeHead(200); res.end(); return; }
    
    if (req.method === 'GET' && req.url === '/api/v1/health') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ status: 'ok', facts: FACTS.length, version: '3.0' }));
        return;
    }
    
    if (req.method === 'POST' && (req.url === '/api/v1/ai/chat' || req.url === '/api/v1/ai/chat/stream')) {
        let body = '';
        req.on('data', chunk => body += chunk);
        req.on('end', () => {
            try {
                const json = JSON.parse(body);
                const message = json.message || '';
                const convId = json.conversation_id || 'default';
                
                let answer = null, method = 'unknown', confidence = 0;
                
                /* 1. Exact Q&A */
                const fact = findBestFact(message);
                if (fact) {
                    answer = fact.a; method = 'knowledge_base'; confidence = 0.95;
                }
                
                /* 2. Math */
                if (!answer) {
                    const mathResult = tryMathCalc(message);
                    if (mathResult) { answer = mathResult; method = 'math'; confidence = 1.0; }
                }
                
                /* 3. Smart answers */
                if (!answer) {
                    const smart = findSmartAnswer(message);
                    if (smart) { answer = smart; method = 'smart_knowledge'; confidence = 0.9; }
                }
                
                /* 4. Fallback */
                if (!answer) {
                    answer = `Не могу ответить на "${message}". Попробуйте спросить про столицы, математику, науку или историю.`;
                    method = 'fallback'; confidence = 0.3;
                }
                
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ response: answer, conversation_id: convId, method, confidence }));
            } catch (e) {
                res.writeHead(400, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ error: 'Invalid request' }));
            }
        });
        return;
    }
    
    res.writeHead(404, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ error: 'Not found' }));
});

server.listen(PORT, '0.0.0.0', () => {
    console.log(`🐦 Kolibri AI Proxy v3.0`);
    console.log(`  Port: ${PORT}`);
    console.log(`  Facts: ${FACTS.length} + smart answers: ${Object.keys(SMART_ANSWERS).length}`);
    console.log(`  🚀 Ready!\n`);
});
