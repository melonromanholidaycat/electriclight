// Boots the built page in a phone-sized Chromium and drives it. The owner has
// no way to open a console, so a runtime error in main.js would reach him as a
// blank screen; this is the only thing standing between him and that.

import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';

const PORT = 8099;
const problems = [];
const check = (cond, msg) => { if (!cond) problems.push(msg); };

const server = createServer((req, res) => {
  const url = (req.url || '').split('?')[0];
  if (url === '/' || url === '/index.html') {
    res.writeHead(200, { 'content-type': 'text/html' });
    res.end(readFileSync('dist/index.html'));
  } else {
    // Pages behaves like this too: no device here, so api/status must 404.
    res.writeHead(404);
    res.end('not found');
  }
});
await new Promise((r) => server.listen(PORT, r));

const browser = await chromium.launch(
  process.env.CHROMIUM_PATH ? { executablePath: process.env.CHROMIUM_PATH } : {}
);
const ctx = await browser.newContext({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2 });
const page = await ctx.newPage();

const expected404 = /api\/status|favicon/;
page.on('pageerror', (e) => problems.push(`uncaught: ${e.message}`));
page.on('console', (m) => {
  if (m.type() !== 'error') return;
  if (expected404.test(m.location()?.url || '')) return;
  problems.push(`console: ${m.text()}`);
});

await page.goto(`http://localhost:${PORT}/`, { waitUntil: 'load' });
await page.waitForTimeout(900);

const first = await page.evaluate(() => ({
  context: document.querySelector('#ctx').textContent,
  fps: parseInt(document.querySelector('#stats').textContent, 10),
  power: document.querySelector('#power').textContent,
  preset: document.querySelector('#playName').textContent,
  sliders: document.querySelectorAll('#playParams input[type=range]').length,
  switches: [...document.querySelectorAll('.sw-name')].map((e) => e.textContent),
  error: document.querySelector('#playErr').textContent,
  lit: (() => {
    const c = document.querySelector('#neck');
    const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
    let sum = 0;
    for (let i = 0; i < d.length; i += 4) sum += d[i] + d[i + 1] + d[i + 2];
    return sum;
  })(),
}));

check(first.context === 'simulator', `context should be "simulator", got "${first.context}"`);
check(first.fps >= 20, `only ${first.fps} fps`);
check(first.sliders >= 3, `expected parameter sliders, got ${first.sliders}`);
check(!first.error, `play tab reported: ${first.error}`);
check(first.lit > 100000, 'the neck rendered black');
check(first.switches.every(Boolean), `unnamed switch positions: ${JSON.stringify(first.switches)}`);
check(/mA est/.test(first.power), `power readout looks wrong: ${first.power}`);

for (const tab of ['library', 'edit', 'setup', 'play']) {
  await page.click(`.tab[data-tab=${tab}]`);
  await page.waitForTimeout(100);
  const visible = await page.$eval(`.panel[data-panel=${tab}]`, (el) => !el.classList.contains('hidden'));
  check(visible, `${tab} tab did not open`);
}

await page.click('.tab[data-tab=library]');
const groups = await page.$$eval('#libList .group', (g) => g.length);
const named = await page.$$eval('#libList .gname', (g) => g.map((e) => e.textContent));
check(groups >= 5, `expected a group per effect, got ${groups}`);
check(new Set(named).size === named.length, 'duplicate groups in the library');

// Every switch position must land on a preset and keep rendering.
const seen = [];
for (let i = 0; i < 5; i++) {
  await page.click(`.sw-pos:nth-child(${i + 1})`);
  await page.waitForTimeout(180);
  seen.push(await page.evaluate(() => ({
    name: document.querySelector('#playName').textContent,
    err: document.querySelector('#playErr').textContent,
  })));
}
check(seen.every((s) => s.name && !s.err), `switch positions failed: ${JSON.stringify(seen)}`);

// Recompiling each built-in unchanged must succeed; a typo must not.
await page.click('.tab[data-tab=edit]');
const defCount = await page.$$eval('#editDef option', (o) => o.length);
for (let i = 0; i < defCount; i++) {
  await page.selectOption('#editDef', { index: i });
  await page.click('#applyDef');
  await page.waitForTimeout(60);
  const s = await page.$eval('#editStatus', (e) => ({ cls: e.className, text: e.textContent }));
  check(s.cls === 'good', `effect ${i} failed to recompile: ${s.text}`);
}
await page.fill('#editSrc', 'v = bogus(1)');
await page.click('#applyDef');
const bad = await page.$eval('#editStatus', (e) => ({ cls: e.className, text: e.textContent }));
check(bad.cls === 'bad' && /unknown function/.test(bad.text), `bad source was accepted: ${bad.text}`);

// Settings must take effect without reload.
await page.click('.tab[data-tab=setup]');
await page.$$eval('#setupFields .field', (fields) => {
  const f = fields.find((el) => el.querySelector('label').textContent.startsWith('LEDs per strip'));
  const input = f.querySelector('input');
  input.value = '24';
  input.dispatchEvent(new Event('change'));
});
await page.waitForTimeout(250);
const afterSetup = await page.evaluate(() => document.querySelector('#power').textContent);
check(/48 LEDs/.test(afterSetup), `LED count did not take effect: ${afterSetup}`);

// A zero must be clamped, not divide by zero all the way to a blank neck.
await page.$$eval('#setupFields .field', (fields) => {
  const f = fields.find((el) => el.querySelector('label').textContent.startsWith('LEDs per strip'));
  const input = f.querySelector('input');
  input.value = '0';
  input.dispatchEvent(new Event('change'));
});
await page.waitForTimeout(250);
const clamped = await page.evaluate(() => document.querySelector('#power').textContent);
check(/2 LEDs/.test(clamped), `zero LEDs was not clamped: ${clamped}`);

await page.click('.tab[data-tab=play]');
await page.waitForTimeout(400);
if (process.env.SMOKE_SHOT) await page.screenshot({ path: process.env.SMOKE_SHOT });

await browser.close();
server.close();

if (problems.length) {
  console.error(`\nsmoke test: ${problems.length} problem(s)\n`);
  for (const p of problems) console.error(`  x ${p}`);
  process.exit(1);
}
console.log(`smoke test passed - ${first.fps} fps, ${groups} effect groups, ${defCount} definitions`);
