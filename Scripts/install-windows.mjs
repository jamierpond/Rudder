// Build Release + install to %LOCALAPPDATA%\Programs\Rudder\Rudder.exe, the
// per-user programs folder, with a Start Menu shortcut. No Desktop one, unlike
// CowTerm: Rudder is an agent app with no window to open from it.
//
// A running Rudder is quit first, as run-windows.mjs does. Windows can rename
// an exe that is still mapped but not overwrite it, so a copy that fails
// moves the old exe aside to Rudder.exe.old (swept on the next install) and
// tries again. The installed Rudder is then launched: it registers itself to
// start at login against its own path, which takes the Run key off whichever
// build last claimed it.
//
// The Release tree is always reconfigured, so CPM refetches branch-pinned
// packages (eacp@main) and the install has the latest of them.
import { spawn } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, renameSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { RELEASE_BUILD_DIR, cpmSourceArgs, main, run } from './lib.mjs';

const DIR = join(process.env.LOCALAPPDATA, 'Programs', 'Rudder');
const EXE = join(DIR, 'Rudder.exe');
const OLD = `${EXE}.old`;

function install(built) {
  mkdirSync(DIR, { recursive: true });

  if (existsSync(OLD))
    rmSync(OLD, { force: true, maxRetries: 3 });

  try {
    copyFileSync(built, EXE);
  } catch {
    renameSync(EXE, OLD);
    copyFileSync(built, EXE);
  }
}

// Through WScript.Shell, which is what writes a .lnk from a script.
function startMenuShortcut() {
  const script = `
    $exe = '${EXE}'
    $shell = New-Object -ComObject WScript.Shell
    $folder = [Environment]::GetFolderPath('Programs')
    $link = $shell.CreateShortcut((Join-Path $folder 'Rudder.lnk'))
    $link.TargetPath = $exe
    $link.WorkingDirectory = (Split-Path $exe)
    $link.IconLocation = $exe
    $link.Description = 'Rudder'
    $link.Save()`;

  // No shell between: cmd would split the script at its newlines.
  return run('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', script],
    { shell: false });
}

await main(async () => {
  await run('cmake', ['-B', RELEASE_BUILD_DIR, ...cpmSourceArgs()]);
  await run('cmake', ['--build', RELEASE_BUILD_DIR, '--config', 'Release', '--target', 'Rudder']);

  await run('taskkill', ['/IM', 'Rudder.exe', '/F'], { allowFail: true, stdio: 'ignore' });

  install(join(RELEASE_BUILD_DIR, 'Release', 'Rudder.exe'));
  await startMenuShortcut();
  console.log('Added a Start Menu shortcut');

  // The shell caches icons; this makes the new shortcut show Rudder's.
  await run('ie4uinit.exe', ['-show'], { allowFail: true });

  spawn(EXE, [], { cwd: DIR, detached: true, stdio: 'ignore' }).unref();
  console.log(`Installed ${EXE} and started it`);
});
