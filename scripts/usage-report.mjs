// Jamie's usage of his own apps, from their session logs: per-app feature
// counts over a window, features the source can log but nobody used, how
// often each picker is closed without a pick, and the slowest picks.
//
//   just usage-report [--days 7] [--slow-ms 3000] [--logs App=/path/to/logs] [--source App=/path/to/repo]
//
// Log folders are found under the app-data folder (pond/<App>/logs holding
// <digits>.log files); source repos at ~/projects/<App>.
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';
import { countFeatures, findLoggedEventNames, measurePickers, parseSessionLog } from './usage-log.mjs';

const millisecondsPerDay = 24 * 60 * 60 * 1000;
const defaultDays = 7;
const defaultSlowPickMs = 3000;
const slowPicksToShow = 10;
const sourceExtensions = /\.(cpp|h|mm|hpp)$/;
const skippedSourceFolders = new Set(['.git', 'node_modules', '.cache']);

function readOptions(argv) {
  const options = { days: defaultDays, slowMs: defaultSlowPickMs, logs: {}, sources: {} };
  for (let index = 0; index < argv.length; index += 2) {
    const [flag, value] = [argv[index], argv[index + 1]];
    if (flag === '--days') options.days = +value;
    else if (flag === '--slow-ms') options.slowMs = +value;
    else if (flag === '--logs' || flag === '--source') {
      const [app, path] = value.split('=');
      options[flag === '--logs' ? 'logs' : 'sources'][app] = path;
    } else throw new Error(`unknown option ${flag}`);
  }
  return options;
}

function getAppDataDirectory() {
  if (process.platform === 'darwin') return join(homedir(), 'Library', 'Application Support');
  if (process.platform === 'win32') return process.env.APPDATA;
  return process.env.XDG_DATA_HOME ?? join(homedir(), '.local', 'share');
}

const isSessionLogName = (name) => /^\d+\.log$/.test(name);

function listDirectories(path) {
  try {
    return readdirSync(path).filter((name) => statSync(join(path, name)).isDirectory());
  } catch {
    return [];
  }
}

function hasSessionLogs(path) {
  try {
    return readdirSync(path).some(isSessionLogName);
  } catch {
    return false;
  }
}

function findLogDirectories() {
  const found = {};
  const root = getAppDataDirectory();
  const candidates = listDirectories(join(root, 'pond')).map((app) => [app, join(root, 'pond', app, 'logs')]);
  for (const [app, path] of candidates)
    if (!app.endsWith('Snapshot') && hasSessionLogs(path))
      found[app] = path;
  return found;
}

function readEvents(logsDirectory, sinceMs) {
  const events = [];
  for (const name of readdirSync(logsDirectory).filter(isSessionLogName)) {
    const sessionStartMs = +name.slice(0, -'.log'.length);
    const text = readFileSync(join(logsDirectory, name), 'utf8');
    for (const event of parseSessionLog(text, sessionStartMs))
      if (event.atMs >= sinceMs) events.push(event);
  }
  return events.sort((a, b) => a.atMs - b.atMs);
}

function readSourceText(directory) {
  let text = '';
  for (const name of readdirSync(directory)) {
    if (skippedSourceFolders.has(name) || name.startsWith('build')) continue;
    const path = join(directory, name);
    if (statSync(path).isDirectory()) text += readSourceText(path);
    else if (sourceExtensions.test(name)) text += readFileSync(path, 'utf8') + '\n';
  }
  return text;
}

function findEventCatalogue(app, options) {
  const repo = options.sources[app] ?? join(homedir(), 'projects', app);
  return existsSync(repo) ? findLoggedEventNames(readSourceText(repo)) : new Set();
}

const percent = (fraction) => `${Math.round(fraction * 100)}%`;

function printApp(app, events, catalogue, options) {
  console.log(`\n== ${app}: ${events.length} events, ${events.filter((e) => e.name === 'app.launch').length} launches`);

  const counts = [...countFeatures(events)].sort((a, b) => b[1] - a[1]);
  for (const [feature, count] of counts) console.log(`  ${String(count).padStart(6)}  ${feature}`);

  const neverUsed = [...catalogue].filter((name) => !counts.some(([feature]) => feature === name)).sort();
  if (neverUsed.length > 0) console.log(`  never used (${neverUsed.length}): ${neverUsed.join(', ')}`);

  for (const [prefix, picker] of Object.entries(measurePickers(events))) {
    console.log(`  ${prefix}: ${picker.opens} opens, ${picker.picks} picks, ${picker.cancels} abandoned (${percent(picker.abandonRate)})`);
    const slow = picker.picksTimed.filter((pick) => pick.ms >= options.slowMs)
      .sort((a, b) => b.ms - a.ms).slice(0, slowPicksToShow);
    for (const pick of slow) {
      const what = pick.event.fields.title ?? pick.event.fields.url ?? '';
      console.log(`    slow ${Math.round(pick.ms)} ms  ${pick.event.name}  query="${pick.query ?? ''}"  ${what}`);
    }
  }
}

const options = readOptions(process.argv.slice(2));
const sinceMs = Date.now() - options.days * millisecondsPerDay;
const logDirectories = { ...findLogDirectories(), ...options.logs };

console.log(`Usage over the last ${options.days} days (slow pick: ${options.slowMs} ms or more)`);
for (const [app, directory] of Object.entries(logDirectories).sort())
  printApp(app, readEvents(directory, sinceMs), findEventCatalogue(app, options), options);
