// Move branch-pinned packages (eacp, emberstore, ...) to their branch heads:
// configure each build tree in lockstep's update mode, which rewrites
// lockstep.lock, then switch it back to locked mode. Nothing is built, so a running Rudder isn't touched. The next `just build`
// / `just run` / `just install` compiles it.
import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { ROOT, lockstepSourceArgs, main, run } from './lib.mjs';

await main(async () => {
  // Only trees configured from this checkout: a tree left over from before a
  // move (or from another worktree) won't reconfigure here. Compared as paths,
  // not text: on Windows CMake writes C:/... where ROOT reads C:\...
  const key = 'CMAKE_HOME_DIRECTORY:INTERNAL=';
  const canonical = (path) =>
    process.platform === 'win32' ? resolve(path).toLowerCase() : resolve(path);

  const ownTree = (name) => {
    const cache = join(ROOT, name, 'CMakeCache.txt');
    return existsSync(cache)
      && readFileSync(cache, 'utf8')
        .split(/\r?\n/)
        .some((line) => line.startsWith(key)
          && canonical(line.slice(key.length)) === canonical(ROOT));
  };

  const trees = readdirSync(ROOT)
    .filter((name) => name.startsWith('build') && ownTree(name));

  for (const tree of trees) {
    console.log(`[update-deps] reconfiguring ${tree}`);
    const buildTree = join(ROOT, tree);
    await run('cmake', ['-B', buildTree, '-DLOCKSTEP_MODE=update', ...lockstepSourceArgs()]);
    await run('cmake', ['-B', buildTree, '-DLOCKSTEP_MODE=locked']);
  }
});
