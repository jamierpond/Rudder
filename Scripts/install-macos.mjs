// Build Release + install to /Applications/Rudder.app, as Grazer's
// install-macos.mjs does.
//
// The new bundle is copied next to the old one first, then swapped in by
// rename, so /Applications never holds a half-copied Rudder. A running
// installed Rudder is asked to quit first; dev builds are left alone.
// LaunchServices is pointed at the installed copy and told to forget the
// build*/Rudder.app bundles. The installed Rudder is then opened, so it
// registers itself to start at login from /Applications.
//
// The bundle is ad-hoc signed, so each install is a new cdhash and macOS asks
// for the Accessibility grant again (drag the icon in from the unlock
// overlay).
//
// The Release tree is always reconfigured, so CPM refetches branch-pinned
// packages (eacp, emberstore) and the install has the latest of them.
import { execFileSync } from 'node:child_process';
import { existsSync, readdirSync } from 'node:fs';
import { rm, rename } from 'node:fs/promises';
import { join } from 'node:path';
import { RELEASE_BUILD_DIR, ROOT, cpmSourceArgs, main, run } from './lib.mjs';

const APP = '/Applications/Rudder.app';
const STAGING = '/Applications/.Rudder.app.installing';
const OLD = '/Applications/.Rudder.app.old';
const LSREGISTER =
  '/System/Library/Frameworks/CoreServices.framework/Frameworks/' +
  'LaunchServices.framework/Support/lsregister';

function bundleId(app) {
  return execFileSync('/usr/libexec/PlistBuddy',
    ['-c', 'Print :CFBundleIdentifier', join(app, 'Contents/Info.plist')],
    { encoding: 'utf8' }).trim();
}

// Gracefully quit (a quit Apple event, like Quit Rudder) whatever runs from
// `app`, found by its bundle id. Returns whether one was running.
async function quitInstalled(app = APP) {
  if (!existsSync(app))
    return false;

  const script = `
    ObjC.import('AppKit');
    ObjC.unwrap($.NSRunningApplication
      .runningApplicationsWithBundleIdentifier(${JSON.stringify(bundleId(app))}))
      .filter((app) => ObjC.unwrap(app.bundleURL.path) === ${JSON.stringify(app)})
      .map((app) => { app.terminate; return app.processIdentifier; })
      .join(' ');
  `;

  const pids = execFileSync('osascript', ['-l', 'JavaScript', '-e', script],
    { encoding: 'utf8' }).trim().split(' ').filter(Boolean).map(Number);

  // osascript has no run loop to see NSRunningApplication.terminated flip,
  // so wait on the pids here.
  const alive = (pid) => {
    try {
      process.kill(pid, 0);
      return true;
    } catch {
      return false;
    }
  };

  for (let i = 0; i < 100 && pids.some(alive); ++i)
    await new Promise((resolve) => setTimeout(resolve, 100));

  if (pids.some(alive))
    throw new Error(`${app} did not quit`);

  return pids.length > 0;
}

await main(async () => {
  await run('cmake', [
    '-G', 'Ninja', '-B', RELEASE_BUILD_DIR, '-DCMAKE_BUILD_TYPE=Release',
    ...cpmSourceArgs(),
  ]);

  await run('cmake', ['--build', RELEASE_BUILD_DIR, '--target', 'Rudder']);

  await rm(STAGING, { recursive: true, force: true });
  await run('ditto', [join(RELEASE_BUILD_DIR, 'Rudder.app'), STAGING]);

  // The linker's ad-hoc signature covers the executable only; seal the whole
  // bundle (Info.plist, icon) so it verifies and carries the bundle id. Still
  // ad-hoc: no identity, no keychain.
  await run('codesign', ['--force', '--sign', '-', STAGING]);
  await run('codesign', ['--verify', '--deep', '--strict', STAGING]);

  await quitInstalled();

  await rm(OLD, { recursive: true, force: true });

  if (existsSync(APP))
    await rename(APP, OLD);

  await rename(STAGING, APP);
  await rm(OLD, { recursive: true, force: true });

  await run(LSREGISTER, ['-f', APP]);

  for (const dir of readdirSync(ROOT).filter((name) => name.startsWith('build'))) {
    const dev = join(ROOT, dir, 'Rudder.app');

    // Fails (-10814) for a bundle LaunchServices never saw; that's fine.
    if (existsSync(dev))
      await run(LSREGISTER, ['-u', dev], { stdio: 'ignore' }).catch(() => {});
  }

  await run('open', [APP]);

  console.log(`Installed ${APP} (${bundleId(APP)})`);
});
