#!/usr/bin/env node
// node tools/style_lint.mjs pack.json [more.json …]  — validates style packs.
import { readFileSync } from 'node:fs';
import { lintPack } from '../js/styledsl.js';

let bad = 0;
for (const file of process.argv.slice(2)) {
  const pack = JSON.parse(readFileSync(file, 'utf8'));
  const problems = lintPack(Array.isArray(pack) ? pack : [pack]);
  if (problems.length) { bad++; console.log(`${file}:`); for (const p of problems) console.log('  ' + p); }
  else console.log(`${file}: ok (${(Array.isArray(pack) ? pack : [pack]).length} styles)`);
}
if (!process.argv[2]) console.log('usage: node tools/style_lint.mjs pack.json');
process.exit(bad ? 1 : 0);
