#pragma once

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

// Really switch: activate the app and bring its front window forward. Also
// the live preview, which activates for real under key capture: an AX raise
// alone stops under the previously active app's front window.
bool activateApp(const RunningApp& app);

// The preview's cheaper activation: AXFrontmost plus a raise of the cached
// front window, no LaunchServices round trip. Needs accessibility.
bool previewApp(const RunningApp& app);

// While the panel is up, the keyboard is Rudder's whichever app is active:
// every key event system-wide goes to `onKey` (on the main thread) and is
// swallowed when it returns true. This is what lets the preview really
// activate the selected app -- its window then sits on top, under the panel
// -- while typing carries on into the panel. `onMouseDownOutside` fires for a
// click anywhere but `panel`. Needs accessibility; off (no tap, no cost)
// between summons.
struct KeyCapture
{
    std::function<bool(const eacp::Graphics::KeyEvent&)> onKey;
    std::function<void()> onMouseDownOutside;
};

bool startKeyCapture(eacp::Graphics::Window& panel, KeyCapture capture);
void stopKeyCapture();

// Captured at summon, before any preview: `origin`'s front window. Esc raises
// it again so the screen is exactly as it was.
void rememberPreviewOrigin(const RunningApp& origin);
void restorePreviewOrigin();

// The app's window titles, front to back (needs accessibility). For the
// preview pane.
std::vector<std::string> windowTitles(const RunningApp& app);

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

// Tells provider apps a (re)started Rudder is listening, so they reconnect:
// the distributed notification "rudder.started".
void announceStarted();
} // namespace rudder::platform
