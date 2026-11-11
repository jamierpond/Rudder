// Build + run the unit tests in build-test/, never build/: rebuilding the app
// bundle in place revokes the running Rudder's Accessibility grant, so tests
// stay off it and can run while Rudder is live. Only the test target builds.
import { existsSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT, main, run } from './lib.mjs';

const TEST_DIR = join(ROOT, 'build-test');

await main(async () => {
  if (!existsSync(join(TEST_DIR, 'build.ninja')))
    await run('cmake', ['-G', 'Ninja', '-B', TEST_DIR, '-DCMAKE_BUILD_TYPE=Debug']);

  await run('cmake', ['--build', TEST_DIR, '--target', 'RudderTests']);
  await run('ctest', ['--test-dir', TEST_DIR, '--output-on-failure']);
});
