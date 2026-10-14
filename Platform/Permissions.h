#pragma once

#include <string>
#include <string_view>

namespace rudder::ui_automation
{
// Prompting opens System Settings; probing never does.
bool isAccessibilityTrusted(bool promptIfNeeded = false);

std::string_view accessibilitySettingsURL();

bool openAccessibilitySettings();

// What the user drags into the Accessibility list to grant this process.
std::string currentAppBundlePath();

// Never prompts.
bool isPostEventAccessGranted();

enum class AutomationPermission
{
    granted,
    denied,
    notDetermined,
    unavailable
};

// The target must be running for a verdict.
AutomationPermission automationPermission(std::string_view bundleId,
                                          bool promptIfNeeded = false);
} // namespace rudder::ui_automation
