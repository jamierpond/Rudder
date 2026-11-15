#include <PermissionsPanel/Permissions.h>

#include <NanoTest/NanoTest.h>

// The privacy-grants model behind each app's Permissions… panel: what each
// row's button says, the Settings panes, and the commands the reset and
// relaunch run.

using namespace nano;
using namespace pond::permissions;

namespace
{
const auto accessibility = Info {Kind::accessibility, "Accessibility", "", "Accessibility", true, true};
const auto fullDiskAccess =
    Info {Kind::fullDiskAccess, "Full Disk Access", "", "Full Disk Access", true, false};
} // namespace

auto tSettingsUrls = test("Permissions/settingsUrls") = []
{
    const auto privacy =
        std::string {"x-apple.systempreferences:com.apple.preference.security?"};

    check(settingsUrl(Kind::accessibility, "") == privacy + "Privacy_Accessibility");
    check(settingsUrl(Kind::fullDiskAccess, "") == privacy + "Privacy_AllFiles");
    check(settingsUrl(Kind::automation, "") == privacy + "Privacy_Automation");
    check(settingsUrl(Kind::filesAndFolders, "") == privacy + "Privacy_FilesAndFolders");
    check(settingsUrl(Kind::screenRecording, "") == privacy + "Privacy_ScreenCapture");
    check(settingsUrl(Kind::systemAudioRecording, "") == privacy + "Privacy_AudioCapture");
    check(settingsUrl(Kind::notifications, "audio.pond.cowterm")
          == "x-apple.systempreferences:com.apple.Notifications-Settings.extension"
             "?id=audio.pond.cowterm");
};

auto tActionLabels = test("Permissions/actionLabels") = []
{
    check(actionLabel(accessibility, Status::granted, false).empty());
    check(actionLabel(accessibility, Status::notGranted, false) == "Grant");
    check(actionLabel(accessibility, Status::notGranted, true) == "Open Settings",
          "macOS prompts once; after that only Settings helps");
    check(actionLabel(accessibility, Status::denied, false) == "Open Settings");
    check(actionLabel(fullDiskAccess, Status::notGranted, false) == "Open Settings",
          "no API prompts for Full Disk Access");
};

auto tStatusLabels = test("Permissions/statusLabels") = []
{
    check(statusLabel(Status::granted) == "granted");
    check(statusLabel(Status::notGranted) == "not granted");
};

auto tResetCommand = test("Permissions/resetCommand") = []
{
    check(resetCommand("audio.pond.cowterm")
          == std::vector<std::string> {"/usr/bin/tccutil", "reset", "All", "audio.pond.cowterm"});
    check(resetCommand("audio.pond.cowterm", "Accessibility")[2] == "Accessibility");
};

auto tRelaunchCommand = test("Permissions/relaunchCommand") = []
{
    const auto argv = relaunchCommand(4242, "/Applications/Cow Term.app", "cowterm-relaunch");

    check(argv.size() == 6);
    check(argv[0] == "/bin/sh" && argv[1] == "-c");
    check(argv[2].find("$1") != std::string::npos && argv[2].find("$2") != std::string::npos);
    check(argv[2].find("Cow Term") == std::string::npos,
          "the path is an argument, never script text");
    check(argv[3] == "cowterm-relaunch");
    check(argv[4] == "4242");
    check(argv[5] == "/Applications/Cow Term.app");
};

auto tRelaunchCommandWithArguments = test("Permissions/relaunchCommandPassesOpenArguments") = []
{
    const auto argv =
        relaunchCommand(4242, "/Apps/ScreenCap.app", "screencap-relaunch", {"permissions"});

    check(argv.size() == 7);
    check(argv[2].find("--args \"$@\"") != std::string::npos);
    check(argv[6] == "permissions");
};
