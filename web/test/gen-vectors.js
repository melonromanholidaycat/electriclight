// Regenerates the golden vectors. Run this only when the language or the output
// chain changes on purpose, and read the diff before committing it: this file
// is the contract the firmware evaluator will be held to.

import { writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { buildVectors } from './vectors.js';

const here = dirname(fileURLToPath(import.meta.url));
const data = buildVectors();
writeFileSync(join(here, 'vectors.json'), JSON.stringify(data, null, 1) + '\n');
console.log(`wrote ${data.cases.length} cases, ${data.cases[0].frames.length} frames each`);
