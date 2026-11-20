// Prints the live frecency store: each key's usage decayed to now (what
// Rudder sorts rows of an equal match by) and how long ago it was last picked
// or held. The half-life is read out of the C++ header. `just frecency-report [path/to/frecency.json]`.
import { existsSync, readFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';
import { BUILD_DIR, RELEASE_BUILD_DIR, ROOT } from './lib.mjs';

function appData() {
  if (process.platform === 'darwin') return join(homedir(), 'Library', 'Application Support');
  if (process.platform === 'win32') return process.env.APPDATA;
  return process.env.XDG_DATA_HOME ?? join(homedir(), '.local', 'share');
}

// The header lives in PondDev, whose checkout a configured build's CMake cache names.
function findPondDevLibraryFolder() {
  for (const buildDirectory of [BUILD_DIR, RELEASE_BUILD_DIR, join(ROOT, 'build-test')]) {
    const cache = join(buildDirectory, 'CMakeCache.txt');
    const match = existsSync(cache) && readFileSync(cache, 'utf8').match(/^PondDev_SOURCE_DIR:\w+=(.*)$/m);
    if (match) return join(match[1], 'Lib');
  }
  throw new Error('PondDev not found: configure Rudder first (just build)');
}

// `constexpr ... name = <expr>;` from a header, evaluated: plain arithmetic
// on numeric literals only (suffixes like LL dropped).
function constant(header, name) {
  const source = readFileSync(join(findPondDevLibraryFolder(), header), 'utf8');
  const match = source.match(new RegExp(`\\b${name}\\s*=\\s*([^;]+);`));
  if (!match) throw new Error(`${name} not found in ${header}`);
  const expr = match[1].replace(/(\d)(LL|ULL|L|U|f)\b/g, '$1');
  if (!/^[\d\s.+\-*/()]+$/.test(expr)) throw new Error(`${name}: can't evaluate '${expr}'`);
  return Function(`return (${expr});`)();
}

const halfLifeMs = constant('FrecencySearch/LearnedPicks.h', 'usageHalfLifeMs');

const decayed = (score, at, now) => score * 2 ** (-Math.max(now - at, 0) / halfLifeMs);

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
console.log(`half-life ${ago(halfLifeMs)}\n`);
console.log(`${'key'.padEnd(56)} ${'f'.padStart(8)} ${'used'.padStart(7)}`);

for (const row of rows)
  console.log(`${row.key.padEnd(56)} ${row.f.toFixed(2).padStart(8)} ${ago(row.ago).padStart(7)}`);

const stable = rows.filter((row) => /\/(branch|path):/.test(row.key)).length;
console.log(`\n${rows.length} keys, ${stable} on a stable branch:/path: key`);
