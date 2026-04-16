import puppeteer from 'puppeteer';

(async () => {
    console.log('🌐 Launching browser to check 404 errors...\n');
    
    const browser = await puppeteer.launch({
        headless: true,
        args: ['--no-sandbox', '--disable-setuid-sandbox']
    });
    
    const page = await browser.newPage();
    await page.setViewport({ width: 1280, height: 800 });
    
    const errors = [];
    
    page.on('response', response => {
        if (response.status() === 404) {
            errors.push(response.url());
            console.log(`❌ 404: ${response.url()}`);
        }
    });
    
    page.on('console', msg => {
        if (msg.type() === 'error') {
            console.log(`❌ Console: ${msg.text()}`);
        }
    });
    
    console.log('📍 Navigating...');
    await page.goto('http://127.0.0.1:3000', {
        waitUntil: 'networkidle0',
        timeout: 30000
    });
    
    await new Promise(resolve => setTimeout(resolve, 2000));
    
    console.log(`\n📊 Total 404 errors: ${errors.length}`);
    if (errors.length > 0) {
        console.log('\n📋 Failed URLs:');
        errors.forEach((url, i) => console.log(`  ${i + 1}. ${url}`));
    }
    
    // Check what HTML contains
    const html = await page.content();
    console.log('\n📄 HTML length:', html.length);
    
    // Check for main elements
    const bodyText = await page.evaluate(() => document.body.textContent);
    console.log('\n📝 Body text preview:');
    console.log(bodyText?.substring(0, 500));
    
    await browser.close();
})();
