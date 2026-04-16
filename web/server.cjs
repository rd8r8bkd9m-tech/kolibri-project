#!/usr/bin/env node
const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = 3000;
const BACKEND_URL = 'http://127.0.0.1:8001';
const DIST_DIR = path.join(__dirname, 'dist');

const MIME_TYPES = {
    '.html': 'text/html',
    '.js': 'application/javascript',
    '.css': 'text/css',
    '.json': 'application/json',
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
    '.gif': 'image/gif',
    '.svg': 'image/svg+xml',
    '.ico': 'image/x-icon',
    '.webmanifest': 'application/manifest+json',
    '.wasm': 'application/wasm',
};

function proxyRequest(req, res) {
    const options = {
        hostname: '127.0.0.1',
        port: 8001,
        path: req.url,
        method: req.method,
        headers: {
            ...req.headers,
            'Connection': 'keep-alive'
        }
    };

    const proxyReq = http.request(options, (proxyRes) => {
        res.writeHead(proxyRes.statusCode, {
            ...proxyRes.headers,
            'Access-Control-Allow-Origin': '*',
            'Access-Control-Allow-Methods': 'GET, POST, PUT, DELETE, OPTIONS',
            'Access-Control-Allow-Headers': 'Content-Type, Authorization'
        });
        proxyRes.pipe(res);
    });

    proxyReq.on('error', (e) => {
        if (e.code === 'ECONNRESET' || e.code === 'ECONNREFUSED') {
            res.writeHead(503, {'Content-Type': 'application/json'});
            res.end(JSON.stringify({error: 'Backend temporarily unavailable, retrying...'}));
        } else {
            res.writeHead(502);
            res.end(JSON.stringify({error: 'Backend unavailable'}));
        }
    });

    req.pipe(proxyReq);
}

const server = http.createServer((req, res) => {
    // Handle CORS preflight
    if (req.method === 'OPTIONS') {
        res.writeHead(200, {
            'Access-Control-Allow-Origin': '*',
            'Access-Control-Allow-Methods': 'GET, POST, PUT, DELETE, OPTIONS',
            'Access-Control-Allow-Headers': 'Content-Type, Authorization'
        });
        res.end();
        return;
    }

    // Proxy API requests to backend
    if (req.url.startsWith('/api/')) {
        proxyRequest(req, res);
        return;
    }

    // Serve static files
    let filePath = path.join(DIST_DIR, req.url === '/' ? 'index.html' : req.url);
    const ext = path.extname(filePath);
    const contentType = MIME_TYPES[ext] || 'application/octet-stream';

    fs.readFile(filePath, (err, content) => {
        if (err) {
            if (err.code === 'ENOENT') {
                // SPA fallback - serve index.html for non-file routes
                fs.readFile(path.join(DIST_DIR, 'index.html'), (err2, indexContent) => {
                    if (err2) {
                        res.writeHead(404);
                        res.end('Not found');
                    } else {
                        res.writeHead(200, { 'Content-Type': 'text/html' });
                        res.end(indexContent, 'utf-8');
                    }
                });
            } else {
                res.writeHead(500);
                res.end('Server error');
            }
        } else {
            res.writeHead(200, { 
                'Content-Type': contentType,
                'Access-Control-Allow-Origin': '*'
            });
            res.end(content, 'utf-8');
        }
    });
});

server.listen(PORT, '127.0.0.1', () => {
    console.log(`🚀 Kolibri Frontend Server`);
    console.log(`   Frontend: http://127.0.0.1:${PORT}`);
    console.log(`   Backend:  ${BACKEND_URL}`);
    console.log(`   API Proxy: Enabled`);
    console.log(`   Serving: ${DIST_DIR}`);
});
