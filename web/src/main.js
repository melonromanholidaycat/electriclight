// App wiring. One page, two contexts: published to Pages it is a simulator,
// served by the guitar it is the control surface. It works out which at runtime
// and is never forked.

import { Engine, FRAME_MS } from './model/engine.js';
import {
  loadLibrary, saveLibrary, defaultLibrary, buildLayers, findPreset, findDefinition,
  presetsByDefinition, makePreset, makeId, invalidate, programFor, SLOT_COUNT,
} from './model/library.js';
import { NeckView } from './ui/neck.js';
import { Knob, FiveWay } from './ui/controls.js';
import { VARS, FUNCS, CONSTANTS } from './lang/ops.js';
import { compile } from './lang/compile.js';
import { encodeProgram, toBase64 } from './lang/serialize.js';

const $ = (sel) => document.querySelector(sel);

const app = {
  lib: null,
  engine: null,
  neck: null,
  knob: null,
  five: null,
  working: {},     // live param values for the selected preset, unsaved
  context: { mode: 'simulator' },
  editingDefId: null,
};

// --- boot --------------------------------------------------------------------

function boot() {
  app.lib = loadLibrary();
  app.engine = new Engine(app.lib.geometry, app.lib.output);
  app.neck = new NeckView($('#neck'));

  app.five = new FiveWay($('#sw'), { value: 0, onChange: () => selectSlot() });
  app.knob = new Knob($('#knob'), { value: 1, onChange: () => applyKnob() });

  document.querySelectorAll('.tab').forEach((b) => {
    b.addEventListener('click', () => showTab(b.dataset.tab));
  });

  bindPlay();
  bindEdit();
  buildSetup();
  bindReset();
  buildLangRef();

  selectSlot();
  renderLibrary();
  renderEditPicker();

  detectContext();
  requestAnimationFrame(loop);
}

async function detectContext() {
  try {
    const res = await fetch('api/status', { cache: 'no-store' });
    if (res.ok) {
      const info = await res.json();
      if (info && info.device === 'electriclight') {
        app.context = { mode: 'device', info };
        const el = $('#ctx');
        el.textContent = info.name || 'guitar';
        el.classList.add('device');
        return;
      }
    }
  } catch {
    /* no device here: Pages, or offline. Simulator it is. */
  }
  app.context = { mode: 'simulator' };
}

// --- frame loop --------------------------------------------------------------

let last = 0;
let acc = 0;
let fpsWindow = [];

function loop(now) {
  requestAnimationFrame(loop);
  if (!last) last = now;
  acc = Math.min(acc + (now - last), 250);
  last = now;

  let steps = 0;
  while (acc >= FRAME_MS && steps < 4) {
    app.engine.step();
    acc -= FRAME_MS;
    steps++;
  }
  if (!steps) return;

  app.neck.resize();
  app.neck.draw(app.engine, app.engine.out8);

  fpsWindow.push(now);
  while (fpsWindow.length && now - fpsWindow[0] > 1000) fpsWindow.shift();
  $('#stats').textContent = `${fpsWindow.length} fps`;

  const p = $('#power');
  const e = app.engine;
  p.textContent = `${Math.round(e.current)} mA est · ${e.pixels.length} LEDs` +
    (e.limited ? ' · current limited' : '');
  p.classList.toggle('limited', !!e.limited);
}

// --- slots and presets -------------------------------------------------------

function currentPreset() {
  return findPreset(app.lib, app.lib.slots[app.five.value]) || app.lib.presets[0];
}

function selectSlot() {
  const preset = currentPreset();
  app.working = preset ? { ...preset.layers[0].values } : {};
  applyCurrent();
  renderPlay();
  renderLibrary();
  app.five.setNames(app.lib.slots.map((id) => findPreset(app.lib, id)?.name || ''));
}

function applyCurrent() {
  const preset = currentPreset();
  if (!preset) return;
  const forEngine = {
    ...preset,
    layers: preset.layers.map((l, i) => (i === 0 ? { ...l, values: app.working } : l)),
  };
  const { layers, errors } = buildLayers(app.lib, forEngine);
  $('#playErr').textContent = errors.join('\n');
  if (!layers.length) return;
  app.engine.setLayers(layers);
  app.engine.knobTarget = preset.knob || null;
  applyKnob();
}

function applyKnob() {
  const e = app.engine;
  e.knob = app.knob.value;
  const target = e.knobTarget;
  if (!target) {
    app.knob.setLabel(`bright ${Math.round(e.knob * 100)}%`);
    return;
  }
  const layer = e.layers[target.layer];
  const idx = layer ? layer.program.params.findIndex((p) => p.name === target.param) : -1;
  if (layer && idx >= 0) {
    const p = layer.program.params[idx];
    const value = p.min + e.knob * (p.max - p.min);
    layer.params[idx] = value;
    app.working[p.name] = value;
    app.knob.setLabel(`${p.label} ${fmt(value)}`);
    const slider = document.querySelector(`input[data-param="${p.name}"]`);
    if (slider) {
      slider.value = value;
      slider.parentElement.querySelector('.val').textContent = fmt(value);
    }
  }
}

const fmt = (v) => (Math.abs(v) >= 100 ? v.toFixed(0) : Math.abs(v) >= 10 ? v.toFixed(1) : v.toFixed(3).replace(/0+$/, '').replace(/\.$/, ''));

// --- play tab ----------------------------------------------------------------

function bindPlay() {
  $('#knobBind').addEventListener('change', (e) => {
    const preset = currentPreset();
    preset.knob = e.target.value === '' ? null : { layer: 0, param: e.target.value };
    persist();
    applyCurrent();
  });
  $('#savePreset').addEventListener('click', () => {
    const preset = currentPreset();
    preset.layers[0].values = { ...app.working };
    persist();
    flash($('#savePreset'), 'Saved');
  });
  $('#presetAsNew').addEventListener('click', () => {
    const preset = currentPreset();
    const name = prompt('Name for the new preset', `${preset.name} 2`);
    if (!name) return;
    const fresh = makePreset(preset.layers[0].defId, name, app.working);
    fresh.knob = preset.knob;
    app.lib.presets.push(fresh);
    app.lib.slots[app.five.value] = fresh.id;
    persist();
    selectSlot();
  });
}

function renderPlay() {
  const preset = currentPreset();
  const host = $('#playParams');
  host.innerHTML = '';
  if (!preset) { $('#playName').textContent = 'No preset'; return; }

  const def = findDefinition(app.lib, preset.layers[0].defId);
  const { program, error } = def ? programFor(def) : { program: null, error: 'effect missing' };
  $('#playName').textContent = preset.name;
  $('#playDef').textContent = def ? def.name : '';
  $('#playNote').textContent = def?.note || '';

  const bind = $('#knobBind');
  bind.innerHTML = '<option value="">Master brightness</option>';

  if (!program) { $('#playErr').textContent = error; return; }

  for (const p of program.params) {
    const wrap = document.createElement('div');
    wrap.className = 'field';
    const value = app.working[p.name] ?? p.def;
    wrap.innerHTML = `<label>${p.label}<span class="val">${fmt(value)}</span></label>`;
    const input = document.createElement('input');
    input.type = 'range';
    input.min = p.min; input.max = p.max; input.step = p.step; input.value = value;
    input.dataset.param = p.name;
    input.addEventListener('input', () => {
      const v = parseFloat(input.value);
      app.working[p.name] = v;
      wrap.querySelector('.val').textContent = fmt(v);
      const idx = program.params.indexOf(p);
      if (app.engine.layers[0]) app.engine.layers[0].params[idx] = v;
    });
    wrap.appendChild(input);
    host.appendChild(wrap);

    const opt = document.createElement('option');
    opt.value = p.name;
    opt.textContent = p.label;
    bind.appendChild(opt);
  }
  bind.value = preset.knob?.param || '';
}

// --- library tab -------------------------------------------------------------

function renderLibrary() {
  const host = $('#libList');
  const open = new Set([...host.querySelectorAll('details[open]')].map((d) => d.dataset.def));
  host.innerHTML = '';
  const { groups, orphans } = presetsByDefinition(app.lib);
  const active = currentPreset();

  for (const g of groups) {
    const d = document.createElement('details');
    d.className = 'group';
    d.dataset.def = g.def.id;
    // Keep whatever the user opened, and always reveal the one now playing.
    d.open = (open.size > 0 && open.has(g.def.id)) || g.presets.some((p) => p.id === active?.id);
    d.innerHTML =
      `<summary><span class="gname">${esc(g.def.name)}</span>` +
      `<span class="gcount">${g.presets.length} preset${g.presets.length === 1 ? '' : 's'}</span></summary>` +
      (g.def.note ? `<div class="gnote">${esc(g.def.note)}</div>` : '');

    for (const p of g.presets) {
      d.appendChild(presetRow(p, p.id === active?.id));
    }
    const add = document.createElement('div');
    add.className = 'preset';
    const btn = document.createElement('button');
    btn.textContent = '+ preset from defaults';
    btn.addEventListener('click', () => {
      const name = prompt('Preset name', `${g.def.name} 2`);
      if (!name) return;
      const fresh = makePreset(g.def.id, name, {});
      app.lib.presets.push(fresh);
      persist();
      renderLibrary();
    });
    add.appendChild(btn);
    d.appendChild(add);
    host.appendChild(d);
  }

  if (orphans.length) {
    const d = document.createElement('details');
    d.className = 'group';
    d.open = true;
    d.innerHTML = '<summary><span class="gname">Composite</span></summary>';
    orphans.forEach((p) => d.appendChild(presetRow(p, p.id === active?.id)));
    host.appendChild(d);
  }
}

function presetRow(preset, isCurrent) {
  const row = document.createElement('div');
  row.className = 'preset' + (isCurrent ? ' current' : '');
  const name = document.createElement('span');
  name.className = 'pname';
  name.textContent = preset.name;
  row.appendChild(name);

  const slots = document.createElement('div');
  slots.className = 'pslots';
  for (let i = 0; i < SLOT_COUNT; i++) {
    const b = document.createElement('button');
    b.className = 'pslot' + (app.lib.slots[i] === preset.id ? ' on' : '');
    b.textContent = i + 1;
    b.title = `Assign to switch position ${i + 1}`;
    b.addEventListener('click', () => {
      app.lib.slots[i] = preset.id;
      persist();
      if (i === app.five.value) selectSlot(); else { renderLibrary(); refreshSwitchNames(); }
    });
    slots.appendChild(b);
  }
  row.appendChild(slots);

  const more = document.createElement('button');
  more.className = 'pmore';
  more.textContent = '⋯';
  more.addEventListener('click', () => presetMenu(preset));
  row.appendChild(more);
  return row;
}

function presetMenu(preset) {
  const what = prompt(`"${preset.name}": type r to rename, d to duplicate, x to delete`, 'r');
  if (what === 'r') {
    const name = prompt('New name', preset.name);
    if (name) { preset.name = name; persist(); selectSlot(); renderLibrary(); }
  } else if (what === 'd') {
    const copy = makePreset(preset.layers[0].defId, `${preset.name} copy`, preset.layers[0].values);
    copy.knob = preset.knob;
    app.lib.presets.push(copy);
    persist();
    renderLibrary();
  } else if (what === 'x') {
    if (app.lib.presets.length <= 1) return alert('Keep at least one preset.');
    app.lib.presets = app.lib.presets.filter((p) => p.id !== preset.id);
    app.lib.slots = app.lib.slots.map((id) => (id === preset.id ? app.lib.presets[0].id : id));
    persist();
    selectSlot();
    renderLibrary();
  }
}

function refreshSwitchNames() {
  app.five.setNames(app.lib.slots.map((id) => findPreset(app.lib, id)?.name || ''));
}

// --- edit tab ----------------------------------------------------------------

function bindEdit() {
  $('#editDef').addEventListener('change', (e) => {
    app.editingDefId = e.target.value;
    $('#editSrc').value = findDefinition(app.lib, app.editingDefId)?.source || '';
    status('');
  });
  $('#applyDef').addEventListener('click', () => {
    const def = findDefinition(app.lib, app.editingDefId);
    if (!def) return;
    const src = $('#editSrc').value;
    try {
      const program = compile(src);
      def.source = src;
      invalidate(def.id);
      persist();
      selectSlot();
      renderLibrary();
      const bytes = encodeProgram(program);
      status(
        `OK - ${bytes.length} bytes on the wire, ${program.params.length} params, ` +
        `stack depth ${program.stack}${program.usesPrev ? ', uses prev' : ''}\n${toBase64(bytes)}`,
        'good'
      );
    } catch (err) {
      status(err.message, 'bad');
    }
  });
  $('#saveAsDef').addEventListener('click', () => {
    const src = $('#editSrc').value;
    try { compile(src); } catch (err) { return status(err.message, 'bad'); }
    const name = prompt('Name for the new effect');
    if (!name) return;
    const def = { id: makeId('d'), name, note: '', source: src, builtin: false };
    app.lib.definitions.push(def);
    app.lib.presets.push(makePreset(def.id, `${name} default`, {}));
    app.editingDefId = def.id;
    persist();
    renderEditPicker();
    renderLibrary();
    status('Saved as a new effect, with one preset.', 'good');
  });
  $('#delDef').addEventListener('click', () => {
    const def = findDefinition(app.lib, app.editingDefId);
    if (!def) return;
    if (def.builtin) return status('Built-in effects cannot be deleted. Edit and "Save as new" instead.', 'bad');
    const used = app.lib.presets.filter((p) => p.layers.some((l) => l.defId === def.id));
    if (!confirm(`Delete "${def.name}" and its ${used.length} preset(s)?`)) return;
    app.lib.definitions = app.lib.definitions.filter((d) => d.id !== def.id);
    app.lib.presets = app.lib.presets.filter((p) => !used.includes(p));
    if (!app.lib.presets.length) app.lib.presets.push(makePreset(app.lib.definitions[0].id, 'Default', {}));
    app.lib.slots = app.lib.slots.map((id) => (findPreset(app.lib, id) ? id : app.lib.presets[0].id));
    persist();
    renderEditPicker();
    renderLibrary();
    selectSlot();
    status('Deleted.');
  });
  $('#newDef').addEventListener('click', () => {
    $('#editSrc').value =
      'param hue   0..360 = 200 "Colour"\n' +
      'param level 0..1   = 0.6 "Level"\n\n' +
      'h = hue\n' +
      's = 1\n' +
      'v = level\n';
    status('Scratch effect. "Save as new" when it does something.');
  });
}

function renderEditPicker() {
  const sel = $('#editDef');
  sel.innerHTML = '';
  for (const d of app.lib.definitions) {
    const o = document.createElement('option');
    o.value = d.id;
    o.textContent = d.name + (d.builtin ? '' : ' *');
    sel.appendChild(o);
  }
  app.editingDefId = app.editingDefId && findDefinition(app.lib, app.editingDefId)
    ? app.editingDefId
    : app.lib.definitions[0]?.id;
  sel.value = app.editingDefId;
  $('#editSrc').value = findDefinition(app.lib, app.editingDefId)?.source || '';
}

function status(text, cls = '') {
  const el = $('#editStatus');
  el.textContent = text;
  el.className = cls;
}

// --- setup tab ---------------------------------------------------------------

const SETUP_FIELDS = [
  { group: 'Neck', key: 'geometry.scaleLength', label: 'Scale length (mm)', type: 'number', step: 0.5, min: 200, max: 1200 },
  { key: 'geometry.frets', label: 'Frets', type: 'number', step: 1, min: 1, max: 36 },
  { key: 'geometry.ledsPerStrip', label: 'LEDs per strip', type: 'number', step: 1, min: 1, max: 120 },
  { key: 'geometry.mapping', label: 'LED spacing', type: 'select', options: [['even', 'Evenly spaced (a real LED tape)'], ['fret-midpoint', 'One per fret space']] },
  { key: 'geometry.firstFret', label: 'First LED sits above fret', type: 'number', step: 1, min: 0, max: 24 },
  { key: 'geometry.stripSpacing', label: 'Distance between the strips (mm)', type: 'number', step: 0.5, min: 2, max: 70 },
  { key: 'geometry.reversed.0', label: 'Bass strip runs body to nut', type: 'check' },
  { key: 'geometry.reversed.1', label: 'Treble strip runs body to nut', type: 'check' },

  { group: 'Output', key: 'output.brightnessCeiling', label: 'Brightness ceiling', type: 'range', min: 0, max: 1, step: 0.01 },
  { key: 'output.gamma', label: 'Gamma', type: 'range', min: 1, max: 3, step: 0.05 },
  { key: 'output.mAPerLed', label: 'mA per LED at full white', type: 'number', step: 1, min: 1, max: 200 },
  { key: 'output.currentBudget', label: 'Current budget (mA)', type: 'number', step: 10, min: 50, max: 20000 },
];

function getPath(obj, path) {
  return path.split('.').reduce((o, k) => o?.[k], obj);
}
function setPath(obj, path, value) {
  const keys = path.split('.');
  const last = keys.pop();
  keys.reduce((o, k) => o[k], obj)[last] = value;
}

function buildSetup() {
  const host = $('#setupFields');
  host.innerHTML = '';
  for (const f of SETUP_FIELDS) {
    if (f.group) {
      const h = document.createElement('h3');
      h.textContent = f.group;
      host.appendChild(h);
    }
    const wrap = document.createElement('div');
    wrap.className = 'field';
    const value = getPath(app.lib, f.key);
    const valSpan = f.type === 'range' ? `<span class="val">${fmt(value)}</span>` : '';
    wrap.innerHTML = `<label>${f.label}${valSpan}</label>`;

    let input;
    if (f.type === 'select') {
      input = document.createElement('select');
      for (const [v, t] of f.options) {
        const o = document.createElement('option');
        o.value = v; o.textContent = t;
        input.appendChild(o);
      }
      input.value = value;
    } else {
      input = document.createElement('input');
      input.type = f.type === 'check' ? 'checkbox' : f.type;
      if (f.min != null) input.min = f.min;
      if (f.max != null) input.max = f.max;
      if (f.step != null) input.step = f.step;
      if (f.type === 'check') input.checked = !!value; else input.value = value;
    }

    const commit = () => {
      let v;
      if (f.type === 'check') v = input.checked;
      else if (f.type === 'select') v = input.value;
      else v = parseFloat(input.value);
      if (f.type !== 'check' && f.type !== 'select') {
        if (!Number.isFinite(v)) return;
        // A zero LED count or zero frets divides by zero all the way down.
        if (f.min != null) v = Math.max(f.min, v);
        if (f.max != null) v = Math.min(f.max, v);
        input.value = v;
      }
      setPath(app.lib, f.key, v);
      if (f.type === 'range') wrap.querySelector('.val').textContent = fmt(v);
      if (f.key.startsWith('geometry')) app.engine.setGeometry(app.lib.geometry);
      else app.engine.setOutput(app.lib.output);
      persist();
      applyCurrent();
    };
    input.addEventListener(f.type === 'range' ? 'input' : 'change', commit);
    wrap.appendChild(input);
    host.appendChild(wrap);
  }

}

function bindReset() {
  $('#resetAll').addEventListener('click', () => {
    if (!confirm('Discard all effects, presets and settings?')) return;
    app.lib = defaultLibrary();
    persist();
    app.engine.setGeometry(app.lib.geometry);
    app.engine.setOutput(app.lib.output);
    buildSetup();
    renderEditPicker();
    renderLibrary();
    selectSlot();
  });
}

// --- language reference ------------------------------------------------------

function buildLangRef() {
  const host = $('#langRef > div');
  const describe = {
    t: 'seconds, on a fixed 60 Hz clock',
    fret: 'fractional fret number, 0 at the nut',
    u: 'physical position along the lit span, 0..1',
    side: '0 bass, 1 treble',
    n: 'LED index on its own strip',
    count: 'LEDs per strip',
    nfrets: 'frets on the neck',
    knob: 'the pot, 0..1',
    sw: 'the five-way, 0..4',
    prev: 'this pixel last frame',
    rnd: 'fixed random per pixel, 0..1',
  };
  host.innerHTML =
    `<p>Write <code>h</code> (0..360), <code>s</code> and <code>v</code> (0..1). One statement per line; ` +
    `a line continues while its brackets are open. Declare knobs with ` +
    `<code>param name min..max = default "Label"</code>.</p>` +
    `<p><strong>Inputs</strong></p><dl>` +
    VARS.map((v) => `<dt>${v}</dt><dd>${describe[v] || ''}</dd>`).join('') +
    `</dl><p><strong>Functions</strong><br><code>` +
    FUNCS.map((f) => `${f.name}/${f.arity}`).join(', ') +
    `</code></p><p><strong>Constants</strong><br><code>${Object.keys(CONSTANTS).join(', ')}</code></p>` +
    `<p>Division by zero is 0, not infinity. <code>mod</code> takes the sign of its divisor. ` +
    `Brightness ceiling, gamma and the current limit are applied after your effect and are out of its reach.</p>`;
}

// --- misc --------------------------------------------------------------------

function showTab(name) {
  document.querySelectorAll('.tab').forEach((b) => b.classList.toggle('on', b.dataset.tab === name));
  document.querySelectorAll('.panel').forEach((p) => p.classList.toggle('hidden', p.dataset.panel !== name));
  if (name === 'library') renderLibrary();
}

function persist() {
  saveLibrary(app.lib);
  refreshSwitchNames();
}

function flash(button, text) {
  const old = button.textContent;
  button.textContent = text;
  setTimeout(() => { button.textContent = old; }, 900);
}

const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', boot);
} else {
  boot();
}
