# Rudder

A macOS fzf-style app switcher. Hit **⌥Space**, type, **Enter**. The panel
appears instantly over everything, filters running apps (and their windows,
and installed apps that aren't running) as you type, and brings the selection
to the front behind it while you type. **⌥`** jumps back to where you were
before, and keeps walking back on each press.

Drawn with eacp's GPU component tier (one `ComponentHost`), no web view.

## Build and run

```sh
just build   # configure + build (CMake/Ninja, deps via CPM)
just run     # build, quit any running Rudder, reset its dev Accessibility grant, open it
just test    # unit tests
```

Rudder is an agent app: no Dock icon, a menu-bar icon (Open, Settings…, Quit),
and it registers itself to start at login.

## Permissions

Accessibility, for the live preview, window rows and the keyboard while the
panel is up. Without it the panel shows an unlock overlay: drag the Rudder
icon into System Settings › Privacy & Security › Accessibility and switch it
on. Every dev build is ad-hoc signed with a new cdhash, so `just run` resets
the grant first.

## Keys

| | |
|---|---|
| ⌥Space | open / close the panel (rebindable) |
| ⌥\` | go back (rebindable) |
| ↑ ↓, ⌃N ⌃P | move |
| Enter | switch (launches a closed app, focuses a window row) |
| Esc | close and put the screen back |
| ⌃Q | quit the selected app (rebindable) |
| ⌃W ⌃U ⌃K ⌃A ⌃E ⌃B ⌃F ⌃H ⌃D, ⌥B ⌥F ⌥⌫ | readline editing |

## Debugging

- `RUDDER_LOG=1` traces the show/hide handshake, previews, the back stack and
  latency (hotkey → first frame, key → frame) on stderr.
- `Rudder --snapshot <dir>` renders the panel and the unlock overlay to PNGs.
