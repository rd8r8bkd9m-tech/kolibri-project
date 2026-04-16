import puppeteer from 'puppeteer';
import { execSync } from 'child_process';

const QUESTIONS = [
  { q: "Сколько будет 2+2?", category: "Математика" },
  { q: "Реши уравнение x+5=10", category: "Math Solver" },
  { q: "Какая формула площади круга?", category: "Formula Pool" },
  { q: "Сколько планет в Солнечной системе?", category: "Knowledge Base" },
  { q: "Что такое фотосинтез?", category: "Knowledge Base" },
  { q: "Кто написал Войну и мир?", category: "Knowledge Base" },
  { q: "Чему равно число Пи?", category: "Formula Pool" },
  { q: "Сколько будет 15 * 8?", category: "Математика" },
  { q: "Какая столица Австралии?", category: "Knowledge Base" },
  { q: "Сколько континентов на Земле?", category: "Knowledge Base" },
  { q: "Что такое гравитация?", category: "World Model" },
  { q: "Реши x² = 16", category: "Math Solver" },
  { q: "Какая скорость света?", category: "Knowledge Base" },
  { q: "Сколько дней в году?", category: "Knowledge Base" },
  { q: "Что такое ДНК?", category: "Knowledge Base" },
  { q: "Какой элемент обозначается Fe?", category: "Knowledge Base" },
  { q: "Сколько будет 100 / 4?", category: "Математика" },
  { q: "Кто открыл Америку?", category: "Knowledge Base" },
  { q: "Какая температура кипения воды?", category: "Knowledge Base" },
  { q: "Что такое квантовая физика?", category: "World Model" },
];

const restartBackend = () => {
  try {
    execSync('pkill -9 -f kolibri_http 2>/dev/null; sleep 1', { stdio: 'ignore' });
    execSync('cd /Users/kolibri/Desktop/kolibri-project && ./kolibri_http 8001 > /dev/null 2>&1 &', { stdio: 'ignore' });
    execSync('sleep 3');
  } catch (e) {}
};

(async () => {
  console.log('🧪 20 Questions Test — Kolibri AI Chat\n');
  
  const browser = await puppeteer.launch({
    headless: true,
    args: ['--no-sandbox', '--disable-setuid-sandbox']
  });
  
  const page = await browser.newPage();
  await page.setViewport({ width: 1280, height: 900 });
  
  await page.goto('http://127.0.0.1:3000', { waitUntil: 'networkidle2', timeout: 15000 });
  await new Promise(r => setTimeout(r, 2000));
  
  let passed = 0;
  let failed = 0;
  const results = [];
  
  for (let i = 0; i < QUESTIONS.length; i++) {
    const test = QUESTIONS[i];
    const num = i + 1;
    console.log(`\n[${num}/20] ${test.category}`);
    console.log(`  Q: "${test.q}"`);
    
    // Restart backend every 5 questions
    if (i > 0 && i % 5 === 0) {
      console.log('  🔄 Restarting backend...');
      restartBackend();
    }
    
    // Type question - use page.click + page.type for reliable input
    await page.click('textarea');
    await new Promise(r => setTimeout(r, 300));
    await page.type('textarea', test.q, { delay: 30 });
    await new Promise(r => setTimeout(r, 500));
    
    // Verify text was typed
    const typedText = await page.evaluate(() => {
      const ta = document.querySelector('textarea');
      return ta?.value || '';
    });
    
    if (!typedText) {
      // Fallback: use evaluate
      await page.evaluate((text) => {
        const ta = document.querySelector('textarea');
        if (ta) {
          const nativeInputValueSetter = Object.getOwnPropertyDescriptor(window.HTMLTextAreaElement.prototype, 'value')?.set;
          nativeInputValueSetter?.call(ta, text);
          ta.dispatchEvent(new Event('input', { bubbles: true }));
          ta.dispatchEvent(new Event('change', { bubbles: true }));
        }
      }, test.q);
    }
    
    // Send
    await page.keyboard.press('Enter');
    
    // Wait
    await new Promise(r => setTimeout(r, 10000));
    
    // Get response
    const response = await page.evaluate(() => {
      const msgs = document.querySelectorAll('.message.assistant .message-content p');
      if (msgs.length === 0) return null;
      return msgs[msgs.length - 1]?.textContent || null;
    });
    
    if (response && response.length > 10 && !response.startsWith('DEBUG')) {
      console.log(`  ✅ ${response.substring(0, 100)}${response.length > 100 ? '...' : ''}`);
      passed++;
      results.push({ num, q: test.q, cat: test.category, status: 'OK', resp: response.substring(0, 100) });
    } else {
      console.log(`  ❌ No response`);
      failed++;
      results.push({ num, q: test.q, cat: test.category, status: 'FAIL', resp: 'No response' });
    }
    
    // Clear textarea
    await page.evaluate(() => {
      const ta = document.querySelector('textarea');
      if (ta) {
        const setter = Object.getOwnPropertyDescriptor(window.HTMLTextAreaElement.prototype, 'value')?.set;
        setter?.call(ta, '');
        ta.dispatchEvent(new Event('input', { bubbles: true }));
      }
    });
    
    await new Promise(r => setTimeout(r, 1000));
  }
  
  console.log('\n' + '='.repeat(70));
  console.log('📊 RESULTS:');
  console.log('='.repeat(70));
  
  for (const r of results) {
    const icon = r.status === 'OK' ? '✅' : '❌';
    console.log(`  ${icon} [${r.num}] ${r.cat}: ${r.q.substring(0, 40)}`);
    if (r.status === 'OK') {
      console.log(`     → ${r.resp.substring(0, 80)}...`);
    }
  }
  
  console.log('\n' + '='.repeat(70));
  console.log(`✅ Passed: ${passed}/${QUESTIONS.length}`);
  console.log(`❌ Failed: ${failed}/${QUESTIONS.length}`);
  console.log(`📈 Success Rate: ${Math.round(passed / QUESTIONS.length * 100)}%`);
  console.log('='.repeat(70));
  
  // Final screenshot
  await page.screenshot({ path: '/tmp/chat_20questions_final.png', fullPage: true });
  
  await browser.close();
  
  if (failed > 0) process.exit(1);
})();
