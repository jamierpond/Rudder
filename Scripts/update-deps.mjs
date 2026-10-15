// Refetch branch-pinned CPM packages (eacp@main, emberstore@main, ...) in
// every configured build tree. Re-running configure is what makes
// FetchContent's update step fetch and check out the branch's new head;
// nothing is built, so a running Rudder isn't touched. The next `just build`
// / `just run` / `just install` compiles it.
import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { ROOT, cpmSourceArgs, main, run } from './lib.mjs';

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
    await run('cmake', ['-B', join(ROOT, tree), ...cpmSourceArgs()]);
  }
});
