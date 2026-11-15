#include "Permissions.h"

#include <eacp/Graphics/Graphics.h>

// Windows and Linux have no privacy grants for an app to hold: it drives other
// apps, reads the user's files or records the screen without asking anyone.
// Every grant is in force, so there is nothing to request, reset or relaunch
// for.
namespace pond::permissions::platform
{
Status check(Kind)
{
    return Status::granted;
}

void request(Kind, bool, std::function<void()> done)
{
    done();
}

bool openSettings(Kind)
{
    return false;
}

bool startAppDrag(eacp::Graphics::View&)
{
    return false;
}

eacp::Graphics::Image appIcon()
{
    return {};
}

std::string bundleIdentifier()
{
    return {};
}

std::string bundlePath()
{
    return {};
}

bool reset(const std::string&, std::string& error)
{
    error = "this platform has no privacy grants to reset";
    return false;
}

bool startRelaunchAfterExit(const std::string&, const std::vector<std::string>&)
{
    return false;
}
} // namespace pond::permissions::platform
