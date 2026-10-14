#pragma once

#include <CaptureGate.h>

#include <eacp/Graphics/Graphics.h>

#include <functional>
#include <string>
#include <vector>

// Everything Rudder asks of the OS, behind one small seam so a Windows
// implementation can slot in beside Platform-macOS.mm. All calls are main
// thread only.
namespace rudder::platform
{
// A running, user-facing app (macOS: activation policy regular). `id` is its
// bundle id, or "pid:<n>" for the odd app without one.
struct RunningApp
{
    std::string id;
    std::string name;
    int pid = 0;
};

// Every running regular app except Rudder, in the OS's launch order.
std::vector<RunningApp> runningApps();

// The app that currently has the user's focus; empty id when that is Rudder
// or nothing.
RunningApp frontmostApp();

// The app's icon as RGBA pixels at 64px, rendered once and cached by id.
// Invalid when the app has none.
const eacp::Graphics::Image& appIcon(const RunningApp& app);
void forgetIcon(const std::string& appId);

// Really switch: activate the app and bring its front window forward (the AX
// part off the main thread).
bool activateApp(const RunningApp& app);

// While the panel is up, the keyboard is Rudder's whichever app is active:
// every key event system-wide is swallowed and handed to `onKey` on the main
// thread, except the `passThrough` chords (the global hotkeys). This is what
// lets the preview really activate the selected app -- its window then sits on
// top, under the panel -- while typing carries on into the panel.
//
// The tap runs on its own thread and decides in O(1) (CaptureGate.h); it never
// waits for the main thread. If the main thread sits on a key for more than
// two seconds, the OS times the tap out, the panel leaves the screen or
// accessibility is lost, capture switches itself off for good and `onAbort`
// runs on the main thread: the panel must go. `onMouseDownOutside` fires for a
// click in another app (a passive monitor: clicks are never held up). Needs
// accessibility; off (no tap, no timer, no cost) between summons.
struct KeyCapture
{
    std::function<void(const eacp::Graphics::KeyEvent&)> onKey;
    std::function<void()> onMouseDownOutside;
    std::function<void(const std::string& reason)> onAbort;
};

bool startKeyCapture(eacp::Graphics::Window& panel,
                     KeyCapture capture,
                     const std::vector<CaptureKey>& passThrough);
void stopKeyCapture();

// The chords that reach their hotkeys while capture is on; empty while a new
// chord is being recorded. At most four.
void setKeyCapturePassThrough(const std::vector<CaptureKey>& passThrough);

// The live preview's activation, off the main thread so a hung app can't
// stall Rudder: AXFrontmost plus a raise of the app's cached front window
// (`windowId` empty), or of that exact window. `restoreOrigin` raises the
// summon's origin window too (previewing the app the panel opened over).
// `onDone` gets whether it raised and the app's window titles, front to back,
// on the main thread. A newer preview supersedes one not yet started, which
// then never reports. Under key capture only: activates for real.
struct PreviewResult
{
    bool raised = false;
    std::vector<std::string> windowTitles;
};

void previewApp(const RunningApp& app,
                const std::string& windowId,
                bool restoreOrigin,
                std::function<void(PreviewResult)> onDone);

// The app's window titles, front to back, synchronously (needs
// accessibility). Only for --snapshot: the panel goes through previewApp.
std::vector<std::string> windowTitles(const RunningApp& app);

// Captured at summon, before any preview: `origin`'s front window. Esc raises
// it again so the screen is exactly as it was. Both queue behind any preview.
void rememberPreviewOrigin(const RunningApp& origin);
void restorePreviewOrigin();

// One of an app's windows, as a row the user can pick: titled standard
// windows only (helpers and untitled panels are skipped), minimised ones
// included. `id` is stable while the window lives.
struct AppWindow
{
    std::string id;
    std::string title;
    bool minimized = false;
};

// What is known of an app's windows right now, from the cache; empty until
// the first enumeration.
std::vector<AppWindow> cachedWindows(const RunningApp& app);

// Enumerates every app's windows through AX off the main thread, then keeps
// them current with AX observers (window created / destroyed / retitled /
// minimised) until stopWatchingWindows. `onChanged` runs on the main thread
// with the app whose windows changed. For the life of an open panel only, so
// nothing is registered while Rudder idles.
void watchWindows(const std::vector<RunningApp>& apps,
                  std::function<void(const std::string& appId)> onChanged);
void stopWatchingWindows();

// Brings that exact window forward and makes its app frontmost, optionally
// un-minimising it first. False if the window isn't known; the AX work runs
// off the main thread, and if the raise fails there the app is activated
// instead.
bool focusWindow(const RunningApp& app, const std::string& windowId, bool unminimize);

// Whether Rudder may drive other apps' windows. `prompt` shows the system
// dialog that sends the user to System Settings.
bool accessibilityTrusted(bool prompt);

// Privacy & Security > Accessibility in System Settings.
bool openAccessibilitySettings();

// What the user drags into the Accessibility list to grant Rudder.
std::string appBundlePath();

// Starts an OS file drag of `path` out of `view`, from the mouse-down being
// handled right now -- so it can land in another app (the Accessibility
// list). The drag image is the file's Finder icon.
bool startFileDrag(eacp::Graphics::View& view, const std::string& path);

struct WorkspaceEvents
{
    std::function<void(const RunningApp&)> onActivated = [](auto&) {};
    std::function<void(const RunningApp&)> onLaunched = [](auto&) {};
    std::function<void(const RunningApp&)> onTerminated = [](auto&) {};
};

// Subscribes to the OS's app lifecycle notifications -- no polling. One
// subscriber per process; a second call replaces the first.
void observeWorkspace(WorkspaceEvents events);

// The top-left (eacp screen space) that centres a width x height panel on the
// display under the mouse.
eacp::Graphics::Point panelOrigin(float width, float height);

// An app bundle in one of the app folders, running or not.
struct InstalledApp
{
    std::string id;
    std::string name;
    std::string path;
};

// Scans /Applications, /System/Applications (and their Utilities) and
// ~/Applications off the main thread, then hands the whole list to `onChange`
// on the main thread -- once now, and again whenever one of those folders
// gains or loses an app. FSEvents on the folders, so nothing runs while
// nothing changes. One watcher per process.
void watchInstalledApps(std::function<void(std::vector<InstalledApp>)> onChange);

// An installed app's icon, rendered on first ask and cached by id: the panel
// only asks for the rows it draws, so hundreds of apps cost nothing up front.
const eacp::Graphics::Image& bundleIcon(const std::string& id, const std::string& path);

// Asks the app to quit, as its own Quit menu item would: it may refuse (an
// unsaved-changes dialog) and stay running. Never a force quit.
bool quitApp(const RunningApp& app);

// Opens a url in the app with bundle id `openWith`, or the default handler.
bool openUrl(const std::string& url, const std::string& openWith);

// Opens a file or folder (a leading ~ is the home folder) in its default
// app, Finder for a folder.
bool openPath(const std::string& path);

// The bundle path of the app a url would open in, for its icon.
std::string handlerFor(const std::string& url, const std::string& openWith);

// Launches (and activates) the app at `path`.
bool launchApp(const std::string& path);

// Whether macOS itself has an enabled shortcut on this chord (System Settings
// > Keyboard > Keyboard Shortcuts). The system wins those, so binding one
// would be a hotkey that never fires.
bool isSystemShortcut(std::uint16_t keyCode, const eacp::Graphics::ModifierKeys& modifiers);

// Tells provider apps a (re)started Rudder is listening, so they reconnect:
// the distributed notification "rudder.started".
void announceStarted();
} // namespace rudder::platform
