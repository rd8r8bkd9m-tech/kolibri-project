/**
 * kolibri_proxy.js — Kolibri AI Smart Proxy
 * 
 * Combines:
 * - Fast local C server for exact Q&A (port 8001)
 * - OpenRouter LLM fallback (Qwen) for unknown questions
 * - RAG pipeline: search local facts → add context → LLM
 * 
 * Usage: node kolibri_proxy.js [port]
 * Default port: 8003
 */

const http = require('http');
const https = require('https');

const PORT = process.argv[2] || 8003;
const C_SERVER_PORT = 8001;
const OPENROUTER_URL = 'https://openrouter.ai/api/v1/chat/completions';
const OPENROUTER_KEY = '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';
const LLM_MODEL = 'qwen/qwen-2.5-72b-instruct';

/* Local facts for RAG */
const FACTS = [
    { q: 'Столица Франции', a: 'Столица Франции — Париж' },
    { q: 'Столица Японии', a: 'Столица Японии — Токио' },
    { q: 'Столица Германии', a: 'Столица Германии — Берлин' },
    { q: 'Столица Канады', a: 'Столица Канады — Оттава' },
    { q: 'Столица Австралии', a: 'Столица Австралии — Канберра' },
    { q: 'Столица Великобритании', a: 'Столица Великобритании — Лондон' },
    { q: 'Столица России', a: 'Столица России — Москва' },
    { q: 'Столица Италии', a: 'Столица Италии — Рим' },
    { q: 'Столица Испании', a: 'Столица Испании — Мадрид' },
    { q: 'Столица Китая', a: 'Столица Китая — Пекин' },
    { q: 'Столица США', a: 'Столица США — Вашингтон' },
    { q: 'Спутник Земли', a: 'Спутник Земли — Луна' },
    { q: 'Планет в Солнечной', a: 'Планет в Солнечной системе — 8' },
    { q: 'Меркурий', a: 'Ближе всего к Солнцу — Меркурий' },
    { q: 'Тихий', a: 'Самый большой океан — Тихий' },
    { q: 'Градус', a: 'Градусов в полном круге — 360' },
    { q: 'Гепард', a: 'Самый быстрый зверь — Гепард (до 110 км/ч)' },
    { q: 'Хромосом', a: 'Хромосом у человека — 46 (23 пары)' },
    { q: 'Войну и мир', a: '"Войну и мир" написал Лев Толстой (1863-1869)' },
    { q: 'Мону Лизу', a: '"Мону Лизу" нарисовал Леонардо да Винчи (~1503-1519)' },
    { q: 'Ртуть', a: 'Жидкий металл при комнатной температуре — Ртуть (Hg)' },
    { q: 'Эйнштейн', a: 'Автор теории относительности — Альберт Эйнштейн' },
    { q: 'Эверест', a: 'Самая высокая гора — Эверест (8849 м)' },
    { q: 'Байкал', a: 'Самое глубокое озеро — Байкал (1642 м)' },
    { q: 'Нил', a: 'Самая длинная река — Нил (6650 км)' },
    { q: '7.*8', a: '7 × 8 = 56' },
];

/* Conversation history */
const conversations = {};

/* BM25-like scoring */
const STOPWORDS = new Set([
    'что', 'такое', 'кто', 'это', 'как', 'где', 'когда', 'какой', 'какая',
    'какие', 'почему', 'зачем', 'сколько', 'the', 'a', 'an', 'is', 'are',
    'и', 'в', 'на', 'с', 'по', 'к', 'у', 'о', 'от', 'до', 'из',
]);

function tokenize(text) {
    /* Keep Cyrillic and Latin letters + digits */
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
            if (dt.includes(qt) || qt.includes(dt)) {
                matches++;
                break;
            }
        }
    }
    return matches / qTokens.length;
}

function findBestFact(query) {
    let bestScore = 0;
    let bestFact = null;
    
    for (const fact of FACTS) {
        const score = bm25Score(query, fact.q) + bm25Score(query, fact.a) * 0.3;
        if (score > bestScore) {
            bestScore = score;
            bestFact = fact;
        }
    }
    
    return bestScore > 0.15 ? bestFact : null;
}

/* Call OpenRouter API */
function callLLM(context, question, callback) {
    const messages = [
        {
            role: 'system',
            content: 'Ты — Kolibri AI, умный помощник с глубокими знаниями. Отвечай на русском языке. Если не знаешь точный ответ, скажи честно. Используй контекст если он предоставлен.'
        }
    ];
    
    if (context) {
        messages.push({ role: 'user', content: `Контекст: ${context}\n\nВопрос: ${question}` });
    } else {
        messages.push({ role: 'user', content: question });
    }
    
    const body = JSON.stringify({
        model: LLM_MODEL,
        messages: messages,
        max_tokens: 1024,
        temperature: 0.7,
    });
    
    const options = {
        hostname: 'openrouter.ai',
        path: '/api/v1/chat/completions',
        method: 'POST',
        headers: {
            'Authorization': `Bearer ${OPENROUTER_KEY}`,
            'Content-Type': 'application/json',
            'Content-Length': Buffer.byteLength(body),
        }
    };
    
    const req = https.request(options, (res) => {
        let data = '';
        res.on('data', chunk => data += chunk);
        res.on('end', () => {
            try {
                const json = JSON.parse(data);
                const answer = json.choices?.[0]?.message?.content || 'Извините, не могу ответить.';
                callback(null, answer);
            } catch (e) {
                callback(e);
            }
        });
    });
    
    req.on('error', callback);
    req.write(body);
    req.end();
}

/* Proxy to C server for local Q&A */
function proxyToCServer(message, callback) {
    const body = JSON.stringify({ message, conversation_id: 'proxy' });
    const options = {
        hostname: 'localhost',
        port: C_SERVER_PORT,
        path: '/api/v1/ai/chat',
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
            'Content-Length': Buffer.byteLength(body),
        }
    };
    
    const req = http.request(options, (res) => {
        let data = '';
        res.on('data', chunk => data += chunk);
        res.on('end', () => {
            try {
                const json = JSON.parse(data);
                callback(null, json.response || '');
            } catch (e) {
                callback(e);
            }
        });
    });
    
    req.on('error', callback);
    req.write(body);
    req.end();
}

/* Main request handler */
function handleRequest(req, res) {
    if (req.method === 'OPTIONS') {
        res.writeHead(200, {
            'Access-Control-Allow-Origin': '*',
            'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
            'Access-Control-Allow-Headers': 'Content-Type',
        });
        res.end();
        return;
    }
    
    if (req.method === 'GET' && req.url === '/api/v1/health') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({
            status: 'ok',
            facts: FACTS.length,
            conversations: Object.keys(conversations).length,
            version: '2.0',
        }));
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
                const stream = req.url.includes('stream');
                
                /* RAG Pipeline:
                 1. Try exact Q&A from local facts
                 2. If not found, call LLM with RAG context
                */
                const localFact = findBestFact(message);
                
                if (localFact && bm25Score(message, localFact.q) > 0.5) {
                    /* Exact match found */
                    const response = {
                        response: localFact.a,
                        conversation_id: convId,
                        method: 'local_knowledge',
                        confidence: 0.95,
                    };
                    res.writeHead(200, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify(response));
                } else {
                    /* LLM fallback with RAG context */
                    const context = localFact ? localFact.a : '';
                    
                    callLLM(context, message, (err, llmAnswer) => {
                        if (err) {
                            res.writeHead(502, { 'Content-Type': 'application/json' });
                            res.end(JSON.stringify({ error: 'LLM unavailable' }));
                            return;
                        }
                        
                        const response = {
                            response: llmAnswer,
                            conversation_id: convId,
                            method: 'llm_qwen',
                            confidence: 0.8,
                        };
                        res.writeHead(200, { 'Content-Type': 'application/json' });
                        res.end(JSON.stringify(response));
                    });
                }
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
}

/* Start server */
const server = http.createServer(handleRequest);
server.listen(PORT, '0.0.0.0', () => {
    console.log(`🐦 Kolibri AI Smart Proxy v2.0`);
    console.log(`  Port: ${PORT}`);
    console.log(`  C Server: localhost:${C_SERVER_PORT}`);
    console.log(`  Local facts: ${FACTS.length}`);
    console.log(`  LLM: ${LLM_MODEL}`);
    console.log(`\n  🚀 Ready!\n`);
});
