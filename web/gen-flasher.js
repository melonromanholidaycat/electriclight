// Builds the web flasher: a page that installs the firmware onto a board over
// USB from any desktop Chrome, with nothing to download and nothing to install.
//
// It exists because of a constraint in the brief. The owner has a phone and no
// computer, so every cabled session is borrowed time on somebody else's machine.
// Asking that machine for Python, ESP-IDF and a driver is how a twenty-minute
// job becomes an evening. This asks it for a browser tab.
//
// The offsets are read out of firmware/partitions/*.csv rather than written
// here. A flasher that writes the app to the wrong address produces a board
// that does not boot, discovered with the borrowed laptop already packed away.

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..');

// ESP-IDF's default, and not overridden in sdkconfig.defaults. Checked against
// that file by web/test/run.js so an override cannot land silently.
export const PARTITION_TABLE_OFFSET = 0x8000;
// The ESP32-S3 boots from offset 0. (The original ESP32 uses 0x1000; getting
// this wrong is the classic way to produce a board that will not start.)
export const BOOTLOADER_OFFSET = 0x0;

export const VARIANTS = ['4mb', '16mb'];

// Offset of a named partition, from the same CSV the firmware is built with.
export function partitionOffset(variant, name) {
  const csv = readFileSync(join(root, 'firmware', 'partitions', `${variant}.csv`), 'utf8');
  for (const line of csv.split('\n')) {
    if (line.trim().startsWith('#') || !line.includes(',')) continue;
    const cols = line.split(',').map((c) => c.trim());
    if (cols[0] !== name) continue;
    const offset = parseInt(cols[3], 16);
    if (Number.isNaN(offset)) throw new Error(`${variant}.csv: '${name}' has no usable offset`);
    return offset;
  }
  throw new Error(`${variant}.csv has no '${name}' partition`);
}

export function buildVersion() {
  const header = readFileSync(join(root, 'firmware', 'main', 'app_identity.h'), 'utf8');
  const v = header.match(/ELECTRICLIGHT_VERSION\s+"([^"]+)"/);
  let sha = process.env.GITHUB_SHA || '';
  if (!sha) {
    try {
      sha = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim();
    } catch { sha = ''; }
  }
  // The commit is the part that matters. A hand-bumped version number says what
  // somebody intended to release; the sha says what is actually in the binary.
  return `${v ? v[1] : '0.0.0'}+${sha ? sha.slice(0, 8) : 'local'}`;
}

export function manifestFor(variant) {
  return {
    name: 'electriclight',
    version: buildVersion(),
    // Offers "erase device" on a board that has not run this firmware before,
    // which clears whatever the factory left in flash.
    new_install_prompt_erase: true,
    builds: [
      {
        chipFamily: 'ESP32-S3',
        parts: [
          { path: 'bootloader.bin', offset: BOOTLOADER_OFFSET },
          { path: 'partition-table.bin', offset: PARTITION_TABLE_OFFSET },
          // Without this the app lands in ota_0 while the guitar is still told
          // to boot ota_1, so a rescue flash changes nothing at all.
          { path: 'ota_data_initial.bin', offset: partitionOffset(variant, 'otadata') },
          { path: 'electriclight.bin', offset: partitionOffset(variant, 'ota_0') },
        ],
      },
    ],
  };
}

export function generate(outDir) {
  const page = readFileSync(join(here, 'flasher', 'flash.html'), 'utf8');
  writeFileSync(join(outDir, 'flash.html'), page);
  for (const variant of VARIANTS) {
    const dir = join(outDir, 'firmware', variant);
    mkdirSync(dir, { recursive: true });
    writeFileSync(join(dir, 'manifest.json'), JSON.stringify(manifestFor(variant), null, 2) + '\n');
  }
  return { variants: VARIANTS.length, version: buildVersion() };
}

// Confirms that every file a published manifest names is actually there. The
// flasher is used on a borrowed computer, usually once; a manifest pointing at
// a missing binary would fail with the laptop open and no way to fix it.
export function verifyPublished(outDir) {
  const missing = [];
  let parts = 0;
  for (const variant of VARIANTS) {
    const dir = join(outDir, 'firmware', variant);
    const manifest = JSON.parse(readFileSync(join(dir, 'manifest.json'), 'utf8'));
    for (const build of manifest.builds) {
      for (const part of build.parts) {
        parts++;
        try {
          const bytes = readFileSync(join(dir, part.path)).length;
          if (bytes === 0) missing.push(`${variant}/${part.path} is empty`);
        } catch {
          missing.push(`${variant}/${part.path} is missing`);
        }
      }
    }
  }
  if (missing.length) {
    console.error('The flasher would be published broken:');
    for (const m of missing) console.error(`  - ${m}`);
    process.exit(1);
  }
  console.log(`flasher ok: ${parts} binaries across ${VARIANTS.length} layouts`);
}
