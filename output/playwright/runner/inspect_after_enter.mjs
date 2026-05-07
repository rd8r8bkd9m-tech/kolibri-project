import { chromium, devices } from 'playwright';

const browser = await chromium.launch({ headless: true });
const context = await browser.newContext({ ...devices['iPhone 14'], locale: 'ru-RU' });
const page = await context.newPage();
await page.goto('https://kolibriai.ru/', { waitUntil: 'networkidle', timeout: 60000 });
await page.waitForTimeout(1200);
const enterBtn = page.getByRole('button', { name: /войти в приложение/i });
if (await enterBtn.isVisible().catch(() => false)) {
  await enterBtn.click({ timeout: 10000 });
}
await page.waitForTimeout(2200);
const buttons = await page.evaluate(() => {
  return [...document.querySelectorAll('button')].map((el) => ({
    text: (el.textContent || '').replace(/\s+/g, ' ').trim(),
    cls: el.className,
    aria: el.getAttribute('aria-label') || '',
    visible: !!el.offsetParent,
  })).slice(0, 60);
});
console.log(JSON.stringify({ href: page.url(), buttonCount: buttons.length, buttons }, null, 2));
await page.screenshot({ path: '/Users/kolibri/Projects/kolibri-project/output/playwright/mobile-check/00-after-enter.png', fullPage: true });
await browser.close();
