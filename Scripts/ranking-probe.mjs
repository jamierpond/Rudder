// Copies Rudder's data folder (never opened in place: Rudder holds it) and
// replays the queries against its saved panel rows with RankingProbe.
import { cpSync, mkdtempSync } from 'node:fs';
import { homedir, tmpdir } from 'node:os';
import { join } from 'node:path';
import { ROOT, main, run } from './lib.mjs';

const TEST_DIR = join(ROOT, 'build-test');
const DATA_DIR = join(homedir(), 'Library', 'Application Support', 'pond', 'Rudder');

await main(async () => {
  await run('cmake', ['--build', TEST_DIR, '--target', 'RankingProbe']);
  const copy = mkdtempSync(join(tmpdir(), 'rudder-probe-'));
  cpSync(DATA_DIR, copy, { recursive: true, filter: (path) => !path.endsWith('.sock') });
  await run(join(TEST_DIR, 'Tests', 'RankingProbe'), [copy, ...process.argv.slice(2)]);
});
