import { test } from 'node:test';
import assert from 'node:assert/strict';
import { countFeatures, findLoggedEventNames, measurePickers, parseFields, parseSessionLog } from './usage-log.mjs';

const sessionStartMs = new Date(2026, 10, 14, 23, 59, 0).getTime();

test('fields: plain and quoted values, escapes undone', () => {
  assert.deepEqual(parseFields('via=cmd-l title="Sign in \\"now\\"" text="a\\nb" query=""'),
    { via: 'cmd-l', title: 'Sign in "now"', text: 'a\nb', query: '' });
});

test('lines from Grazer (|) and Rudder ([rudder n]) both parse; prose lines are skipped', () => {
  const events = parseSessionLog([
    '23:59:01.000 | palette.open via=cmd-l',
    '23:59:02.000 [rudder 123] switcher.pick ms=40',
    '23:59:03.000 | launch for a snapshot',
  ].join('\n'), sessionStartMs);

  assert.deepEqual(events.map((e) => e.name), ['palette.open', 'switcher.pick']);
  assert.equal(events[1].fields.ms, '40');
});

test('a time earlier than the last line is the next day', () => {
  const events = parseSessionLog('23:59:59.000 | a.b\n00:00:01.000 | a.c', sessionStartMs);
  assert.equal(events[1].atMs - events[0].atMs, 2000);
});

test('pickers: only prefixes with picks or cancels; picks, cancels, an open never closed, and pick time from the open', () => {
  const events = parseSessionLog([
    '10:00:00.000 | palette.open via=cmd-l',
    '10:00:00.100 | tab.open tab=3',
    '10:00:01.000 | palette.query text=hn',
    '10:00:02.500 | palette.pick.enter kind=tab',
    '10:01:00.000 | palette.open via=cmd-t',
    '10:01:01.000 | palette.cancel via=esc',
    '10:02:00.000 | palette.open via=cmd-l',
  ].join('\n'), sessionStartMs);

  const pickers = measurePickers(events);
  assert.deepEqual(Object.keys(pickers), ['palette']);
  const palette = pickers.palette;
  assert.equal(palette.picksTimed[0].query, 'hn');
  assert.equal(palette.opens, 3);
  assert.equal(palette.picks, 1);
  assert.equal(palette.cancels, 2);
  assert.equal(palette.picksTimed[0].ms, 2500);
  assert.ok(Math.abs(palette.abandonRate - 2 / 3) < 1e-9);
});

test('features count each event and each event+via', () => {
  const counts = countFeatures(parseSessionLog('10:00:00.000 | palette.open via=cmd-l\n10:00:01.000 | palette.open via=t', sessionStartMs));
  assert.equal(counts.get('palette.open'), 2);
  assert.equal(counts.get('palette.open via=cmd-l'), 1);
});

test('the event catalogue comes from logging calls in source, not file names', () => {
  const names = findLoggedEventNames([
    'logEvent("tab.close", {{"reason", reason}});',
    'logPaletteRowEvent(inNewTab ? "palette.pick.cmd-enter" : "palette.pick.enter", *row);',
    'logEvent("panel-frame.save-fail", {{"path", "rudder.json"}});',
    'auto path = "notes.txt";',
  ].join('\n'));
  assert.deepEqual([...names].sort(), ['palette.pick.cmd-enter', 'palette.pick.enter', 'panel-frame.save-fail', 'tab.close']);
});
