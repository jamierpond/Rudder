// Build, then launch Rudder.exe (multi-config VS layout, single-config fallback).
import { existsSync } from 'node:fs';
import { join } from 'node:path';
import { BUILD_DIR, ROOT, main, run, runNode } from './lib.mjs';

await main(async () => {
  await runNode(join(ROOT, 'Scripts', 'compile-windows.mjs'));

  const candidates = [join(BUILD_DIR, 'Debug', 'Rudder.exe'), join(BUILD_DIR, 'Rudder.exe')];
  const exe = candidates.find(existsSync);
  if (!exe) throw new Error(`Rudder.exe not found in ${BUILD_DIR}`);

  await run(exe, [], { detached: true });
});
