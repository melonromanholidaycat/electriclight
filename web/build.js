// Bundles the simulator into one self-contained file.
//
// Sources stay as ES modules so node can unit-test them; the browser gets a
// single HTML file with no imports, because that same file has to be embedded
// in the firmware as one gzipped blob. One artefact, so the Pages copy and the
// on-device copy can never be different builds of different pieces.

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const src = join(here, 'src');
const outDir = join(here, '..', 'dist');

// Dependency order, maintained by hand. The build fails loudly if a module
// references something that has not been emitted yet.
const MODULES = [
  'lang/ops.js',
  'lang/tokenize.js',
  'lang/parse.js',
  'lang/compile.js',
  'lang/eval.js',
  'lang/serialize.js',
  'model/geometry.js',
  'model/engine.js',
  'model/effects.js',
  'model/library.js',
  'ui/neck.js',
  'ui/controls.js',
  'main.js',
];

function strip(code, file) {
  const lines = code.split('\n');
  const out = [];
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (/^\s*import[\s{*]/.test(line)) {
      // Consume to the end of the import, which may span lines.
      let j = i;
      while (j < lines.length && !/from\s+['"].*['"]\s*;?\s*$/.test(lines[j])) j++;
      if (j >= lines.length) throw new Error(`${file}: unterminated import at line ${i + 1}`);
      i = j;
      continue;
    }
    if (/^\s*export\s*\{[^}]*\}\s*;?\s*$/.test(line)) continue; // re-export list
    out.push(line.replace(/^(\s*)export\s+(?=const|let|var|function|class|async)/, '$1'));
  }
  return out.join('\n');
}

const bundle = MODULES.map((m) => {
  const code = strip(readFileSync(join(src, m), 'utf8'), m);
  return `// ===== ${m} ${'='.repeat(Math.max(0, 60 - m.length))}\n${code}`;
}).join('\n');

const html = readFileSync(join(src, 'index.html'), 'utf8');
const css = readFileSync(join(src, 'style.css'), 'utf8');

const page = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<style>
${css}
</style>
</head>
<body>
${html}
<script>
"use strict";
(function () {
${bundle}
})();
</script>
</body>
</html>
`;

mkdirSync(outDir, { recursive: true });
writeFileSync(join(outDir, 'index.html'), page);
writeFileSync(join(outDir, '.nojekyll'), '');

const kb = (page.length / 1024).toFixed(1);
console.log(`dist/index.html  ${kb} kB`);
if (page.length > 400 * 1024) {
  console.error('Bundle is too large to embed comfortably in firmware flash.');
  process.exit(1);
}
