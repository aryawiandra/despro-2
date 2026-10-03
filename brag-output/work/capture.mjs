import { chromium } from 'playwright';
import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';

const FPS = 30;
const DURATION = 23.5;
const [mode, ...rest] = process.argv.slice(2);

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1920, height: 1080 }, deviceScaleFactor: 1 });
await page.goto('file://' + resolve('video.html'));
await page.evaluate(() => window.ready);

async function shoot(t, path) {
  await page.evaluate((tt) => window.render(tt), t);
  await page.screenshot({ path, type: 'jpeg', quality: 94 });
}

if (mode === 'stills') {
  mkdirSync('stills', { recursive: true });
  for (const s of rest) await shoot(Number(s), `stills/t${s}.jpg`);
} else {
  mkdirSync('frames', { recursive: true });
  const total = Math.round(DURATION * FPS);
  for (let f = 0; f < total; f++) {
    await shoot(f / FPS, `frames/f${String(f).padStart(5, '0')}.jpg`);
    if (f % 100 === 0) console.log(`frame ${f}/${total}`);
  }
}
await browser.close();
