// Reads the session logs every pond app writes through Rudder's SessionLog
// library: <logs>/<startEpochMs>.log, one `<HH:MM:SS.mmm> <prefix> <event> key=value ...`
// line per event. Pure functions, so the report and its tests share them.

const timePattern = /^(\d\d):(\d\d):(\d\d)\.(\d{3}) (?:\| |\[[^\]]*\] )?(.*)$/;
const eventNamePattern = /^[a-z][a-z0-9-]*(\.[a-z0-9-]+)+$/;
const millisecondsPerDay = 24 * 60 * 60 * 1000;

// A field value is a run of characters up to a space, or a double-quoted
// string with \" \\ \n escapes. Each returns the value and where it ends.
const quotedValuePattern = /^"((?:[^"\\]|\\.)*)"/;
const unescapeQuoted = (text) => text.replace(/\\(.)/g, (_, c) => (c === 'n' ? '\n' : c));

function readValue(text, start) {
  const quoted = text.slice(start).match(quotedValuePattern);
  if (quoted) return { value: unescapeQuoted(quoted[1]), end: start + quoted[0].length };

  const space = text.indexOf(' ', start);
  const end = space < 0 ? text.length : space;
  return { value: text.slice(start, end), end };
}

// `key=value key="quoted \" value"` into an object; the inverse of formatEventLine.
export function parseFields(text) {
  const fields = {};
  let index = 0;

  for (;;) {
    while (text[index] === ' ') index++;
    const equals = text.indexOf('=', index);
    if (equals < 0) return fields;

    const { value, end } = readValue(text, equals + 1);
    fields[text.slice(index, equals)] = value;
    index = end;
  }
}

// The events of one session log. Lines carry only a time of day, so each is
// placed on the session's start day, moving a day on whenever the clock wraps.
export function parseSessionLog(text, sessionStartMs) {
  const start = new Date(sessionStartMs);
  let dayStartMs = new Date(start.getFullYear(), start.getMonth(), start.getDate()).getTime();
  let previousTimeOfDayMs = -1;
  const events = [];

  for (const line of text.split('\n')) {
    const match = line.match(timePattern);
    if (!match) continue;

    const [, hours, minutes, seconds, millis, rest] = match;
    const timeOfDayMs = ((+hours * 60 + +minutes) * 60 + +seconds) * 1000 + +millis;
    if (timeOfDayMs < previousTimeOfDayMs) dayStartMs += millisecondsPerDay;
    previousTimeOfDayMs = timeOfDayMs;

    const space = rest.indexOf(' ');
    const name = space < 0 ? rest : rest.slice(0, space);
    if (!eventNamePattern.test(name)) continue;

    events.push({
      atMs: dayStartMs + timeOfDayMs,
      name,
      fields: space < 0 ? {} : parseFields(rest.slice(space + 1)),
    });
  }

  return events;
}

// Pickers are event prefixes with a `.pick*` or `.cancel` (palette, switcher).
// Each `.open` ends at the next pick or cancel; an open followed by another
// open, or by nothing, counts as abandoned too.
function findPickerPrefixes(events) {
  return new Set(events
    .filter((event) => /\.(pick|cancel)(\.|$)/.test(event.name))
    .map((event) => event.name.slice(0, event.name.indexOf('.'))));
}

function getPickerAction(action) {
  if (action === 'pick' || action.startsWith('pick.')) return 'pick';
  return action;
}

class PickerTally {
  opens = 0;
  picks = 0;
  cancels = 0;
  picksTimed = [];
  pendingOpen = undefined;
  lastQuery = undefined;

  open(event) {
    if (this.pendingOpen) this.cancels++;
    this.opens++;
    this.pendingOpen = event;
    this.lastQuery = event.fields.query;
  }

  query(event) {
    this.lastQuery = event.fields.text;
  }

  pick(event) {
    this.picks++;
    const ms = this.measurePickMs(event);
    if (ms !== undefined)
      this.picksTimed.push({ ms, event, query: event.fields.query ?? this.lastQuery });
    this.pendingOpen = undefined;
  }

  cancel() {
    this.cancels++;
    this.pendingOpen = undefined;
  }

  measurePickMs(event) {
    if (event.fields.ms !== undefined) return +event.fields.ms;
    return this.pendingOpen ? event.atMs - this.pendingOpen.atMs : undefined;
  }

  finish() {
    if (this.pendingOpen) this.cancels++;
    this.abandonRate = this.cancels / Math.max(this.picks + this.cancels, 1);
    return this;
  }
}

const pickerActions = new Set(['open', 'query', 'pick', 'cancel']);

function tallyPickerEvent(tallies, prefixes, event) {
  const dot = event.name.indexOf('.');
  const prefix = event.name.slice(0, dot);
  const action = getPickerAction(event.name.slice(dot + 1));
  if (prefixes.has(prefix) && pickerActions.has(action))
    (tallies[prefix] ??= new PickerTally())[action](event);
}

export function measurePickers(events) {
  const prefixes = findPickerPrefixes(events);
  const tallies = {};
  for (const event of events) tallyPickerEvent(tallies, prefixes, event);

  const opened = Object.entries(tallies).filter(([, tally]) => tally.opens > 0);
  return Object.fromEntries(opened.map(([prefix, tally]) => [prefix, tally.finish()]));
}

// "palette.open" and, when it has one, "palette.open via=cmd-l": a feature is
// an event, or an event reached a particular way.
export function countFeatures(events) {
  const counts = new Map();
  const add = (key) => counts.set(key, (counts.get(key) ?? 0) + 1);

  for (const event of events) {
    add(event.name);
    if (event.fields.via !== undefined) add(`${event.name} via=${event.fields.via}`);
  }

  return counts;
}

// Event names an app's source can log: dotted string literals on lines that
// call a logging function. The catalogue "never used" is measured against.
export function findLoggedEventNames(sourceText) {
  const names = new Set();

  for (const line of sourceText.split('\n')) {
    if (!/(Event|EventLine|logStack|logSelectedRow)\w*\s*\(/.test(line)
        && !/^\s*"[a-z][a-z0-9-]*\.[a-z0-9.-]+"\s*,?\s*$/.test(line)) continue;
    for (const match of line.matchAll(/"([^"\s]+)"/g))
      if (eventNamePattern.test(match[1]) && !/\.(json|log|txt|app|com|org)$/.test(match[1]))
        names.add(match[1]);
  }

  return names;
}
