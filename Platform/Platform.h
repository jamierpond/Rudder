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

// Really switch: activate the app and bring its frontmost window forward.
bool activateApp(const RunningApp& app);

// The live preview. Raise the app's front window WITHOUT activating the app,
// so the panel keeps key and typing goes on. On macOS this is an AX raise,
// and the window server only lets it go as far as just under the front window
// of the app the panel was summoned over: anything not covered by that window
// shows, a maximised one hides it. (Activating the target and taking key back
// lands it on top but leaves keystrokes going to the target; not shipped.)
// False when it couldn't (no accessibility permission, no window).
bool previewApp(const RunningApp& app);

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
