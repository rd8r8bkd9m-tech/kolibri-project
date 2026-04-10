/**
 * kolibri_proxy_v4.js — Kolibri AI Smart Proxy v4
 * 
 * Features:
 * - 120 facts loaded from knowledge_base_qa.md
 * - BM25-like scoring for semantic search
 * - Conversation history (last 20 messages)
 * - Math calculation engine
 * - Smart fallback responses
 * 
 * Usage: node kolibri_proxy_v4.js [port]
 */

const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = process.argv[2] || 8003;
const C_SERVER = 'http://localhost:8001';

/* ===== LOAD KNOWLEDGE BASE ===== */
let FACTS = [];
try {
    FACTS = JSON.parse(fs.readFileSync(path.join(__dirname, 'knowledge/facts.json'), 'utf-8'));
    console.log(`📚 Loaded ${FACTS.length} facts from knowledge_base_qa.md`);
} catch (e) {
    console.log('⚠️  Could not load facts.json, using defaults');
    FACTS = [];
}

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
    'днк': 'ДНК (дезоксирибонуклеиновая кислота) — молекула, хранящая генетическую информацию. Состоит из двух цепочек нуклеотидов, закрученных в двойную спираль.',
    'мозг': 'Мозг — центральный орган нервной системы. Содержит ~86 миллиардов нейронов. Управляет мышлением, движением, дыханием, памятью.',
    'интернет': 'Интернет — глобальная компьютерная сеть. Начался с ARPANET в 1960-х. WWW создан Тимом Бернерсом-Ли в 1991.',
    'солнечн': 'Солнечная система: Солнце + 8 планет (Меркурий, Венера, Земля, Марс, Юпитер, Сатурн, Уран, Нептун). Возраст ~4.6 млрд лет.',
};

const STOPWORDS = new Set([
    'что', 'такое', 'кто', 'это', 'как', 'где', 'когда', 'какой', 'какая',
    'какие', 'почему', 'зачем', 'сколько', 'the', 'a', 'an', 'is', 'are',
    'и', 'в', 'на', 'с', 'по', 'к', 'у', 'о', 'от', 'до', 'из', 'был',
]);

function tokenize(text) {
    return text.toLowerCase()
        .replace(/[^\w\s]/g, ' ')
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
    const qLower = query.toLowerCase();
    
    /* First pass: exact substring match (highest priority) */
    for (const fact of FACTS) {
        const fLower = fact.q.toLowerCase();
        const aLower = fact.a.toLowerCase();
        if (fLower.includes(qLower) || qLower.includes(fLower) || aLower.includes(qLower)) {
            return fact;  /* Exact match found */
        }
    }
    
    /* Second pass: BM25 keyword matching */
    const qTokens = tokenize(query);
    for (const fact of FACTS) {
        let score = 0;
        for (const qt of qTokens) {
            if (fact.q.toLowerCase().includes(qt)) score += 2;
            if (fact.a.toLowerCase().includes(qt)) score += 0.5;
            for (const kw of fact.keywords) {
                if (kw.includes(qt) || qt.includes(kw)) score += 1;
            }
        }
        if (score > bestScore) { bestScore = score; bestFact = fact; }
    }
    return bestScore >= 1 ? bestFact : null;
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

/* Conversation history */
const conversations = {};

/* HTTP Server */
const server = http.createServer((req, res) => {
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
    
    if (req.method === 'OPTIONS') { res.writeHead(200); res.end(); return; }
    
    if (req.method === 'GET' && req.url === '/api/v1/health') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ status: 'ok', facts: FACTS.length, version: '4.0' }));
        return;
    }
    
    if (req.method === 'POST' && req.url === '/api/v1/ai/chat') {
        let body = '';
        req.on('data', chunk => body += chunk);
        req.on('end', () => {
            try {
                const json = JSON.parse(body);
                const message = json.message || '';
                const convId = json.conversation_id || 'default';
                
                let answer = null, method = 'unknown', confidence = 0;
                
                /* 1. Exact Q&A from 120 facts */
                const fact = findBestFact(message);
                if (fact) { answer = fact.a; method = 'knowledge_base'; confidence = 0.95; }
                
                /* 2. Smart answers */
                if (!answer) {
                    const smart = findSmartAnswer(message);
                    if (smart) { answer = smart; method = 'smart_knowledge'; confidence = 0.9; }
                }
                
                /* 3. Math calculation */
                if (!answer) {
                    const mathResult = tryMathCalc(message);
                    if (mathResult) { answer = mathResult; method = 'math'; confidence = 1.0; }
                }
                
                /* 4. Fallback */
                if (!answer) {
                    answer = `Не могу ответить на "${message}". Попробуйте спросить про столицы, науку, историю или математику.`;
                    method = 'fallback'; confidence = 0.3;
                }
                
                /* Save to conversation history */
                if (!conversations[convId]) conversations[convId] = [];
                conversations[convId].push({ role: 'user', content: message });
                conversations[convId].push({ role: 'assistant', content: answer });
                if (conversations[convId].length > 20) conversations[convId] = conversations[convId].slice(-20);
                
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
    console.log(`🐦 Kolibri AI Proxy v4.0`);
    console.log(`  Port: ${PORT}`);
    console.log(`  Facts: ${FACTS.length}`);
    console.log(`  Smart answers: ${Object.keys(SMART_ANSWERS).length}`);
    console.log(`  🚀 Ready!\n`);
});
