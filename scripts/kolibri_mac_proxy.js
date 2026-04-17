/**
 * Kolibri Mac Hybrid Proxy — Node.js
 * Объединяет ВСЕ источники в единую сеть:
 *   1. kolibri_swarm (localhost:8002) — 614+ фактов, swarm сеть
 *   2. Node 1 swarm (217.60.249.157:8001) — 30K фактов
 *   3. kolibriai.ru — LLM через SSE streaming
 *   4. kolibri_http (localhost:8001) — fallback
 *
 * Порт: 8003
 */

const http = require('http');
const https = require('https');
const { URL } = require('url');

const PORT = 8003;

// ─── Источники знаний ( reordered for speed: fast local first ) ───
const SOURCES = [
    { name: 'kolibri_http',  url: 'http://localhost:8001',           timeout: 2000 },  // C server — fastest, local
    { name: 'swarm_mac',     url: 'http://localhost:8002',           timeout: 2000 },  // local swarm
    { name: 'swarm_node1',   url: 'http://217.60.249.157:8001',      timeout: 5000 },  // remote node (network latency)
    { name: 'kolibriai_ru',  url: 'https://kolibriai.ru',            timeout: 60000 }, // LLM fallback
];

// ─── Response cache (LRU-style) ───
const responseCache = new Map();  // question → { answer, source, confidence, method, timestamp, hits }
const CACHE_MAX_SIZE = 500;
const CACHE_TTL_MS = 300000;  // 5 minutes

// ─── Known facts cache for learning ───
const learnedFacts = new Map();  // question → answer
const PEER_PUSH_INTERVAL = 120000;  // Push to peers every 2 min

/**
 * Get cached response if available and not expired
 */
function getCachedAnswer(question) {
    const key = question.trim().toLowerCase();
    const cached = responseCache.get(key);
    if (!cached) return null;
    if (Date.now() - cached.timestamp > CACHE_TTL_MS) {
        responseCache.delete(key);
        return null;
    }
    cached.hits++;
    return cached;
}

/**
 * Store response in cache with LRU eviction
 */
function setCachedAnswer(question, result) {
    const key = question.trim().toLowerCase();
    if (responseCache.size >= CACHE_MAX_SIZE) {
        // Remove oldest entry
        let oldestKey = null;
        let oldestTime = Infinity;
        for (const [k, v] of responseCache) {
            if (v.timestamp < oldestTime) {
                oldestTime = v.timestamp;
                oldestKey = k;
            }
        }
        if (oldestKey) responseCache.delete(oldestKey);
    }
    responseCache.set(key, {
        ...result,
        timestamp: Date.now(),
        hits: 0,
    });
}

/**
 * Попробовать kolibri_swarm endpoint
 * Возвращает { answer, source, confidence, ... } или null
 */
async function trySwarm(source, body) {
    return new Promise((resolve) => {
        const url = new URL('/api/v1/ai/chat', source.url);
        const data = JSON.stringify(body);
        const isHttps = source.url.startsWith('https');
        const lib = isHttps ? https : http;

        const req = lib.request(url, {
            method: 'POST',
            timeout: source.timeout,
            headers: {
                'Content-Type': 'application/json',
                'Content-Length': Buffer.byteLength(data),
            },
        }, (res) => {
            let raw = '';
            res.on('data', chunk => raw += chunk);
            res.on('end', () => {
                try {
                    const json = JSON.parse(raw);
                    const ans = json.response || '';
                    // Require meaningful answer (not empty, not fallback, at least 15 chars)
                    if (ans && ans.length > 15 && 
                        !ans.includes('Нет точного ответа') && 
                        !ans.includes('Попробуйте вопрос') &&
                        !ans.includes('Доступно')) {
                        resolve({ ...json, source: source.name });
                        return;
                    }
                } catch(e) {}
                resolve(null);
            });
        });
        req.on('error', () => resolve(null));
        req.on('timeout', () => { req.destroy(); resolve(null); });
        req.write(data);
        req.end();
    });
}

/**
 * Проверить health всех источников
 */
async function checkHealth() {
    const results = {};
    const promises = SOURCES.map(async (src) => {
        return new Promise((resolve) => {
            const url = new URL('/api/v1/health', src.url);
            const isHttps = src.url.startsWith('https');
            const lib = isHttps ? https : http;
            
            const req = lib.get(url, { timeout: 3000 }, (res) => {
                let raw = '';
                res.on('data', chunk => raw += chunk);
                res.on('end', () => {
                    try {
                        const json = JSON.parse(raw);
                        resolve({
                            ok: res.statusCode === 200,
                            facts: json.facts || json.status || '',
                            peers: json.peers || 0,
                        });
                    } catch(e) {
                        resolve({ ok: res.statusCode === 200 });
                    }
                });
            });
            req.on('error', () => resolve({ ok: false }));
            req.on('timeout', () => { req.destroy(); resolve({ ok: false }); });
            req.end();
        });
    });
    
    const healthResults = await Promise.all(promises);
    SOURCES.forEach((src, i) => {
        results[src.name] = healthResults[i];
    });
    return results;
}

/**
 * SSE streaming proxy от kolibriai.ru
 */
function proxySSE(body, res) {
    const url = new URL('/api/v1/ai/chat/stream', SOURCES[3].url);
    const data = JSON.stringify(body);
    
    res.writeHead(200, {
        'Content-Type': 'text/event-stream',
        'Cache-Control': 'no-cache',
        'Connection': 'keep-alive',
        'X-Accel-Buffering': 'no',
        'X-Source': 'kolibriai_ru',
    });
    
    const req = https.request(url, {
        method: 'POST',
        timeout: 60000,
        headers: {
            'Content-Type': 'application/json',
            'Content-Length': Buffer.byteLength(data),
        },
    }, (proxyRes) => {
        proxyRes.on('data', chunk => res.write(chunk));
        proxyRes.on('end', () => res.end());
    });
    req.on('error', (e) => {
        res.write(`event: error\ndata: {"error":"${e.message}"}\n\n`);
        res.end();
    });
    req.on('timeout', () => {
        res.write('event: error\ndata: {"error":"timeout"}\n\n');
        res.end();
        req.destroy();
    });
    req.write(data);
    req.end();
}

/**
 * JSON response helper
 */
function sendJSON(res, status, data) {
    res.writeHead(status, {
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*',
    });
    res.end(JSON.stringify(data));
}

/**
 * Generic proxy to a specific source
 * Forwards the request body and pipes the response back
 */
function proxyToSource(path, req, res, source) {
    const url = new URL(path, source.url);
    if (url.search) url.search = url.search;
    const isHttps = source.url.startsWith('https');
    const lib = isHttps ? https : http;

    let body = '';
    req.on('data', chunk => body += chunk);
    req.on('end', () => {
        const proxyReq = lib.request(url, {
            method: req.method,
            timeout: source.timeout,
            headers: {
                'Content-Type': 'application/json',
                'Content-Length': Buffer.byteLength(body),
            },
        }, (proxyRes) => {
            res.writeHead(proxyRes.statusCode, proxyRes.headers);
            proxyRes.pipe(res);
        });
        proxyReq.on('error', () => sendJSON(res, 502, { error: `Proxy error to ${source.name}` }));
        proxyReq.on('timeout', () => { proxyReq.destroy(); sendJSON(res, 504, { error: 'Timeout' }); });
        if (body) proxyReq.write(body);
        proxyReq.end();
    });
}

// ─── HTTP Server ───
const server = http.createServer(async (req, res) => {
    // CORS preflight
    if (req.method === 'OPTIONS') {
        res.writeHead(200, {
            'Access-Control-Allow-Origin': '*',
            'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
            'Access-Control-Allow-Headers': 'Content-Type, Authorization',
        });
        res.end();
        return;
    }
    
    const url = new URL(req.url, `http://localhost:${PORT}`);
    const path = url.pathname;
    
    // ─── Health ───
    if (path === '/api/v1/health' && req.method === 'GET') {
        const health = await checkHealth();
        const totalFacts = Object.values(health)
            .filter(h => h.ok && typeof h.facts === 'number')
            .reduce((sum, h) => sum + h.facts, 0);
        
        sendJSON(res, 200, {
            status: 'kolibri_mac_hybrid',
            sources: health,
            total_facts: totalFacts,
        });
        return;
    }
    
    // ─── Swarm Status ───
    if (path === '/api/v1/swarm/status' && req.method === 'GET') {
        const health = await checkHealth();
        sendJSON(res, 200, { sources: health });
        return;
    }
    
    // ─── Chat: /api/v1/ai/chat (POST) ───
    if ((path === '/api/v1/ai/chat' || path === '/api/v1/ai/chat/') && req.method === 'POST') {
        let body = '';
        req.on('data', chunk => body += chunk);
        req.on('end', async () => {
            try {
                const parsed = JSON.parse(body);
                const question = parsed.message || '';

                // Check cache first
                const cached = getCachedAnswer(question);
                if (cached) {
                    sendJSON(res, 200, { ...cached, cached: true });
                    return;
                }

                // Try sources sequentially (fast local first)
                let result = null;
                for (const source of SOURCES.slice(0, 3)) {  // Skip LLM for non-stream
                    result = await trySwarm(source, parsed);
                    if (result) break;
                }

                if (result) {
                    setCachedAnswer(question, result);
                    sendJSON(res, 200, result);
                    return;
                }

                // LLM as last resort
                result = await trySwarm(SOURCES[3], parsed);
                if (result) {
                    setCachedAnswer(question, result);
                    sendJSON(res, 200, result);
                    return;
                }

                sendJSON(res, 503, { response: 'Нет доступных источников.', source: 'none' });
            } catch(e) {
                sendJSON(res, 400, { error: e.message });
            }
        });
        return;
    }
    
    // ─── Live Queue API: proxy to kolibri_http (C server) ───
    if (path.startsWith('/api/v1/live-queue') && (req.method === 'GET' || req.method === 'POST')) {
        proxyToSource(path, req, res, SOURCES[0]); // kolibri_http
        return;
    }

    // ─── Learning API: proxy to kolibri_http (C server) ───
    if (path.startsWith('/api/v1/learning') && (req.method === 'GET' || req.method === 'POST')) {
        proxyToSource(path, req, res, SOURCES[0]); // kolibri_http
        return;
    }

    // ─── C-Core API: proxy to kolibri_http (C server) ───
    // Covers: /api/v1/ai/*, /api/v1/live-queue/*, /api/v1/learning/*,
    //         /api/v1/autolearn/*, /api/v1/world_model/*, /api/v1/corpus/*,
    //         /api/v1/formula/*, /api/v1/fractal/*, /api/v1/system/*
    if (path.startsWith('/api/v1/ai/') || path.startsWith('/api/v1/live-queue') ||
        path.startsWith('/api/v1/learning') || path.startsWith('/api/v1/autolearn') ||
        path.startsWith('/api/v1/world_model') || path.startsWith('/api/v1/corpus') ||
        path.startsWith('/api/v1/formula') || path.startsWith('/api/v1/fractal') ||
        path.startsWith('/api/v1/system')) {
        proxyToSource(path, req, res, SOURCES[0]); // kolibri_http
        return;
    }

    // ─── Chat Stream: /api/v1/ai/chat/stream (POST) ───
    if ((path === '/api/v1/ai/chat/stream' || path === '/api/v1/ai/chat/stream/') && req.method === 'POST') {
        let body = '';
        req.on('data', chunk => body += chunk);
        req.on('end', async () => {
            try {
                const parsed = JSON.parse(body);
                const question = parsed.message || '';

                // Check cache first — send as single SSE event
                const cached = getCachedAnswer(question);
                if (cached) {
                    res.writeHead(200, {
                        'Content-Type': 'text/event-stream',
                        'Cache-Control': 'no-cache',
                        'Connection': 'keep-alive',
                        'X-Source': cached.source,
                        'X-Cached': 'true',
                    });
                    res.write(`event: message\ndata: ${JSON.stringify({
                        token: cached.response,
                        done: true,
                        source: cached.source,
                        confidence: cached.confidence,
                        cached: true,
                    })}\n\n`);
                    res.write(`event: done\ndata: ${JSON.stringify({
                        conversation_id: parsed.conversation_id || '',
                        source: cached.source,
                    })}\n\n`);
                    res.end();
                    return;
                }

                // Try local sources first (fast)
                let result = null;
                for (const source of SOURCES.slice(0, 3)) {
                    result = await trySwarm(source, parsed);
                    if (result) break;
                }

                if (result) {
                    setCachedAnswer(question, result);
                    res.writeHead(200, {
                        'Content-Type': 'text/event-stream',
                        'Cache-Control': 'no-cache',
                        'Connection': 'keep-alive',
                        'X-Source': result.source,
                    });
                    res.write(`event: message\ndata: ${JSON.stringify({
                        token: result.response,
                        done: true,
                        source: result.source,
                        confidence: result.confidence,
                    })}\n\n`);
                    res.write(`event: done\ndata: ${JSON.stringify({
                        conversation_id: parsed.conversation_id || '',
                        source: result.source,
                    })}\n\n`);
                    res.end();
                    return;
                }

                // kolibriai.ru SSE streaming — проксируем напрямую
                proxySSE(parsed, res);
            } catch(e) {
                sendJSON(res, 400, { error: e.message });
            }
        });
        return;
    }
    
    // ─── Passthrough остальных запросов к kolibriai.ru ───
    {
        let body = '';
        req.on('data', chunk => body += chunk);
        req.on('end', () => {
            const targetUrl = new URL(path, SOURCES[3].url);
            if (url.search) targetUrl.search = url.search;
            
            const isHttps = SOURCES[3].url.startsWith('https');
            const lib = isHttps ? https : http;
            
            const proxyReq = lib.request(targetUrl, {
                method: req.method,
                timeout: 30000,
                headers: {
                    ...req.headers,
                    host: new URL(SOURCES[3].url).host,
                },
            }, (proxyRes) => {
                res.writeHead(proxyRes.statusCode, proxyRes.headers);
                proxyRes.pipe(res);
            });
            proxyReq.on('error', () => sendJSON(res, 502, { error: 'Proxy error' }));
            proxyReq.on('timeout', () => { proxyReq.destroy(); sendJSON(res, 504, { error: 'Timeout' }); });
            if (body) proxyReq.write(body);
            proxyReq.end();
        });
        return;
    }
});

server.listen(PORT, '0.0.0.0', () => {
    console.log('🐦 Kolibri Mac Hybrid Proxy — все источники в одной сети');
    console.log(`  Listening on http://0.0.0.0:${PORT}`);
    console.log('');
    console.log('  Источники:');
    SOURCES.forEach(s => console.log(`    • ${s.name}: ${s.url}`));
    console.log('');
    console.log('  Порядок маршрутизации:');
    console.log('    1. kolibri_http (localhost:8001) — C server, fastest local');
    console.log('    2. kolibri_swarm (localhost:8002) — local swarm facts');
    console.log('    3. Node 1 swarm (217.60.249.157:8001) — remote 30K facts');
    console.log('    4. kolibriai.ru — LLM streaming fallback');
    console.log('');
    console.log('  Response cache: enabled (500 entries, 5min TTL)');
    console.log('');
    
    // Initial health check
    checkHealth().then(h => {
        console.log('  Health:');
        Object.entries(h).forEach(([name, info]) => {
            console.log(`    ${info.ok ? '✅' : '❌'} ${name}: ${JSON.stringify(info)}`);
        });
        console.log('');
    });
});
