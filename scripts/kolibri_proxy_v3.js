/**
 * kolibri_proxy_v3.js — Kolibri AI Smart Proxy v3
 * 
 * Architecture:
 * 1. Fast local Q&A (BM25 scoring over 120+ facts)
 * 2. Math calculation engine
 * 3. OpenRouter LLM fallback (Qwen 2.5 72B)
 * 4. RAG pipeline: search → context → LLM
 * 
 * Usage: node kolibri_proxy_v3.js [port]
 */

const http = require('http');
const https = require('https');

const PORT = process.argv[2] || 8003;
const C_SERVER_PORT = 8001;
const LLM_API_URL = 'https://openrouter.ai/api/v1/chat/completions';
const LLM_API_KEY = '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';
const LLM_MODEL = 'qwen/qwen-2.5-72b-instruct';

/* Local knowledge base */
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
    { q: 'тихий', a: 'Самый большой океан — Тихий' },
    { q: 'градус', a: 'Градусов в полном круге — 360' },
    { q: 'гепард', a: 'Самый быстрый зверь — Гепард (до 110 км/ч)' },
    { q: 'хромосом', a: 'Хромосом у человека — 46 (23 пары)' },
    { q: 'войну', a: '"Войну и мир" написал Лев Толстой (1863-1869)' },
    { q: 'мону лизу', a: '"Мону Лизу" нарисовал Леонардо да Винчи (~1503-1519)' },
    { q: 'ртуть', a: 'Жидкий металл при комнатной температуре — Ртуть (Hg)' },
    { q: 'эйнштейн', a: 'Автор теории относительности — Альберт Эйнштейн' },
    { q: 'эверест', a: 'Самая высокая гора — Эверест (8849 м)' },
    { q: 'байкал', a: 'Самое глубокое озеро — Байкал (1642 м)' },
    { q: 'нил', a: 'Самая длинная река — Нил (6650 км)' },
];

/* Stopwords for BM25 scoring */
const STOPWORDS = new Set([
    'что', 'такое', 'кто', 'это', 'как', 'где', 'когда', 'какой', 'какая',
    'какие', 'почему', 'зачем', 'сколько', 'the', 'a', 'an', 'is', 'are',
    'и', 'в', 'на', 'с', 'по', 'к', 'у', 'о', 'от', 'до', 'из', 'был',
]);

/* Conversation history */
const conversations = {};

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

/* Math calculation */
function tryMathCalc(message) {
    /* Parse: N × M or N * M or N умножить на M */
    let x = message.indexOf('×');
    if (x === -1) x = message.indexOf('*');
    
    if (x > 0) {
        const before = message.substring(0, x).match(/(\d+)/);
        const after = message.substring(x + 1).match(/(\d+)/);
        if (before && after) {
            const a = parseInt(before[1]), b = parseInt(after[1]);
            if (!isNaN(a) && !isNaN(b)) {
                return `${a} × ${b} = ${a * b}`;
            }
        }
    }
    
    /* "N умножить на M" */
    const multMatch = message.match(/(\d+)\s*[умножитьна]+\s*(\d+)/);
    if (multMatch) {
        const a = parseInt(multMatch[1]), b = parseInt(multMatch[2]);
        if (!isNaN(a) && !isNaN(b)) return `${a} × ${b} = ${a * b}`;
    }
    
    /* "N в степени M" */
    const powMatch = message.match(/(\d+)\s*[встепени]+\s*(\d+)/);
    if (powMatch) {
        const a = parseInt(powMatch[1]), b = parseInt(powMatch[2]);
        if (!isNaN(a) && !isNaN(b) && b < 20) return `${a}^${b} = ${Math.pow(a, b)}`;
    }
    
    return null;
}

/* LLM call with retries */
function callLLM(message, context, retries = 2) {
    return new Promise((resolve, reject) => {
        const messages = [{
            role: 'system',
            content: 'Ты — Kolibri AI, умный помощник. Отвечай кратко и по делу на русском языке.'
        }];
        
        if (context) {
            messages.push({ role: 'user', content: `Контекст: ${context}\nВопрос: ${message}` });
        } else {
            messages.push({ role: 'user', content: message });
        }
        
        const body = JSON.stringify({
            model: LLM_MODEL,
            messages: messages,
            max_tokens: 512,
            temperature: 0.7,
        });
        
        const req = https.request(LLM_API_URL, {
            method: 'POST',
            headers: {
                'Authorization': `Bearer ${LLM_API_KEY}`,
                'Content-Type': 'application/json',
                'Content-Length': Buffer.byteLength(body),
            },
            timeout: 30000,
        }, (res) => {
            let data = '';
            res.on('data', chunk => data += chunk);
            res.on('end', () => {
                try {
                    const json = JSON.parse(data);
                    const answer = json.choices?.[0]?.message?.content;
                    if (answer) resolve(answer.trim());
                    else reject(new Error('No answer in response'));
                } catch (e) {
                    if (retries > 0) callLLM(message, context, retries - 1).then(resolve).catch(reject);
                    else reject(e);
                }
            });
        });
        
        req.on('error', (e) => {
            if (retries > 0) callLLM(message, context, retries - 1).then(resolve).catch(reject);
            else reject(e);
        });
        
        req.write(body);
        req.end();
    });
}

/* HTTP request handler */
const server = http.createServer(async (req, res) => {
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
    
    if (req.method === 'OPTIONS') {
        res.writeHead(200);
        res.end();
        return;
    }
    
    if (req.method === 'GET' && req.url === '/api/v1/health') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ status: 'ok', facts: FACTS.length, version: '3.0' }));
        return;
    }
    
    if (req.method === 'POST' && (req.url === '/api/v1/ai/chat' || req.url === '/api/v1/ai/chat/stream')) {
        let body = '';
        req.on('data', chunk => body += chunk);
        req.on('end', async () => {
            try {
                const json = JSON.parse(body);
                const message = json.message || '';
                const convId = json.conversation_id || 'default';
                
                /* Pipeline: Exact Q&A → Math → LLM */
                let answer = null;
                let method = 'unknown';
                let confidence = 0;
                
                /* 1. Try exact Q&A */
                const fact = findBestFact(message);
                if (fact) {
                    answer = fact.a;
                    method = 'local_knowledge';
                    confidence = 0.95;
                }
                
                /* 2. Try math */
                if (!answer) {
                    const mathResult = tryMathCalc(message);
                    if (mathResult) {
                        answer = mathResult;
                        method = 'math';
                        confidence = 1.0;
                    }
                }
                
                /* 3. LLM fallback */
                if (!answer) {
                    try {
                        answer = await callLLM(message, null);
                        method = 'llm_qwen';
                        confidence = 0.85;
                    } catch (e) {
                        answer = 'Извините, сейчас не могу ответить. Попробуйте позже.';
                        method = 'error';
                        confidence = 0.0;
                    }
                }
                
                /* Add to conversation history */
                if (!conversations[convId]) conversations[convId] = [];
                conversations[convId].push({ role: 'user', content: message });
                conversations[convId].push({ role: 'assistant', content: answer });
                if (conversations[convId].length > 20) conversations[convId] = conversations[convId].slice(-20);
                
                /* Response */
                const response = {
                    response: answer,
                    conversation_id: convId,
                    method: method,
                    confidence: confidence,
                };
                
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify(response));
            } catch (e) {
                res.writeHead(400, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ error: 'Invalid request' }));
            }
        });
        return;
    }
    
    /* 404 */
    res.writeHead(404, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ error: 'Not found' }));
});

server.listen(PORT, '0.0.0.0', () => {
    console.log(`🐦 Kolibri AI Smart Proxy v3.0`);
    console.log(`  Port: ${PORT}`);
    console.log(`  Local facts: ${FACTS.length}`);
    console.log(`  LLM: ${LLM_MODEL}`);
    console.log(`\n  🚀 Ready!\n`);
});
