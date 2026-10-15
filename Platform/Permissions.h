#pragma once

#include <string>
#include <string_view>

namespace rudder::ui_automation
{
// Prompting opens System Settings; probing never does.
bool isAccessibilityTrusted(bool promptIfNeeded = false);

std::string_view accessibilitySettingsURL();

bool openAccessibilitySettings();

// Whether this platform keeps per-app privacy grants that can go stale.
bool canResetPrivacyGrants();

// Drops this app's own TCC entries (tccutil reset <service> <bundle id>, for
// each service Rudder asks for) so a grant pinned to an old signature -- the
// toggle shown on, the app still refused -- stops shadowing this binary.
// False if any reset failed.
bool resetPrivacyGrants();

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
