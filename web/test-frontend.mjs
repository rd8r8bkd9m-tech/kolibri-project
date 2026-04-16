import http from 'http';
import fs from 'fs';
import path from 'path';

const BASE_URL = 'http://127.0.0.1:3000';
const API_URL = 'http://127.0.0.1:8001';

function fetchUrl(url) {
    return new Promise((resolve, reject) => {
        http.get(url, (res) => {
            let data = '';
            res.on('data', chunk => data += chunk);
            res.on('end', () => {
                resolve({ status: res.statusCode, headers: res.headers, data });
            });
        }).on('error', reject);
    });
}

async function testFrontend() {
    console.log('🧪 Testing Kolibri Frontend...\n');
    
    // Test 1: Frontend serves index.html
    console.log('1. Testing frontend index.html...');
    try {
        const res = await fetchUrl(BASE_URL + '/');
        if (res.status === 200 && res.data.includes('<title>Колибри AI</title>')) {
            console.log('   ✅ Frontend index.html OK\n');
        } else {
            console.log('   ❌ Frontend index.html FAILED\n');
        }
    } catch (e) {
        console.log('   ❌ Frontend unreachable:', e.message, '\n');
    }

    // Test 2: Backend health
    console.log('2. Testing backend health...');
    try {
        const res = await fetchUrl(API_URL + '/api/v1/health');
        const data = JSON.parse(res.data);
        if (data.status === 'ok') {
            console.log('   ✅ Backend health OK');
            console.log('   - corpus_patterns:', data.corpus_patterns);
            console.log('   - corpus_edges:', data.corpus_edges, '\n');
        }
    } catch (e) {
        console.log('   ❌ Backend unreachable:', e.message, '\n');
    }

    // Test 3: Check JS bundles load
    console.log('3. Testing JS bundle availability...');
    try {
        const res = await fetchUrl(BASE_URL + '/');
        const scriptMatch = res.data.match(/src="([^"]+\.js)"/);
        if (scriptMatch) {
            console.log('   ✅ Main script found:', scriptMatch[1]);
        }
    } catch (e) {
        console.log('   ❌ Failed to check scripts\n');
    }

    // Test 4: Check CSS availability
    console.log('\n4. Testing CSS bundle...');
    try {
        const res = await fetchUrl(BASE_URL + '/');
        const cssMatch = res.data.match(/href="([^"]+\.css)"/);
        if (cssMatch) {
            console.log('   ✅ Main CSS found:', cssMatch[1]);
        }
    } catch (e) {
        console.log('   ❌ Failed to check CSS\n');
    }

    // Test 5: Check Mantine styles
    console.log('\n5. Testing Mantine styles integration...');
    try {
        const res = await fetchUrl(BASE_URL + '/');
        if (res.data.includes('@mantine')) {
            console.log('   ✅ Mantine styles imported\n');
        } else {
            console.log('   ⚠️  Mantine styles not detected in HTML\n');
        }
    } catch (e) {
        console.log('   ❌ Failed\n');
    }

    // Test 6: Check dist folder structure
    console.log('6. Testing dist folder structure...');
    const distDir = '/Users/kolibri/Desktop/kolibri-project/frontend/dist';
    const requiredFiles = ['index.html', 'g-logo.svg'];
    for (const file of requiredFiles) {
        const exists = fs.existsSync(path.join(distDir, file));
        console.log(`   ${exists ? '✅' : '❌'} ${file}`);
    }
    
    const assetsDir = path.join(distDir, 'assets');
    if (fs.existsSync(assetsDir)) {
        const assets = fs.readdirSync(assetsDir);
        console.log(`   ✅ assets/ contains ${assets.length} files`);
        const jsFiles = assets.filter(f => f.endsWith('.js'));
        const cssFiles = assets.filter(f => f.endsWith('.css'));
        console.log(`   - ${jsFiles.length} JS bundles`);
        console.log(`   - ${cssFiles.length} CSS files`);
    }
    console.log('');

    // Test 7: API endpoints
    console.log('7. Testing API endpoints...');
    const endpoints = [
        '/api/v1/health',
        '/api/v1/ai/models',
    ];
    for (const endpoint of endpoints) {
        try {
            const res = await fetchUrl(API_URL + endpoint);
            console.log(`   ✅ ${endpoint} → ${res.status}`);
        } catch (e) {
            console.log(`   ❌ ${endpoint} → ${e.message}`);
        }
    }
    console.log('');

    console.log('✅ Testing complete!');
    console.log('\n📊 Summary:');
    console.log('   Frontend: http://127.0.0.1:3000');
    console.log('   Backend:  http://127.0.0.1:8001');
    console.log('\n🌐 Open in browser: http://127.0.0.1:3000');
}

testFrontend().catch(console.error);
