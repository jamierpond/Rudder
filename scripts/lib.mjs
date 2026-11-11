import { spawn } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

export const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
export const BUILD_DIR = join(ROOT, 'build');
export const RELEASE_BUILD_DIR = join(ROOT, 'build-release');

// lockstep source overrides from the environment, for a fresh configure: export
// e.g. LOCKSTEP_eacp_SOURCE=$HOME/projects/eacp to build against a local checkout.
export function lockstepSourceArgs() {
  return Object.entries(process.env)
    .filter(([name]) => /^LOCKSTEP_\w+_SOURCE$/.test(name))
    .map(([name, value]) => `-D${name}=${value}`);
}

const PLATFORMS = { darwin: 'macos', win32: 'windows' };

// Resolve `scripts/<name>-<platform>.mjs` for the current platform.
export function platformScript(name) {
  const platform = PLATFORMS[process.platform];
  if (!platform) throw new Error(`Unsupported platform: ${process.platform}`);
  return join(ROOT, 'scripts', `${name}-${platform}.mjs`);
}

// cmd.exe hands a quoted argument to the program whole, spaces included.
const quoteForShell = (argument) => `"${String(argument).replace(/"/g, '\\"')}"`;

// Node deprecates passing args alongside shell: true (DEP0190), so a shell run
// is one command string, as tamber-web's scripts/lib/core.mjs does it.
function describeSpawn(command, args, shell) {
  if (!shell)
    return { target: command, args };

  return { target: [command, ...args.map(quoteForShell)].join(' '), args: [] };
}

// Run a command with inherited stdio, through cmd.exe on Windows. Rejects
// when it exits non-zero, unless `allowFail`.
export function run(command, args = [],
  { allowFail = false, shell = process.platform === 'win32', ...options } = {}) {
  const spawned = describeSpawn(command, args, shell);

  return new Promise((resolve, reject) => {
    const child = spawn(spawned.target, spawned.args, {
      cwd: ROOT,
      stdio: 'inherit',
      shell,
      ...options,
    });
    child.on('exit', (code) =>
      code === 0 || allowFail
        ? resolve()
        : reject(new Error(`${command} ${args.join(' ')} exited with ${code}`)),
    );
    child.on('error', reject);
  });
}

// Node itself, never through a shell: its path may hold spaces
// (C:\Program Files\nodejs\node.exe).
export function runNode(script, args = []) {
  return run(process.execPath, [script, ...args], { shell: false });
}

// Entry-point wrapper: run `fn`, exit non-zero on failure.
export async function main(fn) {
  try {
    await fn();
  } catch (error) {
    console.error(error.message);
    process.exit(1);
  }
}

// Rudder as the dev scripts see it: which privacy grants a dev launch resets.
export const RUDDER = {
  bundleId: 'com.pond.rudder',
  devResetsPrivacy: ['Accessibility'],
};

/** Every dev build is ad-hoc signed with a fresh cdhash, so macOS keeps the
 *  old privacy grant switched on in System Settings while refusing it to the
 *  new binary. Resetting before launch makes the app ask again for real. */
export async function resetDevPrivacyGrants(app) {
  if (process.platform !== 'darwin' || !app.bundleId) return;
  for (const service of app.devResetsPrivacy) {
    console.log(`[run] resetting ${service} for ${app.bundleId}`);
    await run('tccutil', ['reset', service, app.bundleId], { allowFail: true });
  }
}
