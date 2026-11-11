// Configure (if needed) + build Rudder.exe with CMake/Ninja. Run under
// scripts/with-msvc.ps1 (the justfile does), which brings in cl and link.
import { existsSync } from 'node:fs';
import { join } from 'node:path';
import { BUILD_DIR, main, run } from './lib.mjs';

await main(async () => {
  if (!existsSync(join(BUILD_DIR, 'build.ninja')))
    await run('cmake', ['-G', 'Ninja', '-B', BUILD_DIR, '-DCMAKE_BUILD_TYPE=Debug']);

  await run('cmake', ['--build', BUILD_DIR]);
});
