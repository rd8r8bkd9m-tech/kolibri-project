import { chromium } from 'playwright';
import { execSync } from 'child_process';

const QUESTIONS = [
  { q: "Сколько будет 2+2?", cat: "Математика" },
  { q: "Реши уравнение x+5=10", cat: "Math Solver" },
  { q: "Какая формула площади круга?", cat: "Formula Pool" },
  { q: "Сколько планет в Солнечной системе?", cat: "Knowledge" },
  { q: "Что такое фотосинтез?", cat: "Knowledge" },
  { q: "Кто написал Войну и мир?", cat: "Knowledge" },
  { q: "Чему равно число Пи?", cat: "Formula" },
  { q: "Сколько будет 15 умножить на 8?", cat: "Math" },
  { q: "Какая столица Австралии?", cat: "Knowledge" },
  { q: "Сколько континентов на Земле?", cat: "Knowledge" },
  { q: "Что такое гравитация?", cat: "World Model" },
  { q: "Реши x в квадрате равно 16", cat: "Math Solver" },
  { q: "Какая скорость света?", cat: "Knowledge" },
  { q: "Сколько дней в году?", cat: "Knowledge" },
  { q: "Что такое ДНК?", cat: "Knowledge" },
  { q: "Какой элемент обозначается Fe?", cat: "Knowledge" },
  { q: "Сколько будет 100 разделить на 4?", cat: "Math" },
  { q: "Кто открыл Америку?", cat: "Knowledge" },
  { q: "Какая температура кипения воды?", cat: "Knowledge" },
  { q: "Что такое квантовая физика?", cat: "World Model" },
];

const restartBackend = () => {
  try {
    execSync('pkill -9 -f kolibri_http 2>/dev/null; sleep 1', { stdio: 'ignore' });
    execSync('cd /Users/kolibri/Desktop/kolibri-project && ./kolibri_http 8001 > /dev/null 2>&1 &', { stdio: 'ignore' });
    execSync('sleep 4');
  } catch (e) {
    console.log('   ⚠️ Restart failed');
  }
};

(async () => {
  console.log('🧪 20 Questions Test via Playwright\n');
  
  const browser = await chromium.launch({ headless: true });
  const page = await browser.newPage();
  await page.setViewportSize({ width: 1280, height: 900 });
  
  console.log('📍 Opening http://127.0.0.1:3000...');
  await page.goto('http://127.0.0.1:3000', { waitUntil: 'networkidle', timeout: 15000 });
  await page.waitForTimeout(2000);
  
  let passed = 0;
  let failed = 0;
  const results = [];
  
  for (let i = 0; i < QUESTIONS.length; i++) {
    const test = QUESTIONS[i];
    const num = i + 1;
    console.log(`\n[${num}/20] ${test.cat}`);
    console.log(`  Q: "${test.q}"`);
    
    // Restart backend every 5 questions
    if (i > 0 && i % 5 === 0) {
      console.log('  🔄 Restarting backend...');
      restartBackend();
      // Re-navigate after restart
      await page.goto('http://127.0.0.1:3000', { waitUntil: 'networkidle', timeout: 15000 });
      await page.waitForTimeout(2000);
    }
    
    // Type question using Playwright's reliable fill
    try {
      await page.locator('textarea').fill(test.q);
      await page.waitForTimeout(500);
      
      // Verify filled
      const filledValue = await page.locator('textarea').inputValue();
      if (!filledValue || filledValue.length < 5) {
        console.log('  ⚠️  Fill may have failed, retrying...');
        await page.locator('textarea').click();
        await page.keyboard.type(test.q, { delay: 20 });
        await page.waitForTimeout(500);
      }
      
      // Send
      await page.locator('textarea').press('Enter');
      
      // Wait for response
      console.log('  ⏳ Waiting...');
      await page.waitForTimeout(12000);
      
      // Get last assistant message
      const responseText = await page.evaluate(() => {
        const msgs = document.querySelectorAll('.message.assistant .message-content p');
        if (msgs.length === 0) return null;
        return msgs[msgs.length - 1]?.textContent || null;
      });
      
      if (responseText && responseText.length > 10) {
        console.log(`  ✅ ${responseText.substring(0, 100)}${responseText.length > 100 ? '...' : ''}`);
        passed++;
        results.push({ num, cat: test.cat, q: test.q, status: 'OK', resp: responseText.substring(0, 80) });
      } else {
        console.log(`  ❌ No valid response (got: ${responseText ? responseText.substring(0, 50) : 'null'})`);
        failed++;
        results.push({ num, cat: test.cat, q: test.q, status: 'FAIL', resp: responseText || 'null' });
      }
    } catch (err) {
      console.log(`  ❌ Error: ${err.message}`);
      failed++;
      results.push({ num, cat: test.cat, q: test.q, status: 'ERROR', resp: err.message.substring(0, 80) });
    }
  }
  
  console.log('\n' + '='.repeat(70));
  console.log('📊 RESULTS:');
  console.log('='.repeat(70));
  
  for (const r of results) {
    const icon = r.status === 'OK' ? '✅' : '❌';
    console.log(`  ${icon} [${r.num}] ${r.cat}: ${r.q.substring(0, 45)}`);
    if (r.status === 'OK') {
      console.log(`     → ${r.resp}...`);
    }
  }
  
  console.log('\n' + '='.repeat(70));
  console.log(`✅ Passed: ${passed}/${QUESTIONS.length}`);
  console.log(`❌ Failed: ${failed}/${QUESTIONS.length}`);
  console.log(`📈 Success Rate: ${Math.round(passed / QUESTIONS.length * 100)}%`);
  console.log('='.repeat(70));
  
  await page.screenshot({ path: '/tmp/20questions_final.png', fullPage: true });
  
  await browser.close();
})();
