// Build Release + install to ~/.local/libexec/rudder/Rudder.app, with a
// ~/.local/bin/rudder launcher script that opens it through LaunchServices.
//
// The new bundle is copied next to the old one first, then swapped in by
// rename, so the install dir never holds a half-copied Rudder. A running
// installed Rudder is asked to quit first; dev builds are left alone.
// LaunchServices is pointed at the installed copy and told to forget the
// build*/Rudder.app bundles. The installed Rudder is then opened, so it
// registers itself to start at login from there. A Rudder left in
// /Applications by older installs is quit, unregistered and removed.
//
// The bundle is signed with a self-signed "Rudder Local Signing" identity,
// made once in its own keychain and reused: the designated requirement then
// stays the same across installs, so the Accessibility grant survives. It's
// reset only when the requirement changes (first install, a recreated
// certificate, an ad-hoc copy installed before). No Apple certificate needed.
//
// The Release tree is always reconfigured, so CPM refetches branch-pinned
// packages (eacp, emberstore) and the install has the latest of them.
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readdirSync, rmSync } from 'node:fs';
import { rm, rename, writeFile } from 'node:fs/promises';
import { homedir, tmpdir } from 'node:os';
import { join } from 'node:path';
import {
  RELEASE_BUILD_DIR,
  ROOT,
  RUDDER,
  cpmSourceArgs,
  main,
  resetDevPrivacyGrants,
  run,
} from './lib.mjs';

const LOCAL_DIRECTORY = join(homedir(), '.local');
const INSTALL_DIRECTORY = join(LOCAL_DIRECTORY, 'libexec', 'rudder');
const COMMAND_DIRECTORY = join(LOCAL_DIRECTORY, 'bin');
const APP = join(INSTALL_DIRECTORY, 'Rudder.app');
const STAGING = join(INSTALL_DIRECTORY, '.Rudder.app.installing');
const OLD = join(INSTALL_DIRECTORY, '.Rudder.app.old');
const COMMAND_LAUNCHER = join(COMMAND_DIRECTORY, 'rudder');
const LEGACY_APPLICATIONS_APP = '/Applications/Rudder.app';
const LSREGISTER =
  '/System/Library/Frameworks/CoreServices.framework/Frameworks/' +
  'LaunchServices.framework/Support/lsregister';

const IDENTITY = 'Rudder Local Signing';
const KEYCHAIN = join(homedir(), 'Library', 'Keychains', 'rudder-signing.keychain-db');
const KEYCHAIN_PASSWORD = 'rudder-signing-local';

const capture = (command, args) =>
  execFileSync(command, args, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });

function designatedRequirement(app) {
  if (!existsSync(app)) return null;
  try {
    return capture('codesign', ['-d', '-r', '-', app]).trim();
  } catch {
    // Unsigned, or not a bundle codesign can read.
    return null;
  }
}

function identityHash() {
  try {
    const out = capture('security', ['find-identity', '-p', 'codesigning', KEYCHAIN]);
    return out.match(new RegExp(`([0-9A-F]{40}) "${IDENTITY}"`))?.[1] ?? null;
  } catch {
    return null;
  }
}

async function ensureKeychain() {
  if (!existsSync(KEYCHAIN))
    await run('security', ['create-keychain', '-p', KEYCHAIN_PASSWORD, KEYCHAIN]);

  await run('security', ['unlock-keychain', '-p', KEYCHAIN_PASSWORD, KEYCHAIN]);
  // No timeout, no lock on sleep.
  await run('security', ['set-keychain-settings', KEYCHAIN]);

  const searchList = capture('security', ['list-keychains', '-d', 'user'])
    .split('\n')
    .map((line) => line.trim().replace(/^"|"$/g, ''))
    .filter(Boolean);

  if (!searchList.includes(KEYCHAIN))
    await run('security', ['list-keychains', '-d', 'user', '-s', ...searchList, KEYCHAIN]);
}

async function createIdentity() {
  console.log(`[install] creating the "${IDENTITY}" signing identity`);
  const dir = mkdtempSync(join(tmpdir(), 'rudder-signing-'));

  try {
    const key = join(dir, 'key.pem');
    const cert = join(dir, 'cert.pem');
    const p12 = join(dir, 'identity.p12');

    // /usr/bin/openssl is LibreSSL: its .p12 is one `security import` reads
    // (OpenSSL 3's default ciphers need -legacy for that).
    await run('/usr/bin/openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-nodes',
      '-keyout', key, '-out', cert, '-days', '3650', '-subj', `/CN=${IDENTITY}`,
      '-addext', 'basicConstraints=critical,CA:false',
      '-addext', 'keyUsage=critical,digitalSignature',
      '-addext', 'extendedKeyUsage=critical,codeSigning']);
    await run('/usr/bin/openssl', ['pkcs12', '-export', '-inkey', key, '-in', cert,
      '-name', IDENTITY, '-out', p12, '-passout', `pass:${KEYCHAIN_PASSWORD}`]);

    await run('security', ['import', p12, '-k', KEYCHAIN, '-P', KEYCHAIN_PASSWORD,
      '-T', '/usr/bin/codesign']);
    // Let codesign use the key without a prompt.
    await run('security', ['set-key-partition-list', '-S', 'apple-tool:,apple:,codesign:',
      '-s', '-k', KEYCHAIN_PASSWORD, KEYCHAIN], { stdio: ['inherit', 'ignore', 'inherit'] });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

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

async function removeLegacyApplicationsInstall() {
  if (!existsSync(LEGACY_APPLICATIONS_APP))
    return;

  console.log(`[install] removing the old ${LEGACY_APPLICATIONS_APP}`);
  await quitInstalled(LEGACY_APPLICATIONS_APP);
  await run(LSREGISTER, ['-u', LEGACY_APPLICATIONS_APP], { stdio: 'ignore' }).catch(() => {});
  await rm(LEGACY_APPLICATIONS_APP, { recursive: true, force: true });
}

const COMMAND_LAUNCHER_FILE_MODE = 0o755;

// A plain launch goes through LaunchServices, not the bundle's executable, so
// macOS makes the app (not the terminal) the responsible process for TCC
// prompts and grants. Any argument execs the executable directly instead:
// argv and the terminal's stdout/stderr reach it, as before.
function describeCommandLauncherScript(appBundlePath) {
  const appExecutablePath = join(appBundlePath, 'Contents', 'MacOS', 'Rudder');
  return [
    '#!/bin/sh',
    `[ "$#" -gt 0 ] && exec ${JSON.stringify(appExecutablePath)} "$@"`,
    `exec open -a ${JSON.stringify(appBundlePath)}`,
    '',
  ].join('\n');
}

async function writeCommandLauncher(launcherPath, appBundlePath) {
  await rm(launcherPath, { force: true });
  await writeFile(launcherPath, describeCommandLauncherScript(appBundlePath),
    { mode: COMMAND_LAUNCHER_FILE_MODE });
}

await main(async () => {
  mkdirSync(INSTALL_DIRECTORY, { recursive: true });
  mkdirSync(COMMAND_DIRECTORY, { recursive: true });

  await run('cmake', [
    '-G', 'Ninja', '-B', RELEASE_BUILD_DIR, '-DCMAKE_BUILD_TYPE=Release',
    ...cpmSourceArgs(),
  ]);

  await run('cmake', ['--build', RELEASE_BUILD_DIR, '--target', 'Rudder']);

  await rm(STAGING, { recursive: true, force: true });
  await run('ditto', [join(RELEASE_BUILD_DIR, 'Rudder.app'), STAGING]);

  await ensureKeychain();
  if (!identityHash()) await createIdentity();
  const hash = identityHash();
  if (!hash) throw new Error(`"${IDENTITY}" isn't usable for code signing in ${KEYCHAIN}`);

  // Seal the whole bundle (Info.plist, icon), not just the executable the
  // linker signed, with the local identity.
  console.log(`[install] signing with "${IDENTITY}"`);
  await run('codesign', ['--force', '--deep', '--options', 'runtime', '--timestamp=none',
    '--keychain', KEYCHAIN, '--sign', hash, STAGING]);
  await run('codesign', ['--verify', '--deep', '--strict', STAGING]);

  const before = designatedRequirement(APP);
  const after = designatedRequirement(STAGING);

  await quitInstalled();
  await removeLegacyApplicationsInstall();

  await rm(OLD, { recursive: true, force: true });

  if (existsSync(APP))
    await rename(APP, OLD);

  await rename(STAGING, APP);
  await rm(OLD, { recursive: true, force: true });

  if (before !== after) {
    console.log('[install] signing identity changed');
    await resetDevPrivacyGrants(RUDDER);
  } else {
    console.log('[install] same signing identity: keeping the Accessibility grant');
  }

  await writeCommandLauncher(COMMAND_LAUNCHER, APP);

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
