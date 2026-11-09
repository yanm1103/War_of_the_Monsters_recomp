// The editor's colors keep their contrast (WCAG 2.1): text 4.5:1 on the surfaces it sits on, the border of a field 3:1. It reads the
// tokens of web/style.css (the dark set and the light set), so a color changed there is checked at once. No browser needed.
// Run: node tools/level_editor/test_contrast.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const css = fs.readFileSync(path.join(path.dirname(fileURLToPath(import.meta.url)), 'web', 'style.css'), 'utf8');
const block = (head) => {
  const i = css.indexOf(head);
  if (i < 0) throw new Error('no block ' + head);
  return css.slice(i, css.indexOf('}', i));
};
const tokens = (b) => Object.fromEntries([...b.matchAll(/--([a-z0-9-]+):\s*(#[0-9A-Fa-f]{6})/g)].map((m) => [m[1], m[2]]));
const sets = { dark: tokens(block(':root, :root[data-theme="dark"]')), light: tokens(block(':root[data-theme="light"] {')) };

const lin = (c) => { c /= 255; return c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4; };
const lum = (h) => { const [r, g, b] = [1, 3, 5].map((i) => lin(parseInt(h.slice(i, i + 2), 16))); return 0.2126 * r + 0.7152 * g + 0.0722 * b; };
const ratio = (a, b) => { const x = lum(a), y = lum(b); return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05); };

// [foreground token, background token, minimum]
const PAIRS = [['fg', 'surface-1', 4.5], ['fg-2', 'surface-1', 4.5], ['muted', 'surface-1', 4.5], ['muted', 'surface-2', 4.5], ['muted', 'bg', 4.5],
  ['accent-text', 'surface-1', 4.5], ['ok', 'surface-1', 4.5], ['warn', 'surface-1', 4.5], ['danger', 'surface-1', 4.5],
  ['control', 'input-bg', 3], ['control', 'surface-1', 3]];
let bad = 0;
for (const [name, t] of Object.entries(sets)) {
  for (const [a, b, min] of PAIRS) {
    if (!t[a] || !t[b]) { console.log(`FAIL  ${name}: no token ${!t[a] ? a : b}`); bad++; continue; }
    const r = ratio(t[a], t[b]);
    if (r < min) { console.log(`FAIL  ${name}: ${a} ${t[a]} on ${b} ${t[b]} is ${r.toFixed(2)} (needs ${min})`); bad++; }
  }
}
console.log(bad ? `${bad} FAILED` : 'test_contrast: ok');
process.exit(bad ? 1 : 0);
