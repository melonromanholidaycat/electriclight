// Boots the built page in a phone-sized Chromium and drives it. The owner has
// no way to open a console, so a runtime error in main.js would reach him as a
// blank screen; this is the only thing standing between him and that.

import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';

const PORT = 8099;
const problems = [];
const check = (cond, msg) => { if (!cond) problems.push(msg); };

const TYPES = { '.html': 'text/html', '.json': 'application/json', '.bin': 'application/octet-stream' };

// Flipped on for the second half of the run, so the same page is driven in both
// of the contexts it has to work in rather than only the one Pages serves.
let pretendDevice = false;
let lastEffects = null;
const settingsRequests = [];

const DEVICE_STATUS = {
  device: 'electriclight', name: 'electriclight', version: '0.1.0-test',
  wifi: 'access point', ip: '192.168.4.1', slot: 'ota_0', pendingVerify: false,
  heap: 180000, uptime: 42,
  render: {
    frames: 600, late: 0, lastUs: 6900, worstUs: 9100, avgUs: 7000,
    budgetUs: 16667, currentMa: 310, limited: false, slot: 2, effect: 'Walk Up',
  },
  selftest: { ran: true, ok: true, ms: 19941, summary: '16/16 cases, 104 frames, all match' },
  geometry: { ledsPerStrip: 26, frets: 21, scaleLength: 648, brightnessCeiling: 0.5, gamma: 2.2, currentBudget: 1500 },
};

const server = createServer((req, res) => {
  const url = (req.url || '').split('?')[0];
  const path = url === '/' ? '/index.html' : url;

  if (pretendDevice && path === '/api/status') {
    res.writeHead(200, { 'content-type': 'application/json' });
    res.end(JSON.stringify(DEVICE_STATUS));
    return;
  }
  if (pretendDevice && ['/api/battery', '/api/diagnostic', '/api/radio'].includes(path) && req.method === 'POST') {
    let body = '';
    req.on('data', (c) => { body += c; });
    req.on('end', () => {
      settingsRequests.push({ path, body: JSON.parse(body) });
      res.writeHead(200, { 'content-type': 'application/json' });
      res.end('{"ok":true}');
    });
    return;
  }
  if (pretendDevice && path === '/api/effects' && req.method === 'POST') {
    let body = '';
    req.on('data', (c) => { body += c; });
    req.on('end', () => {
      lastEffects = body;
      res.writeHead(200, { 'content-type': 'application/json' });
      res.end('{"ok":true}');
    });
    return;
  }
  // Serve dist/ the way Pages does, so the flasher can reach its manifests.
  // Anything absent 404s - including api/status, since there is no device here.
  if (/^\/[\w./-]+$/.test(path) && !path.includes('..')) {
    try {
      const body = readFileSync(`dist${path}`);
      const ext = path.slice(path.lastIndexOf('.'));
      res.writeHead(200, { 'content-type': TYPES[ext] || 'application/octet-stream' });
      res.end(body);
      return;
    } catch { /* fall through to 404 */ }
  }
  res.writeHead(404);
  res.end('not found');
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

// --- the page as the guitar serves it -----------------------------------------
//
// The same file is the simulator and the control surface, and until now only
// the simulator half was ever driven. A device panel that throws on a real
// guitar would reach the owner as a blank tab on a phone with no console.

pretendDevice = true;
const dev = await ctx.newPage();
const devProblems = [];
dev.on('pageerror', (e) => devProblems.push(`device page uncaught: ${e.message}`));
dev.on('console', (m) => { if (m.type() === 'error') devProblems.push(`device page: ${m.text()}`); });

await dev.goto(`http://localhost:${PORT}/`, { waitUntil: 'load' });
await dev.waitForTimeout(900);

check(await dev.$eval('#ctx', (e) => e.classList.contains('device')),
  'the page did not notice it was being served by the guitar');

await dev.click('.tab[data-tab=setup]');
await dev.waitForTimeout(200);

const panel = await dev.evaluate(() => ({
  rows: document.querySelectorAll('#devStatus table.kv tr').length,
  hasPush: !!document.querySelector('#devPush'),
  hasOta: !!document.querySelector('#devOta'),
  hasJoin: !!document.querySelector('#devJoin'),
  otaAccept: document.querySelector('#devOtaFile')?.getAttribute('accept'),
  text: document.querySelector('#devStatus')?.textContent || '',
}));
check(panel.rows >= 6, `device panel showed ${panel.rows} status rows`);
check(panel.hasPush && panel.hasOta && panel.hasJoin, 'device panel is missing its controls');
// iOS filters a file picker by type, and .bin maps to no useful UTI - a narrow
// accept can grey the firmware out entirely on the one device that has to pick
// it. There is nothing to gain by filtering here.
check(!panel.otaAccept,
  `the firmware picker filters by "${panel.otaAccept}", which iOS may honour by showing nothing`);
// Everything below needs the controls to exist. Bail with a readable failure
// rather than letting Playwright time out waiting for a button that is not
// coming.
if (!panel.hasPush) {
  problems.push('device panel never rendered; skipping the rest of its checks');
}
check(/Walk Up/.test(panel.text), 'device panel does not show what is playing');
check(/104 frames/.test(panel.text), 'device panel does not show the self-test result');

// The push has to produce something the firmware would actually accept, so the
// fake device keeps the body and it is checked rather than just the status text.
if (panel.hasPush) {
await dev.click('#devPush');
await dev.waitForTimeout(400);
const pushed = await dev.$eval('#devPushStatus', (e) => ({ text: e.textContent, cls: e.className }));
check(/good/.test(pushed.cls), `push reported: ${pushed.text}`);
check(lastEffects !== null, 'pushing sent nothing');
if (lastEffects) {
  const sent = JSON.parse(lastEffects);
  check(Array.isArray(sent.slots) && sent.slots.length === 5,
    `sent ${sent.slots?.length} slots, expected 5`);
  check(sent.slots.every((x) => x && typeof x.program === 'string'),
    'a sent slot carried no program');
  check(sent.output && typeof sent.output.brightnessCeiling === 'number',
    'output settings were not sent with the effects');
}
}

// Exercise the phone controls through their actual requests, including defaults
// that must keep an unwired board usable.
check(!await dev.isChecked('#devBatteryEnabled'), 'battery monitoring enabled on an unconfigured board');
await dev.fill('#devBatteryRatio', '4.1');
await dev.fill('#devBatteryDim', '6.8');
await dev.fill('#devBatteryCutoff', '6.1');
await dev.check('#devBatteryEnabled');
await dev.click('#devBatterySave');
await dev.waitForSelector('#devBatteryStatus.good');
const batteryRequest = settingsRequests.find((r) => r.path === '/api/battery');
check(batteryRequest?.body.enabled === true && batteryRequest.body.dividerRatio === 4.1 &&
  batteryRequest.body.dimVolts === 6.8 && batteryRequest.body.cutoffVolts === 6.1,
  `battery controls sent wrong settings: ${JSON.stringify(batteryRequest)}`);
for (const mode of ['1', '2', '3', '4', '5', '0']) {
  await dev.selectOption('#devDiagnostic', mode);
  await dev.click('#devTest');
  await dev.waitForSelector('#devTestStatus.good');
  check(settingsRequests.at(-1)?.body.mode === Number(mode), `diagnostic mode ${mode} not sent`);
}
await dev.uncheck('#devRadioAlways');
await dev.click('#devRadioSave');
await dev.waitForSelector('#devRadioStatus.good');
check(settingsRequests.at(-1)?.body.alwaysOn === false, 'radio policy was not sent');

for (const p of devProblems) problems.push(p);
pretendDevice = false;

// --- the flasher --------------------------------------------------------------
//
// This page gets one chance on a borrowed computer. Everything that would waste
// that chance - a manifest that does not load, a pinned CDN URL that 404s, a
// script error leaving a dead button - is cheap to catch here and expensive to
// catch there.

const cdn = readFileSync('web/flasher/flash.html', 'utf8')
  .match(/https:\/\/unpkg\.com\/esp-web-tools@[^'"]+/)[0];

// Reachability is checked from node, not from the page: node honours the
// environment's proxy and CA bundle, so this answers "is the dependency
// published where the flasher says it is" rather than "can this particular
// sandbox's browser reach the internet".
const cdnStatus = await fetch(cdn, { method: 'GET' })
  .then((r) => r.status).catch((e) => `failed: ${e.message}`);
check(cdnStatus === 200, `the flasher's pinned dependency is unreachable (${cdn}): ${cdnStatus}`);

const flash = await ctx.newPage();
const flashProblems = [];
flash.on('pageerror', (e) => flashProblems.push(`flasher uncaught: ${e.message}`));
flash.on('console', (m) => {
  if (m.type() !== 'error') return;
  // Failures fetching the external dependency have their own assertions below;
  // a browser that cannot reach a CDN is an environment, not a bug.
  if ((m.location()?.url || '').startsWith('https://unpkg.com/')) return;
  flashProblems.push(`flasher console: ${m.text()}`);
});

await flash.goto(`http://localhost:${PORT}/flash.html`, { waitUntil: 'load' });
await flash.waitForTimeout(1500);

// The build stamp comes from the manifest, so this failing means the page could
// not read it - which is also how it would fail on the borrowed laptop.
const stamp = await flash.$eval('#build-id', (e) => e.textContent.trim());
check(/^\d+\.\d+\.\d+\+/.test(stamp), `flasher could not read its manifest: "${stamp}"`);

const state = await flash.evaluate(() => ({
  serial: 'serial' in navigator,
  unsupported: getComputedStyle(document.getElementById('unsupported')).display !== 'none',
  supported: !document.getElementById('supported').hidden,
  defined: !!customElements.get('esp-web-install-button'),
  why: document.getElementById('unsupported').textContent.replace(/\s+/g, ' ').trim(),
}));

// Exactly one panel, always. A page showing neither is a page that looks broken
// to somebody standing over a borrowed laptop.
check(state.supported !== state.unsupported,
  `flasher shows both or neither panel: ${JSON.stringify(state)}`);

if (state.supported) {
  // If it offers to flash, the button has to be a real upgraded element rather
  // than inert markup that does nothing when clicked.
  check(state.defined, 'flasher offers an Install button that is not a defined custom element');
} else {
  // If it declines, it has to say why, and the reason has to be true: either
  // this browser has no Web Serial, or the dependency did not load.
  const noSerial = !state.serial && /cannot flash/i.test(state.why);
  const noModule = state.serial && /could not load/i.test(state.why);
  check(noSerial || noModule,
    `flasher declined without a true reason (serial=${state.serial}): ${state.why.slice(0, 120)}`);
}

const flashMode = state.supported ? 'install button live'
  : state.serial ? 'dependency did not load here' : 'no Web Serial in this browser';

for (const p of flashProblems) problems.push(p);

await browser.close();
server.close();

if (problems.length) {
  console.error(`\nsmoke test: ${problems.length} problem(s)\n`);
  for (const p of problems) console.error(`  x ${p}`);
  process.exit(1);
}
console.log(`smoke test passed - ${first.fps} fps, ${groups} effect groups, ${defCount} definitions`);
console.log(`flasher passed - build ${stamp}, ${flashMode}`);
