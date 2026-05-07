import { chromium, devices } from 'playwright';

const browser = await chromium.launch({ headless: true });
const context = await browser.newContext({ ...devices['iPhone 14'], locale: 'ru-RU' });
const page = await context.newPage();
await page.goto('https://kolibriai.ru/', { waitUntil: 'networkidle', timeout: 60000 });
await page.waitForTimeout(2500);

const data = await page.evaluate(() => {
  const buttons = [...document.querySelectorAll('button')].map((el) => ({
    text: (el.textContent || '').replace(/\s+/g, ' ').trim(),
    cls: el.className,
    aria: el.getAttribute('aria-label') || '',
    display: getComputedStyle(el).display,
    visible: !!el.offsetParent,
  }));
  return {
    title: document.title,
    href: location.href,
    bodyTextStart: (document.body.innerText || '').slice(0, 500),
    buttonCount: buttons.length,
    buttons,
  };
});

console.log(JSON.stringify(data, null, 2));
await browser.close();
