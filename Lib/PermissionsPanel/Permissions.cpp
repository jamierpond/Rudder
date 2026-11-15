#include "Permissions.h"

namespace pond::permissions
{
namespace
{
constexpr auto privacyPane = "x-apple.systempreferences:com.apple.preference.security?";
} // namespace

std::string settingsUrl(Kind kind, const std::string& bundleId)
{
    const auto privacy = [](const char* anchor)
    { return std::string {privacyPane} + anchor; };

    switch (kind)
    {
        case Kind::accessibility:
            return privacy("Privacy_Accessibility");
        case Kind::fullDiskAccess:
            return privacy("Privacy_AllFiles");
        case Kind::automation:
            return privacy("Privacy_Automation");
        case Kind::filesAndFolders:
            return privacy("Privacy_FilesAndFolders");
        case Kind::notifications:
            return "x-apple.systempreferences:com.apple.Notifications-Settings.extension?id="
                   + bundleId;
        case Kind::screenRecording:
            return privacy("Privacy_ScreenCapture");
        case Kind::systemAudioRecording:
            return privacy("Privacy_AudioCapture");
    }

    return privacy("");
}

std::string statusLabel(Status status)
{
    switch (status)
    {
        case Status::granted:
            return "granted";
        case Status::notGranted:
            return "not granted";
        case Status::denied:
            return "denied";
        case Status::unknown:
            return "asks on first use";
    }

    return {};
}

namespace
{
bool canAskDirectly(const Info& grant, Status status, bool prompted)
{
    if (!grant.canPrompt || prompted)
        return false;

    return status != Status::denied;
}
} // namespace

std::string actionLabel(const Info& grant, Status status, bool prompted)
{
    if (status == Status::granted)
        return {};

    if (canAskDirectly(grant, status, prompted))
        return "Grant";

    return "Open Settings";
}

std::vector<std::string> resetCommand(const std::string& bundleId,
                                      const std::string& service)
{
    return {"/usr/bin/tccutil", "reset", service, bundleId};
}

namespace
{
constexpr auto waitThenOpen =
    R"(while /bin/kill -0 "$1" 2>/dev/null; do /bin/sleep 0.2; done; exec /usr/bin/open "$2")";
constexpr auto waitThenOpenWithArguments =
    R"(pid="$1"; bundle="$2"; shift 2; while /bin/kill -0 "$pid" 2>/dev/null; do /bin/sleep 0.2; done; exec /usr/bin/open "$bundle" --args "$@")";
} // namespace

std::vector<std::string> relaunchCommand(int pid,
                                         const std::string& bundlePath,
                                         const std::string& scriptName,
                                         const std::vector<std::string>& openArguments)
{
    // The app's own pid, path and arguments go in as $1, $2..., never into the script text.
    const auto* script = openArguments.empty() ? waitThenOpen : waitThenOpenWithArguments;
    auto argv = std::vector<std::string> {
        "/bin/sh", "-c", script, scriptName, std::to_string(pid), bundlePath};
    argv.insert(argv.end(), openArguments.begin(), openArguments.end());
    return argv;
}
} // namespace pond::permissions
