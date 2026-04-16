import puppeteer from 'puppeteer';

(async () => {
    console.log('🌐 Launching browser to inspect frontend...\n');
    
    const browser = await puppeteer.launch({
        headless: true,
        args: ['--no-sandbox', '--disable-setuid-sandbox']
    });
    
    const page = await browser.newPage();
    await page.setViewport({ width: 1280, height: 800 });
    
    // Navigate to frontend
    console.log('📍 Navigating to http://127.0.0.1:3000...');
    const response = await page.goto('http://127.0.0.1:3000', {
        waitUntil: 'networkidle2',
        timeout: 30000
    });
    
    console.log(`   HTTP Status: ${response.status()}\n`);
    
    // Check page title
    const title = await page.title();
    console.log(`📄 Page title: "${title}"`);
    
    // Check if React app mounted
    const rootContent = await page.evaluate(() => {
        const root = document.getElementById('root');
        return root ? root.innerHTML.length : 0;
    });
    console.log(`📦 React root content length: ${rootContent} bytes\n`);
    
    // Take screenshot
    console.log('📸 Taking screenshot...');
    await page.screenshot({ 
        path: '/Users/kolibri/Desktop/kolibri-project/frontend-screenshot.png',
        fullPage: true
    });
    console.log('   ✅ Screenshot saved to frontend-screenshot.png\n');
    
    // Check for errors in console
    page.on('console', msg => {
        if (msg.type() === 'error') {
            console.log(`❌ Console Error: ${msg.text()}`);
        }
    });
    
    page.on('pageerror', error => {
        console.log(`❌ Page Error: ${error.message}`);
    });
    
    // Wait a bit for React to render
    console.log('⏳ Waiting 3 seconds for React to render...');
    await new Promise(resolve => setTimeout(resolve, 3000));
    
    // Check rendered elements
    console.log('\n🔍 Checking rendered elements...');
    const elements = await page.evaluate(() => {
        const checks = {
            hasKolibriText: document.body.textContent?.includes('Kolibri') || document.body.textContent?.includes('Колибри'),
            hasChatElements: document.querySelectorAll('section, button, input').length,
            hasMantineStyles: document.querySelector('[data-mantine]') !== null,
            hasTailwindClasses: document.querySelector('[class*="bg-"]') !== null,
            hasReactRoot: document.getElementById('root') !== null,
            rootChildCount: document.getElementById('root')?.children?.length || 0,
        };
        return checks;
    });
    
    console.log(`   ${elements.hasKolibriText ? '✅' : '❌'} Kolibri text found`);
    console.log(`   ${elements.hasChatElements ? '✅' : '❌'} Interactive elements: ${elements.hasChatElements}`);
    console.log(`   ${elements.hasMantineStyles ? '✅' : '⚠️ '} Mantine data attributes`);
    console.log(`   ${elements.hasTailwindClasses ? '✅' : '❌'} Tailwind classes present`);
    console.log(`   ${elements.hasReactRoot ? '✅' : '❌'} React root exists`);
    console.log(`   ℹ️  Root children: ${elements.rootChildCount}\n`);
    
    // Get page text content
    console.log('📝 Page content preview (first 200 chars):');
    const textContent = await page.evaluate(() => {
        return document.body.textContent?.trim().substring(0, 200) || '';
    });
    console.log(`   "${textContent}"\n`);
    
    // Take another screenshot after interaction
    console.log('📸 Taking final screenshot...');
    await page.screenshot({ 
        path: '/Users/kolibri/Desktop/kolibri-project/frontend-screenshot-final.png',
        fullPage: true
    });
    
    await browser.close();
    
    console.log('✅ Browser inspection complete!');
    console.log('\n📊 Summary:');
    console.log('   Frontend is rendering:', elements.rootChildCount > 0 ? '✅ YES' : '❌ NO');
    console.log('   Content loaded:', elements.hasKolibriText ? '✅ YES' : '❌ NO');
    console.log('\n📸 Screenshots saved for visual inspection');
})();
