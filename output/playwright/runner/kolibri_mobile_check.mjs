import { chromium, devices } from 'playwright';
import fs from 'fs/promises';

const outDir = '/Users/kolibri/Projects/kolibri-project/output/playwright/mobile-check';
await fs.mkdir(outDir, { recursive: true });

const logs = [];
const errors = [];

const browser = await chromium.launch({ headless: true });
const context = await browser.newContext({
  ...devices['iPhone 14'],
  locale: 'ru-RU',
  timezoneId: 'Europe/Moscow',
});

const page = await context.newPage();
page.on('console', (msg) => {
  const text = `[${msg.type()}] ${msg.text()}`;
  logs.push(text);
  if (msg.type() === 'error') {
    errors.push(text);
  }
});
page.on('pageerror', (err) => errors.push(`[pageerror] ${err.message}`));

await page.goto('https://kolibriai.ru/', { waitUntil: 'networkidle', timeout: 60000 });
await page.waitForTimeout(1200);

const enterBtn = page.getByRole('button', { name: /войти в приложение/i });
if (await enterBtn.isVisible().catch(() => false)) {
  await enterBtn.click({ timeout: 10000 });
  await page.waitForTimeout(1500);
}

await page.screenshot({ path: `${outDir}/01-chat-home.png`, fullPage: true });

const agentBtn = page.getByRole('button', { name: /^агент$/i }).first();
await agentBtn.click({ timeout: 10000 });
await page.waitForTimeout(1200);
await page.screenshot({ path: `${outDir}/02-agent-controls.png`, fullPage: true });

const agentState = await page.evaluate(() => {
  const controls = document.querySelector('.agent-controls');
  const dashboard = document.querySelector('.agent-dashboard');
  const mobileSwitch = document.querySelector('.agent-mobile-switch');
  return {
    controlsVisible: !!controls && getComputedStyle(controls).display !== 'none',
    dashboardVisible: !!dashboard && getComputedStyle(dashboard).display !== 'none',
    mobileSwitchVisible: !!mobileSwitch && getComputedStyle(mobileSwitch).display !== 'none',
  };
});

const progressTab = page.getByRole('tab', { name: /2\.\s*прогресс/i }).first();
if (await progressTab.isVisible().catch(() => false)) {
  await progressTab.click({ timeout: 8000 });
  await page.waitForTimeout(1000);
  await page.screenshot({ path: `${outDir}/03-agent-progress.png`, fullPage: true });
}

const voiceBtn = page.locator('.gx-mobile-bottom .gx-mobile-bottom-item[aria-label="Голос"]').first();
await voiceBtn.click({ timeout: 10000 });
await page.waitForTimeout(1200);
await page.screenshot({ path: `${outDir}/04-voice-tab.png`, fullPage: true });

const voiceState = await page.evaluate(() => {
  const status = document.querySelector('.voice-mode-hint')?.textContent?.trim() || '';
  const modeBtns = [...document.querySelectorAll('.voice-mode-btn')].map((el) => ({
    text: el.textContent?.replace(/\s+/g, ' ').trim() || '',
    disabled: !!el.getAttribute('disabled') || !!el.disabled,
    active: el.classList.contains('active'),
  }));
  const actionBtns = [...document.querySelectorAll('.voice-actions .voice-btn')].map((el) =>
    el.textContent?.replace(/\s+/g, ' ').trim() || '',
  );
  return { status, modeBtns, actionBtns };
});

const layoutState = await page.evaluate(() => {
  const root = document.querySelector('.gx-shell');
  const mobileBottom = document.querySelector('.gx-mobile-bottom');
  return {
    viewport: { width: window.innerWidth, height: window.innerHeight },
    shellDisplay: root ? getComputedStyle(root).display : null,
    mobileBottomVisible: !!mobileBottom && getComputedStyle(mobileBottom).display !== 'none',
    url: location.href,
  };
});

await fs.writeFile(
  `${outDir}/report.json`,
  JSON.stringify({ layoutState, agentState, voiceState, errorCount: errors.length, errors, logs: logs.slice(0, 240) }, null, 2),
  'utf8',
);

await browser.close();
console.log(JSON.stringify({ outDir, layoutState, agentState, voiceState, errorCount: errors.length }, null, 2));
