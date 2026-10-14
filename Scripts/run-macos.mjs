// Build, then launch Rudder.app. `--relaunch` restarts the current build
// instead: no compile and no privacy reset, since the binary (and with it the
// Accessibility grant) is unchanged.
//
// RUDDER_* variables in the caller's environment reach the app (`open` would
// drop them), e.g. RUDDER_LOG=1 or the RUDDER_DEBUG_* hooks; with RUDDER_LOG
// set, its stderr goes to build/rudder.log.
import { join } from 'node:path';
import {
  BUILD_DIR,
  ROOT,
  RUDDER,
  main,
  resetDevPrivacyGrants,
  run,
  runNode,
} from './lib.mjs';

await main(async () => {
  const relaunch = process.argv.includes('--relaunch');

  if (!relaunch)
    await runNode(join(ROOT, 'Scripts', 'compile-macos.mjs'));

  // A running Rudder would just be reactivated by `open`, old binary and all.
  await run('pkill', ['-x', 'Rudder'], { allowFail: true });

  if (!relaunch)
    await resetDevPrivacyGrants(RUDDER);

  const args = [];

  for (const [name, value] of Object.entries(process.env))
    if (name.startsWith('RUDDER_'))
      args.push('--env', `${name}=${value}`);

  if (process.env.RUDDER_LOG) {
    const log = join(BUILD_DIR, 'rudder.log');
    console.log(`[run] Rudder's log: ${log}`);
    args.push('--stderr', log);
  }

  await run('open', [...args, join(BUILD_DIR, 'Rudder.app')]);
});
