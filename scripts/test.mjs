// Build + run the unit tests in build-test/, never build/: rebuilding the app
// bundle in place revokes the running Rudder's Accessibility grant, so tests
// stay off it and can run while Rudder is live. Only the test target builds.
import { existsSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT, main, run } from './lib.mjs';

const TEST_DIR = join(ROOT, 'build-test');
const windows = process.platform === 'win32';

await main(async () => {
  if (!existsSync(join(TEST_DIR, 'CMakeCache.txt')))
    await run('cmake', windows
      ? ['-B', TEST_DIR]
      : ['-G', 'Ninja', '-B', TEST_DIR, '-DCMAKE_BUILD_TYPE=Debug']);

  await run('cmake', ['--build', TEST_DIR, '--target', 'RudderTests',
    ...(windows ? ['--config', 'Debug'] : [])]);

  await run(windows
    ? join(TEST_DIR, 'Tests', 'Debug', 'RudderTests.exe')
    : join(TEST_DIR, 'Tests', 'RudderTests'));
});
