# Working in Rudder

Jamie runs the installed Rudder all day. A change only exists once it is
installed, so the dogfood cycle is the first rule of this repo.

## Ship every change at once

After any change: `just test`, then `git add -A`, commit, `git push origin
main`, `just install`. In that order, in the same turn, every time, without
being asked and without asking.

- Never ask what to commit or whether to commit. Commit everything in the
  tree. Work in progress goes in too, with "Work in progress:" and the
  failing test names in the message.
- Never ask whether to install. `just install` quits and relaunches the
  running Rudder; that is wanted. The older rule against relaunching apps on
  Jamie's Mac does not apply to Rudder, CowTerm or Grazer. It still bans
  synthetic input (CGEvents, AppleScript keystrokes, clicks) and
  screen-capture checks: Jamie tests the real app himself.
- A subagent that built something but did not install it has not finished.
  Put this file's rules in every subagent brief.
- A change to the Rudder client library (what CowTerm and Grazer fetch from
  GitHub main) means rebuilding and reinstalling those two as well.
- Never launch the raw bundle executable from a shell (TCC then blames the
  terminal). Use `open`, the `~/.local/bin/rudder` launcher or `just install`.

## Debugging

Rudder always logs to `~/Library/Logs/Rudder/rudder.log`. When something
misbehaves, read it first, before asking Jamie anything.

## Product rules decided by Jamie

- While the panel is up, Rudder keeps keyboard focus. A preview brings the
  selected window forward (the Accessibility raise action only) and never
  activates its app. Only Enter activates. (2026-11-03)

## Code style

Follow ~/.claude/CLAUDE.md: long verbose JUCE-style names, no magic numbers,
comments only for a non-obvious why, no AI attribution on commits.
