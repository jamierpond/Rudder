#pragma once

#include <functional>
#include <string>
#include <vector>

namespace eacp::Graphics
{
class Image;
class View;
} // namespace eacp::Graphics

// The macOS privacy grants (TCC) a pond app asks for, and the list it shows
// them in. Each app passes the grants it needs, with its own reason for each.
//
// The model here is pure, so it is tested anywhere; the platform half (live
// checks, prompts, System Settings) is Permissions-macOS.mm. Windows and
// Linux have no such grants: Permissions-Default.cpp reports each in force.
namespace pond::permissions
{
enum class Kind
{
    accessibility,
    fullDiskAccess,
    automation,
    filesAndFolders,
    notifications,
    screenRecording,
    systemAudioRecording
};

enum class Status
{
    granted,

    // Off, or never asked.
    notGranted,

    // Asked and refused: macOS will not prompt again, only Settings helps.
    denied,

    // Checking would itself prompt (Files & Folders), so it waits for Grant.
    unknown
};

struct Info
{
    Kind kind;
    std::string name;
    std::string why;
    // System Settings' name for the list, for the steps.
    std::string listName;

    // The list takes an app dragged onto it (or added with +).
    bool dragToList = false;

    // macOS has an API that shows its own prompt for this one.
    bool canPrompt = false;
};

// The exact System Settings pane. Notifications are per app, so they need
// the bundle id.
std::string settingsUrl(Kind kind, const std::string& bundleId);

struct Entry
{
    Kind kind;
    Status status;
};

std::string statusLabel(Status status);

// The row's button: "" for a grant already in force. `prompted` is whether
// Grant was pressed already this run; macOS shows its prompt once, after that
// only System Settings can turn the grant on.
std::string actionLabel(const Info& grant, Status status, bool prompted);

// tccutil's argv for wiping one service (or All) for one bundle id.
std::vector<std::string> resetCommand(const std::string& bundleId,
                                      const std::string& service = "All");

// /bin/sh argv, named `scriptName` in ps, that waits for `pid` to exit, then
// opens `bundlePath` again, passing it `openArguments` when there are any.
std::vector<std::string> relaunchCommand(int pid,
                                         const std::string& bundlePath,
                                         const std::string& scriptName,
                                         const std::vector<std::string>& openArguments = {});

namespace platform
{
// Live, never prompts. Cheap except where noted in the .mm.
Status check(Kind kind);

// The system prompt where there is one, otherwise (or the second time) the
// System Settings pane. Asynchronous where macOS blocks; `done` runs on the
// main thread once the answer may have changed.
void request(Kind kind, bool prompted, std::function<void()> done);

bool openSettings(Kind kind);

// An OS drag of this app's bundle from `view`, during its mouse-down.
bool startAppDrag(eacp::Graphics::View& view);

eacp::Graphics::Image appIcon();

std::string bundleIdentifier();
std::string bundlePath();

// tccutil reset <service> for this app's bundle id, as the user (no sudo).
// False, with tccutil's words in `error`, if it failed.
bool reset(const std::string& service, std::string& error);

// Starts a detached waiter that opens this app's bundle again once this
// process has exited (see relaunchCommand). The caller then quits. False if
// there is no bundle or the waiter didn't start.
bool startRelaunchAfterExit(const std::string& scriptName,
                            const std::vector<std::string>& openArguments = {});
} // namespace platform
} // namespace pond::permissions
