// The stored world: definitions, presets, the five switch slots, and the
// hardware settings. Same shape in localStorage here and in NVS on the device,
// so the on-device UI can load one and the simulator the other.
//
// A preset holds a *list* of layers even though nothing in v1 creates more than
// one. Layering is the one part of the format that is genuinely expensive to
// retrofit, so the shape is reserved now and left unused.

import { compile } from '../lang/compile.js';
import { BUILTIN_DEFINITIONS, BUILTIN_PRESETS } from './effects.js';
import { DEFAULT_GEOMETRY } from './geometry.js';
import { DEFAULT_OUTPUT } from './engine.js';

export const LIBRARY_VERSION = 1;
export const SLOT_COUNT = 5;
const STORAGE_KEY = 'electriclight.library.v1';

let nextId = 1;
const makeId = (prefix) => `${prefix}_${Date.now().toString(36)}_${nextId++}`;

export function fullMask() {
  return { fromFret: -1, toFret: 999, sides: [true, true] };
}

export function makePreset(defId, name, values = {}) {
  return {
    id: makeId('p'),
    name,
    layers: [{ defId, values: { ...values }, mask: fullMask(), blend: 'normal' }],
    knob: null, // null = master brightness
  };
}

export function defaultLibrary() {
  const definitions = BUILTIN_DEFINITIONS.map((d) => ({ ...d, builtin: true }));
  const presets = BUILTIN_PRESETS.map((p) => makePreset(p.defId, p.name, p.values));
  const byDef = (id) => presets.find((p) => p.layers[0].defId === id);
  return {
    version: LIBRARY_VERSION,
    definitions,
    presets,
    slots: [
      byDef('comet').id,
      byDef('breathe').id,
      byDef('fretchase').id,
      byDef('standingwave').id,
      byDef('sparkle').id,
    ],
    geometry: { ...DEFAULT_GEOMETRY },
    output: { ...DEFAULT_OUTPUT },
  };
}

export function loadLibrary() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return defaultLibrary();
    const lib = JSON.parse(raw);
    if (lib.version !== LIBRARY_VERSION) return defaultLibrary();
    return migrate(lib);
  } catch {
    return defaultLibrary();
  }
}

export function saveLibrary(lib) {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(lib));
  } catch {
    /* private browsing, quota - not worth interrupting the user over */
  }
}

// Re-seed anything the stored copy is missing, so a library saved by an older
// build still opens.
function migrate(lib) {
  const base = defaultLibrary();
  lib.geometry = { ...base.geometry, ...(lib.geometry || {}) };
  lib.output = { ...base.output, ...(lib.output || {}) };
  lib.definitions = lib.definitions?.length ? lib.definitions : base.definitions;
  for (const b of base.definitions) {
    if (!lib.definitions.some((d) => d.id === b.id)) lib.definitions.push(b);
  }
  lib.presets = lib.presets?.length ? lib.presets : base.presets;
  lib.slots = Array.from({ length: SLOT_COUNT }, (_, i) => lib.slots?.[i] ?? base.slots[i]);
  return lib;
}

// --- compiled cache ----------------------------------------------------------

const cache = new Map(); // defId -> { source, program, error }

export function programFor(def) {
  const hit = cache.get(def.id);
  if (hit && hit.source === def.source) return hit;
  let entry;
  try {
    entry = { source: def.source, program: compile(def.source), error: null };
  } catch (err) {
    entry = { source: def.source, program: null, error: err.message };
  }
  cache.set(def.id, entry);
  return entry;
}

export function invalidate(defId) { cache.delete(defId); }

export function findDefinition(lib, id) { return lib.definitions.find((d) => d.id === id); }
export function findPreset(lib, id) { return lib.presets.find((p) => p.id === id); }

export function presetsByDefinition(lib) {
  const groups = lib.definitions.map((def) => ({ def, presets: [] }));
  const index = new Map(groups.map((g) => [g.def.id, g]));
  const orphans = [];
  for (const p of lib.presets) {
    const g = p.layers.length === 1 ? index.get(p.layers[0].defId) : null;
    if (g) g.presets.push(p); else orphans.push(p);
  }
  return { groups, orphans };
}

// Fill in defaults for any param the preset does not pin, and drop values for
// params that no longer exist - editing a definition must not break its presets.
export function resolveValues(program, values) {
  const out = new Float32Array(program.params.length);
  program.params.forEach((p, i) => {
    const v = values[p.name];
    out[i] = Number.isFinite(v) ? Math.min(p.max, Math.max(p.min, v)) : p.def;
  });
  return out;
}

// A preset plus the library -> the layer list the engine wants.
export function buildLayers(lib, preset) {
  const layers = [];
  const errors = [];
  for (const layer of preset.layers) {
    const def = findDefinition(lib, layer.defId);
    if (!def) { errors.push(`missing effect "${layer.defId}"`); continue; }
    const { program, error } = programFor(def);
    if (!program) { errors.push(`${def.name}: ${error}`); continue; }
    layers.push({
      program,
      params: resolveValues(program, layer.values),
      mask: layer.mask,
      blend: layer.blend,
      def,
    });
  }
  return { layers, errors };
}

export { makeId };
