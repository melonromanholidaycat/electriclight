// The device panel: what the guitar is doing, and how to change it.
//
// Only useful when this page is being served by the instrument. On Pages there
// is nothing to talk to, and the panel says so rather than offering buttons
// that cannot work - the owner has one phone and no console, so a control that
// silently does nothing is worse than no control.

import { findPreset, findDefinition, programFor, resolveValues, LIBRARY_VERSION }
  from '../model/library.js';
import { encodeProgram, toBase64 } from '../lang/serialize.js';

// What the firmware understands: compiled bytecode and values, never source.
// A null slot means "leave that switch position alone", which is how a preset
// that will not compile avoids taking the whole upload down with it.
export function buildEffectsPayload(lib) {
  const problems = [];
  const slots = lib.slots.map((presetId, i) => {
    const preset = findPreset(lib, presetId);
    if (!preset) { problems.push(`Slot ${i + 1} is empty`); return null; }
    const layer = preset.layers[0];
    const def = findDefinition(lib, layer.defId);
    if (!def) { problems.push(`${preset.name}: its effect is missing`); return null; }
    const { program, error } = programFor(def);
    if (!program) { problems.push(`${preset.name}: ${error}`); return null; }
    return {
      name: preset.name,
      program: toBase64(encodeProgram(program)),
      params: Array.from(resolveValues(program, layer.values)),
    };
  });
  return {
    payload: { version: LIBRARY_VERSION, slots, output: { ...lib.output } },
    problems,
  };
}

const fmtMs = (us) => `${(us / 1000).toFixed(1)} ms`;

function statusHtml(info) {
  const r = info.render || {};
  const st = info.selftest || {};
  const g = info.geometry || {};
  const budget = r.budgetUs || 16667;
  const load = r.avgUs ? Math.round((r.avgUs / budget) * 100) : 0;
  const heavy = load > 80;

  const rows = [
    ['Playing', `${r.effect || '?'} <span class="dim">(position ${(r.slot ?? 0) + 1})</span>`],
    ['Frame', r.avgUs
      ? `${fmtMs(r.avgUs)} average, ${fmtMs(r.worstUs)} worst &mdash; <strong class="${heavy ? 'bad' : 'good'}">${load}%</strong> of the ${fmtMs(budget)} a frame allows`
      : 'not rendering'],
    ['Late frames', `${r.late ?? 0} of ${r.frames ?? 0}`],
    ['Draw', `${Math.round(r.currentMa || 0)} mA${r.limited ? ' <strong class="bad">(limited)</strong>' : ''}`],
    ['Evaluator', st.ran
      ? `<span class="${st.ok ? 'good' : 'bad'}">${st.summary}</span>`
      : '<span class="dim">not checked on this build yet</span>'],
    ['Firmware', `${info.version || '?'} on ${info.slot || '?'}${info.pendingVerify ? ' <span class="bad">(on probation)</span>' : ''}`],
    ['Radio', `${info.wifi || '?'} at ${info.ip || '?'}`],
    ['Free memory', `${Math.round((info.heap || 0) / 1024)} kB`],
  ];
  return `<table class="kv">${rows.map(([k, v]) => `<tr><th>${k}</th><td>${v}</td></tr>`).join('')}</table>`;
}

// The page can change effects and output settings; it cannot yet change the
// pixel layout, because that means re-initialising the LED driver. Saying so is
// better than letting the two quietly disagree.
function geometryWarning(info, lib) {
  const g = info.geometry || {};
  const diffs = [];
  if (g.ledsPerStrip && g.ledsPerStrip !== lib.geometry.ledsPerStrip) {
    diffs.push(`${g.ledsPerStrip} LEDs per strip, not ${lib.geometry.ledsPerStrip}`);
  }
  if (g.frets && g.frets !== lib.geometry.frets) {
    diffs.push(`${g.frets} frets, not ${lib.geometry.frets}`);
  }
  if (!diffs.length) return '';
  return `<p class="note warn">The guitar is rendering ${diffs.join(' and ')}.
    Neck geometry is not sent with effects yet &mdash; changing it needs a
    firmware update, not an upload.</p>`;
}

export function createDevicePanel(host, app, { onLibraryReplaced }) {
  let timer = null;

  const say = (id, text, cls = '') => {
    const el = host.querySelector(`#${id}`);
    if (el) { el.textContent = text; el.className = `status ${cls}`; }
  };

  async function refresh() {
    if (app.context.mode !== 'device') return;
    try {
      const res = await fetch('api/status', { cache: 'no-store' });
      if (!res.ok) return;
      const info = await res.json();
      app.context.info = info;
      const box = host.querySelector('#devStatus');
      if (box) box.innerHTML = statusHtml(info) + geometryWarning(info, app.lib);
    } catch {
      /* the guitar went away mid-poll; the next tick will notice */
    }
  }

  async function push() {
    const { payload, problems } = buildEffectsPayload(app.lib);
    if (problems.length) {
      say('devPushStatus', problems.join('; '), 'bad');
      return;
    }
    say('devPushStatus', 'Sending...');
    try {
      const res = await fetch('api/effects', {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify(payload),
      });
      const body = await res.json().catch(() => ({}));
      if (res.ok && body.ok) {
        say('devPushStatus', 'Sent. The guitar will still have these after a power cycle.', 'good');
        refresh();
      } else {
        say('devPushStatus', body.error || `The guitar refused it (${res.status})`, 'bad');
      }
    } catch (err) {
      say('devPushStatus', `Could not reach the guitar: ${err.message}`, 'bad');
    }
  }

  async function installFirmware(file) {
    if (!file) return;
    say('devOtaStatus', `Uploading ${(file.size / 1024).toFixed(0)} kB...`);
    try {
      const res = await fetch('api/ota', { method: 'POST', body: file });
      const body = await res.json().catch(() => ({}));
      if (res.ok && body.ok) {
        say('devOtaStatus', 'Installed. The guitar is restarting; this page will '
          + 'come back on its own once it has.', 'good');
      } else {
        say('devOtaStatus', `Refused (${res.status}). The old firmware is still running.`, 'bad');
      }
    } catch (err) {
      // An interrupted upload is not a brick: the device discards a partial
      // image and keeps running what it has. Worth saying, because this is the
      // moment it feels most like it might not be true.
      say('devOtaStatus', `Upload failed: ${err.message}. The guitar is still `
        + 'running the firmware it had.', 'bad');
    }
  }

  async function joinNetwork() {
    const ssid = host.querySelector('#devSsid').value.trim();
    if (!ssid) { say('devJoinStatus', 'Needs a network name.', 'bad'); return; }
    say('devJoinStatus', 'Saving and restarting...');
    try {
      const res = await fetch('api/wifi', {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ ssid, password: host.querySelector('#devPass').value }),
      });
      if (res.ok) {
        say('devJoinStatus', `Restarting to join "${ssid}". Its own access point `
          + 'will disappear; rejoin your normal WiFi and look for the guitar '
          + 'at electriclight.local.', 'good');
      } else {
        say('devJoinStatus', `The guitar refused it (${res.status}).`, 'bad');
      }
    } catch (err) {
      // The restart kills the connection mid-reply, so a network error here is
      // the expected outcome rather than a failure. Saying otherwise would send
      // somebody looking for a problem that is not there.
      say('devJoinStatus', `Sent. If the guitar accepted it, it is restarting `
        + `to join "${ssid}" now.`, 'good');
    }
  }

  function render() {
    if (app.context.mode !== 'device') {
      host.innerHTML = `<h3>The guitar</h3>
        <p class="note">This page is running as a simulator, so there is nothing to
        control. Open it from the instrument's own network and this becomes the
        control surface &mdash; same page, same effects.</p>`;
      return;
    }

    host.innerHTML = `<h3>The guitar</h3>
      <div id="devStatus"></div>
      <div class="row">
        <button id="devPush">Send these effects to the guitar</button>
      </div>
      <p id="devPushStatus" class="status"></p>
      <p class="note">Sends the five switch positions as compiled effects, plus the
      output settings below. The guitar keeps them through a power cycle.</p>

      <h3>Firmware</h3>
      <div class="row">
        <input type="file" id="devOtaFile">
        <button id="devOta">Install</button>
      </div>
      <p id="devOtaStatus" class="status"></p>
      <p class="note">An interrupted upload is discarded and the guitar keeps
      running what it has. A new image that cannot get back on the network rolls
      itself back.</p>

      <h3>Network</h3>
      <div class="row">
        <input type="text" id="devSsid" placeholder="WiFi name" autocapitalize="off"
               autocorrect="off" spellcheck="false">
        <input type="password" id="devPass" placeholder="Password">
      </div>
      <div class="row"><button id="devJoin">Join this network</button></div>
      <p id="devJoinStatus" class="status"></p>
      <p class="note">Puts the guitar on your own WiFi instead of its own access
      point, which is worth doing once: after it the phone can reach the guitar
      and the internet at the same time, so downloading firmware and installing
      it stop being two different networks. The guitar restarts to join, and
      falls back to its own access point if it cannot.</p>`;

    host.querySelector('#devPush').addEventListener('click', push);
    host.querySelector('#devOta').addEventListener('click', () => {
      installFirmware(host.querySelector('#devOtaFile').files[0]);
    });
    host.querySelector('#devJoin').addEventListener('click', joinNetwork);
    refresh();
  }

  return {
    render,
    start() {
      render();
      clearInterval(timer);
      // Slow on purpose. This polls a device that is also rendering 60 frames a
      // second on the other core, and nothing here changes fast enough to care.
      if (app.context.mode === 'device') timer = setInterval(refresh, 3000);
    },
    stop() { clearInterval(timer); timer = null; },
  };
}
