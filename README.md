<p align="center">
  <img src="Assets/Icon/Icon.png" width="160" alt="Rudder icon: a cow-spotted jet on a black tile">
</p>

<h1 align="center">Rudder</h1>

An fzf-style app switcher for macOS and Windows. Hit **⌥Space** (**Alt+Space**
on Windows), type, **Enter**. The panel appears instantly over everything,
filters running apps (and their windows, and installed apps that aren't
running) as you type, and brings the selection to the front behind it while
you type. **⌥`** (**Alt+`**) jumps back to where you were before, and keeps
walking back on each press.

Drawn with eacp's GPU component tier (one `ComponentHost`), no web view.

## Build and run

```sh
just build   # configure + build (macOS: Ninja, Windows: the default Visual Studio generator; deps via CPM)
just run     # build, quit any running Rudder (macOS: and reset its dev Accessibility grant), open it
just test    # unit tests
just install # Release build into /Applications/Rudder.app (macOS) or
             # %LOCALAPPDATA%\Programs\Rudder with a Start Menu shortcut
             # (Windows), then start it so it starts at login from there
just update-deps  # refetch eacp and the other branch-pinned deps
```

`just install` needs no Apple certificate on macOS: it signs with a
self-signed "Rudder Local Signing" identity it makes once in its own keychain.
The signature stays the same between installs, so the Accessibility grant
survives reinstalling.

Rudder is an agent app: no Dock icon or taskbar button, a menu-bar / tray icon
(Open, Settings…, Quit), and it registers itself to start at login (on Windows
under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`). Launching it again
while it runs opens the panel.

## Permissions

Windows needs none: any app may read and raise another's windows, so there is
no grant to ask for and the unlock overlay never shows.

On macOS, Accessibility, for the live preview, window rows and the keyboard while the
panel is up. Without it the panel shows an unlock overlay: drag the Rudder
icon into System Settings › Privacy & Security › Accessibility and switch it
on. Every dev build is ad-hoc signed with a new cdhash, so `just run` resets
the grant first.

## Keys

| macOS | Windows | |
|---|---|---|
| ⌥Space | Alt+Space | open / close the panel (rebindable) |
| ⌥\` | Alt+\` | go back (rebindable) |
| ↑ ↓, ⌃N ⌃P | ↑ ↓, Ctrl+N Ctrl+P | move |
| Enter | Enter | switch (launches a closed app, focuses a window row) |
| Esc | Esc | close and put the screen back |
| ⌃Q | Ctrl+Q | quit the selected app (rebindable) |
| ⌘D | Ctrl+D | favourite / unfavourite the selection (rebindable) |
| ⌃R | Ctrl+R | rename the selected pane or tab: Enter saves, Esc cancels, an empty name clears it (rebindable) |
| ⌃W ⌃U ⌃K ⌃A ⌃E ⌃B ⌃F ⌃H ⌃D, ⌥B ⌥F ⌥⌫ | the same with Ctrl and Alt | readline editing |

On Windows, Alt+Space normally opens the focused window's menu (Restore, Move,
Close…). That menu is the app's own reaction to the key rather than a
registered hotkey, so Rudder's RegisterHotKey takes the chord before any app
sees it. Only another app that registered it first (PowerToys Run, say) wins;
Settings then says so, and you can pick another chord.

## Recent work and renames

With nothing typed, the list is the running apps and favourites by when you
last used them, with the panes and tabs you used in the last 36 hours (at
most six) among them. An app whose panes or tabs are listed isn't listed again
as an app, typed query or not. Rudder learns that from what you do, not only from what
you pick in the panel: every app activation, and every time a provider's
focused context changes while its app is in front. A context that stays
focused for 5 seconds also earns a quarter of a pick's frecency, so the
panes you work in rise when you type.

A context is remembered by its repository and git branch (else its
directory), not by the provider's id, so a new shell on the same branch of a
repo and the unopened place for it are one thing. That's also what a rename (⌃R) sticks to: the name becomes
the row's title, the provider's title moves to the subtitle, and typing
either finds it. Names live in Rudder's own store, so they work for every
provider.

## Favourites

Starred places you can find by typing; they take their place in the list by
when you last used them, like apps. They live in
`~/.config/rudder.json` (also shown in Settings), which Rudder reloads
whenever it changes on disk:

```json
{
  "favourites": [
    {"title": "tamber-web PRs", "url": "https://github.com/tamber-inc/tamber-web/pulls",
     "keywords": ["prs", "pulls"]},
    {"title": "Notes", "app": "com.apple.Notes"},
    {"title": "Projects", "path": "~/projects"}
  ]
}
```

Each needs a `title` and one of `url`, `app` (bundle id) or `path`; `openWith`
(bundle id) picks the app a url opens in. On Windows an app's id is its
executable's file name: `"app": "notepad.exe"`, `"openWith": "firefox.exe"`. A url that's already open in a
provider's tab (Grazer) switches to that tab instead of opening another.

The same file holds where you left the panel. Drag the handle on its top edge
to move it, its edges to resize it, and double-click the handle to put it back; Rudder
writes the result under `panel`, one frame per screen (keyed by the screen's
usable area, so a different display or arrangement starts fresh):

```json
{
  "panel": {
    "0,33 1512x949": {"x": 850, "y": 84, "width": 640, "height": 420}
  }
}
```

No `panel`, or no entry for a screen, opens the panel centred at the default
size. Each of `x`, `y`, `width` and `height` must be a number, the size above 0.
Rudder rewrites the file only when a drag or resize ends, or on a reset.

## Icons

`Assets/Icon/Source.png` is the art; `Scripts/make-icons.sh` (ImageMagick 7)
turns it into the macOS app icon (`Icon.png`, Apple's 824-on-1024 grid), the
Windows `Icon.ico` (near full-bleed tile, plain silhouette at 16–48 px) and the
tray glyphs (`Tray.png` template for the menu bar, `TrayWindows.png`). The
outputs are committed; rerun the script only when the art changes.

## Debugging

- `RUDDER_LOG=1` traces the show/hide handshake, previews, the back stack and
  latency (hotkey → first frame, key → frame) on stderr.
- `Rudder --snapshot <dir>` renders the panel and the unlock overlay to PNGs.

## Publishing contexts from an app

Apps can list their own places (tabs, sessions) in Rudder. Rudder listens for
newline-delimited JSON (see `Client/RudderProtocol.h`): on macOS on
`~/Library/Application Support/Rudder/rudder.sock`, posting the distributed
notification `rudder.started` when it starts; on Windows on a per-user named
pipe (an eacp IPC channel), keeping the named event `Local\rudder.started` set
while it runs. The `RudderClient` library does the client
side; pull in just that with CPM:

```cmake
CPMAddPackage(NAME Rudder GIT_REPOSITORY <rudder repo> GIT_TAG main
              OPTIONS "RUDDER_CLIENT_ONLY ON")
target_link_libraries(MyApp PRIVATE RudderClient)
```

```cpp
auto rudder = rudder::RudderClient {{"MyApp", "com.example.myapp"}};
rudder.onActivate = [](const std::string& id) { /* bring that place forward */ };
rudder.pushContexts({{"1", "Title", "subtitle", "https://..."}});
```

It reconnects when Rudder restarts and is silent when Rudder isn't running.
Grazer is the first provider.
