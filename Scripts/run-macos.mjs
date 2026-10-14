// Build, then launch Rudder.app.
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
  await runNode(join(ROOT, 'Scripts', 'compile-macos.mjs'));
  await resetDevPrivacyGrants(RUDDER);
  await run('open', [join(BUILD_DIR, 'Rudder.app')]);
});
