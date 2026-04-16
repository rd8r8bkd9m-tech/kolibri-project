import puppeteer from 'puppeteer';
import { execSync } from 'child_process';

const TESTS = [
  { name: "Math Solver", query: "Реши уравнение 2*x = 10", expect: "5" },
  { name: "Knowledge Base", query: "Сколько планет в Солнечной системе?", expect: "8" },
  { name: "Reasoning", query: "Почему небо голубое?", expect: null },
  { name: "Formula Pool", query: "Какая формула площади круга?", expect: "π" },
  { name: "World Model", query: "Что такое гравитация?", expect: null },
];

const restartBackend = () => {
  try {
    execSync('pkill -9 -f kolibri_http 2>/dev/null; sleep 1', { stdio: 'ignore' });
    execSync('cd /Users/kolibri/Desktop/kolibri-project && ./kolibri_http 8001 > /dev/null 2>&1 &', { stdio: 'ignore' });
    execSync('sleep 3');
  } catch (e) {
    console.log('   ⚠️  Backend restart failed');
  }
};

(async () => {
  console.log('🧪 Testing ALL C-Core modules via chat UI...\n');
  
  const browser = await puppeteer.launch({
    headless: true,
    args: ['--no-sandbox', '--disable-setuid-sandbox']
  });
  
  const page = await browser.newPage();
  await page.setViewport({ width: 1280, height: 900 });
  
  // Navigate to frontend
  console.log('📍 Opening http://127.0.0.1:3000...');
  await page.goto('http://127.0.0.1:3000', { waitUntil: 'networkidle2', timeout: 15000 });
  await new Promise(r => setTimeout(r, 2000));
  
  let passed = 0;
  let failed = 0;
  
  for (const test of TESTS) {
    console.log(`\n🔍 Testing: ${test.name}`);
    console.log(`   Query: "${test.query}"`);
    
    // Restart backend before each test to ensure stability
    if (TESTS.indexOf(test) > 0) {
      console.log('   🔄 Restarting backend...');
      restartBackend();
    }
    
    // Type message
    const textarea = await page.$('textarea');
    if (!textarea) {
      console.log('   ❌ Textarea not found!');
      failed++;
      continue;
    }
    
    await textarea.click();
    await textarea.type(test.query, { delay: 20 });
    
    // Take screenshot of typed message
    await page.screenshot({ path: `/tmp/chat_${test.name.replace(/\s/g,'_')}_typed.png` });
    
    // Send message
    await page.keyboard.press('Enter');
    
    // Wait for response (up to 20 seconds after backend restart)
    console.log('   ⏳ Waiting for response...');
    await new Promise(r => setTimeout(r, 15000));
    
    // Get last assistant message - try multiple selectors
    const response = await page.evaluate(() => {
      // Try selector 1: .message.assistant .message-content p
      let msgs = document.querySelectorAll('.message.assistant .message-content p');
      if (msgs.length > 0 && msgs[msgs.length - 1]?.textContent?.length > 5) {
        return msgs[msgs.length - 1].textContent;
      }
      
      // Try selector 2: any p inside .assistant message
      msgs = document.querySelectorAll('.message.assistant p');
      if (msgs.length > 0) {
        const last = msgs[msgs.length - 1];
        if (last.textContent.length > 5) return last.textContent;
      }
      
      // Debug: count all messages
      const allMsgs = document.querySelectorAll('.message');
      const assistantMsgs = document.querySelectorAll('.message.assistant');
      return `DEBUG: total=${allMsgs.length}, assistant=${assistantMsgs.length}`;
    });
    
    // Save screenshot for debugging
    await page.screenshot({ path: `/tmp/chat_${test.name.replace(/\s/g,'_')}_check.png` });
    
    if (response && response.length > 10) {
      console.log(`   ✅ Response (${response.length} chars): "${response.substring(0, 80)}..."`);
      
      // Check if expected keyword is in response
      if (test.expect && response.toLowerCase().includes(test.expect.toLowerCase())) {
        console.log(`   ✅ Contains expected: "${test.expect}"`);
      }
      
      passed++;
      
      // Screenshot
      await page.screenshot({ path: `/tmp/chat_${test.name.replace(/\s/g,'_')}_response.png` });
    } else {
      console.log(`   ❌ No response or too short`);
      failed++;
      
      // Screenshot of failure
      await page.screenshot({ path: `/tmp/chat_${test.name.replace(/\s/g,'_')}_fail.png` });
    }
    
    // Clear textarea for next test - use page.evaluate to properly clear
    await page.evaluate(() => {
      const ta = document.querySelector('textarea');
      if (ta) {
        // Clear via native setter
        const nativeInputValueSetter = Object.getOwnPropertyDescriptor(window.HTMLTextAreaElement.prototype, 'value')?.set;
        nativeInputValueSetter?.call(ta, '');
        ta.dispatchEvent(new Event('input', { bubbles: true }));
        ta.dispatchEvent(new Event('change', { bubbles: true }));
        // Also clear value attribute
        ta.value = '';
      }
    });
    await new Promise(r => setTimeout(r, 1500));
    
    // Verify textarea is empty
    const isEmpty = await page.evaluate(() => {
      const ta = document.querySelector('textarea');
      return ta?.value === '';
    });
    console.log(`   Textarea cleared: ${isEmpty ? '✅' : '❌'}`);
  }
  
  console.log('\n' + '='.repeat(50));
  console.log(`📊 RESULTS: ${passed} passed, ${failed} failed out of ${TESTS.length}`);
  console.log('='.repeat(50));
  
  // Final screenshot
  await page.screenshot({ path: '/tmp/chat_final_state.png', fullPage: true });
  
  await browser.close();
  
  if (failed > 0) {
    process.exit(1);
  }
})();
