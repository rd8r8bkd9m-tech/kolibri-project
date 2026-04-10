/**
 * kolibri_proxy_v5.js — Kolibri AI Smart Proxy v5
 * 
 * Features:
 * - 120 facts from knowledge_base_qa.md
 * - Stem-aware fuzzy matching (градус matches градусов)
 * - Conversation history
 * - Math engine
 * - Smart answers for science topics
 */

const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = process.argv[2] || 8003;

/* ===== LOAD KNOWLEDGE BASE ===== */
let FACTS = [];
try {
    FACTS = JSON.parse(fs.readFileSync(path.join(__dirname, 'knowledge/facts.json'), 'utf-8'));
} catch (e) {
    FACTS = [];
}

/* Smart answers */
const SMART = {
    'квантов': 'Квантовая физика изучает поведение материи на микроскопическом уровне. Суперпозиция — частица в двух местах одновременно. Запутанность — мгновенное влияние на расстоянии.',
    'нейронн': 'Нейросеть — модель из слоёв нейронов, вдохновлённая мозгом. Учится распознавать паттерны в данных через backpropagation.',
    'машинн': 'Машинное обучение — ИИ учится на данных. Виды: с учителем (классификация), без учителя (кластеризация), с подкреплением (обучение через награду).',
    'блокчейн': 'Блокчейн — криптографическая цепочка блоков. Каждый блок содержит хеш предыдущего. Используется в Bitcoin, Ethereum, смарт-контрактах.',
    'эволюци': 'Эволюция — изменение видов через естественный отбор. Дарвин: выживают приспособленные. Мутации + отбор = новые виды.',
    'гравитац': 'Гравитация — притяжение масс. Ньютон: F = G·m₁·m₂/r². Эйнштейн: гравитация = искривление пространства-времени.',
    'фотосинтез': 'Фотосинтез: 6CO₂ + 6H₂O → C₆H₁₂O₆ + 6O₂. Растения превращают свет в глюкозу. Происходит в хлоропластах.',
    'днк': 'ДНК — двойная спираль из нуклеотидов. Хранит генетическую информацию. 46 хромосом у человека.',
    'мозг': 'Мозг — ~86 млрд нейронов. Управляет мышлением, памятью, движением. Кора отвечает за сознание.',
};

const STOPWORDS = new Set(['что','такое','кто','это','как','где','когда','какой','какая','какие','почему','зачем','сколько','the','a','an','is','are','и','в','на','с','по','к','у','о','от','до','из','был','самый','самая','самое']);

/* Stemmer: simple Russian stemmer */
function stem(word) {
    const suffixes = ['ов','ев','ев','ам','ями','ах','ов','ок','ек','ик','ец','ец','ость','ение','ание','тие','ние','ние','ный','ной','ная','ное','ные','ого','его','ому','ему','ым','ым','ими','ими','ых','ых'];
    let stemmed = word.toLowerCase();
    for (const s of suffixes) {
        if (stemmed.endsWith(s) && stemmed.length > s.length + 2) {
            return stemmed.slice(0, -s.length);
        }
    }
    return stemmed;
}

function tokenize(text) {
    return text.toLowerCase()
        .replace(/[^\w\s]/g, ' ')
        .split(/\s+/)
        .filter(w => w.length >= 2 && !STOPWORDS.has(w));
}

function findBestFact(query) {
    const qTokens = tokenize(query);
    const qStems = qTokens.map(stem);
    let bestScore = 0, bestFact = null;
    
    for (const fact of FACTS) {
        let score = 0;
        const fLower = fact.q.toLowerCase() + ' ' + fact.a.toLowerCase();
        const fTokens = tokenize(fact.q + ' ' + fact.a);
        const fStems = fTokens.map(stem);
        
        /* Exact match bonus */
        if (fLower.includes(query.toLowerCase())) score += 10;
        
        /* Stem matching */
        for (const qs of qStems) {
            for (const fs of fStems) {
                if (fs === qs || fs.includes(qs) || qs.includes(fs)) { score += 3; break; }
            }
        }
        
        /* Token matching */
        for (const qt of qTokens) {
            if (fLower.includes(qt)) score += 1;
        }
        
        if (score > bestScore) { bestScore = score; bestFact = fact; }
    }
    return bestScore >= 3 ? bestFact : null;
}

function findSmart(query) {
    const qLower = query.toLowerCase();
    for (const [key, answer] of Object.entries(SMART)) {
        if (qLower.includes(key)) return answer;
        for (const token of tokenize(query)) {
            if (token.includes(key) || key.includes(token)) return answer;
        }
    }
    return null;
}

function tryMath(message) {
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
    const powMatch = message.match(/(\d+)\s*[встепени]+\s*(\d+)/);
    if (powMatch) {
        const a = parseInt(powMatch[1]), b = parseInt(powMatch[2]);
        if (!isNaN(a) && !isNaN(b) && b < 20) return `${a}^${b} = ${Math.pow(a, b)}`;
    }
    const addMatch = message.match(/(\d+)\s*[+\-]\s*(\d+)/);
    if (addMatch) {
        const a = parseInt(addMatch[1]), b = parseInt(addMatch[2]);
        return message.includes('+') ? `${a} + ${b} = ${a + b}` : `${a} - ${b} = ${a - b}`;
    }
    return null;
}

const conversations = {};

const server = http.createServer((req, res) => {
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
    if (req.method === 'OPTIONS') { res.writeHead(200); res.end(); return; }
    
    if (req.method === 'GET' && req.url === '/api/v1/health') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ status: 'ok', facts: FACTS.length, version: '5.0' }));
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
                
                /* 1. Exact Q&A with stem matching */
                const fact = findBestFact(message);
                if (fact) { answer = fact.a; method = 'knowledge_base'; confidence = 0.95; }
                
                /* 2. Smart answers */
                if (!answer) {
                    const smart = findSmart(message);
                    if (smart) { answer = smart; method = 'smart_knowledge'; confidence = 0.9; }
                }
                
                /* 3. Math */
                if (!answer) {
                    const mathResult = tryMath(message);
                    if (mathResult) { answer = mathResult; method = 'math'; confidence = 1.0; }
                }
                
                /* 4. Fallback */
                if (!answer) {
                    answer = `Не могу ответить на "${message}". Попробуйте: столицы, науку, историю, математику.`;
                    method = 'fallback'; confidence = 0.3;
                }
                
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
    console.log(`🐦 Kolibri AI Proxy v5.0`);
    console.log(`  Port: ${PORT}, Facts: ${FACTS.length}, Smart: ${Object.keys(SMART).length}`);
    console.log(`  🚀 Ready!\n`);
});
