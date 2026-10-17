// Prints the live frecency store: each key's frecency decayed to now, the
// bonus it adds to a match, and how long ago it was last picked or held.
// The constants are read out of the C++ headers, so the numbers are the ones
// Rudder ranks by. `just frecency-report [path/to/frecency.json]`.
import { readFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';
import { ROOT } from './lib.mjs';

function appData() {
  if (process.platform === 'darwin') return join(homedir(), 'Library', 'Application Support');
  if (process.platform === 'win32') return process.env.APPDATA;
  return process.env.XDG_DATA_HOME ?? join(homedir(), '.local', 'share');
}

// `constexpr ... name = <expr>;` from a header, evaluated: plain arithmetic
// on numeric literals only (suffixes like LL dropped).
function constant(header, name) {
  const source = readFileSync(join(ROOT, 'Lib', 'Switcher', header), 'utf8');
  const match = source.match(new RegExp(`\\b${name}\\s*=\\s*([^;]+);`));
  if (!match) throw new Error(`${name} not found in ${header}`);
  const expr = match[1].replace(/(\d)(LL|ULL|L|U|f)\b/g, '$1');
  if (!/^[\d\s.+\-*/()]+$/.test(expr)) throw new Error(`${name}: can't evaluate '${expr}'`);
  return Function(`return (${expr});`)();
}

const halfLifeMs = constant('Frecency.h', 'halfLifeMs');
const k = constant('Ranking.h', 'frecencyK');
const scale = constant('Ranking.h', 'frecencyScale');
const cap = constant('Ranking.h', 'frecencyCap');

const decayed = (score, at, now) => score * 2 ** (-Math.max(now - at, 0) / halfLifeMs);
const bonus = (f) => (f <= 0 ? 0 : Math.min(cap, k * Math.log2(1 + scale * f)));

function ago(ms) {
  const minutes = ms / 60000;
  if (minutes < 60) return `${minutes.toFixed(0)}m`;
  if (minutes < 48 * 60) return `${(minutes / 60).toFixed(1)}h`;
  return `${(minutes / 1440).toFixed(1)}d`;
}

const path = process.argv[2] ?? join(appData(), 'pond', 'Rudder', 'frecency.json');
const store = JSON.parse(readFileSync(path, 'utf8'));
const now = Date.now();

const rows = Object.entries(store)
  .map(([key, { score, at }]) => ({ key, f: decayed(score, at, now), ago: now - at }))
  .sort((a, b) => b.f - a.f);

console.log(`${path}`);
console.log(`half-life ${ago(halfLifeMs)}, bonus = min(${cap}, ${k.toFixed(3)} * log2(1 + ${scale} f)),`
  + ` cap at f = ${((2 ** (cap / k) - 1) / scale).toFixed(0)}\n`);
console.log(`${'key'.padEnd(56)} ${'f'.padStart(8)} ${'bonus'.padStart(6)} ${'used'.padStart(7)}`);

for (const row of rows)
  console.log(`${row.key.padEnd(56)} ${row.f.toFixed(2).padStart(8)} `
    + `${bonus(row.f).toFixed(2).padStart(6)} ${ago(row.ago).padStart(7)}`);

const atCap = rows.filter((row) => bonus(row.f) >= cap).length;
const stable = rows.filter((row) => /\/(branch|path):/.test(row.key)).length;
console.log(`\n${rows.length} keys, ${atCap} at the cap, ${stable} on a stable branch:/path: key`);
