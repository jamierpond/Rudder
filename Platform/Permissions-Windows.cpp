#include "Permissions.h"

// Windows has no Accessibility grant: any app may read and raise another's
// windows and post input (an elevated one's aside, which no grant would
// unlock either). Everything reports granted, so the unlock overlay never
// shows and nothing sends the user to a settings pane.
namespace rudder::ui_automation
{
bool isAccessibilityTrusted(bool)
{
    return true;
}

std::string_view accessibilitySettingsURL()
{
    return {};
}

bool openAccessibilitySettings()
{
    return false;
}

std::string currentAppBundlePath()
{
    return {};
}

bool isPostEventAccessGranted()
{
    return true;
}

AutomationPermission automationPermission(std::string_view, bool)
{
    return AutomationPermission::granted;
}
} // namespace rudder::ui_automation
