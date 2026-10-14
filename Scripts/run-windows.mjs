// Build, then launch Rudder.exe (multi-config VS layout, single-config
// fallback). `--relaunch` restarts the current build instead, no compile.
//
// A running Rudder goes first: the linker can't replace an exe that is
// running, and a second Rudder would only hand the running one a reopen and
// exit. Rudder is started detached, so this returns once it is up; with
// RUDDER_LOG set, its stderr goes to build/rudder.log.
import { spawn } from 'node:child_process';
import { existsSync, openSync } from 'node:fs';
import { join } from 'node:path';
import { BUILD_DIR, ROOT, main, run, runNode } from './lib.mjs';

await main(async () => {
  const relaunch = process.argv.includes('--relaunch');

  await run('taskkill', ['/IM', 'Rudder.exe', '/F'], { allowFail: true, stdio: 'ignore' });

  if (!relaunch)
    await runNode(join(ROOT, 'Scripts', 'compile-windows.mjs'));

  const candidates = [join(BUILD_DIR, 'Debug', 'Rudder.exe'), join(BUILD_DIR, 'Rudder.exe')];
  const exe = candidates.find(existsSync);
  if (!exe) throw new Error(`Rudder.exe not found in ${BUILD_DIR}`);

  let stderr = 'ignore';

  if (process.env.RUDDER_LOG) {
    const log = join(BUILD_DIR, 'rudder.log');
    console.log(`[run] Rudder's log: ${log}`);
    stderr = openSync(log, 'w');
  }

  spawn(exe, [], { cwd: ROOT, detached: true, stdio: ['ignore', 'ignore', stderr] }).unref();
});
