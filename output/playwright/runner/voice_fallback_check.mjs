import { chromium, devices } from 'playwright';
import fs from 'fs/promises';

const outDir = '/Users/kolibri/Projects/kolibri-project/output/playwright/mobile-check';
await fs.mkdir(outDir, { recursive: true });

const browser = await chromium.launch({ headless: true });
const context = await browser.newContext({ ...devices['iPhone 14'], locale: 'ru-RU' });
const page = await context.newPage();
await page.goto('https://kolibriai.ru/', { waitUntil: 'networkidle', timeout: 60000 });
const enterBtn = page.getByRole('button', { name: /войти в приложение/i });
if (await enterBtn.isVisible().catch(() => false)) {
  await enterBtn.click({ timeout: 10000 });
}
await page.waitForTimeout(1200);

await page.locator('.gx-mobile-bottom .gx-mobile-bottom-item[aria-label="Голос"]').first().click();
await page.waitForTimeout(1200);

const ta = page.locator('.voice-input');
await ta.fill('Сделай короткий ответ: тест fallback голоса.');
await page.getByRole('button', { name: /отправить текст/i }).click();
await page.waitForTimeout(35000);

const state = await page.evaluate(() => {
  const active = document.querySelector('.voice-mode-btn.active')?.textContent?.replace(/\s+/g, ' ').trim() || '';
  const hint = document.querySelector('.voice-mode-hint')?.textContent?.replace(/\s+/g, ' ').trim() || '';
  const response = document.querySelector('.voice-output-body')?.textContent?.replace(/\s+/g, ' ').trim() || '';
  const err = document.querySelector('.voice-error')?.textContent?.replace(/\s+/g, ' ').trim() || '';
  return { active, hint, responseLen: response.length, error: err };
});

await page.screenshot({ path: `${outDir}/05-voice-fallback.png`, fullPage: true });
await fs.writeFile(`${outDir}/voice_fallback_report.json`, JSON.stringify(state, null, 2), 'utf8');
console.log(JSON.stringify(state, null, 2));

await browser.close();
