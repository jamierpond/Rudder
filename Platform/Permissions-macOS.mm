#include "Permissions.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/Foundation.h>

#include <cstring>

namespace rudder::ui_automation
{
bool isAccessibilityTrusted(bool promptIfNeeded)
{
    if (!promptIfNeeded)
        return AXIsProcessTrusted();

    NSDictionary* options = @{(__bridge NSString*) kAXTrustedCheckOptionPrompt: @YES};
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef) options);
}

std::string_view accessibilitySettingsURL()
{
    return "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility";
}

bool openAccessibilitySettings()
{
    const auto address = accessibilitySettingsURL();
    NSString* text = [[NSString alloc] initWithBytes:address.data()
                                              length:address.size()
                                            encoding:NSUTF8StringEncoding];
    NSURL* url = [NSURL URLWithString:text];
    return url != nil && [NSWorkspace.sharedWorkspace openURL:url];
}

bool canResetPrivacyGrants()
{
    return true;
}

bool resetPrivacyGrants()
{
    // The services Scripts/lib.mjs resets for a dev launch (RUDDER.devResetsPrivacy).
    static constexpr const char* services[] = {"Accessibility"};

    NSString* bundleId = NSBundle.mainBundle.bundleIdentifier;
    if (bundleId.length == 0)
        return false;

    auto ok = true;

    for (const auto* service: services)
    {
        NSTask* task = [[NSTask alloc] init];
        task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/tccutil"];
        task.arguments = @[@"reset", @(service), bundleId];
        task.standardOutput = NSFileHandle.fileHandleWithNullDevice;
        task.standardError = NSFileHandle.fileHandleWithNullDevice;

        NSError* error = nil;
        if (![task launchAndReturnError:&error])
        {
            ok = false;
            continue;
        }

        [task waitUntilExit];
        ok = ok && task.terminationStatus == 0;
    }

    return ok;
}

std::string currentAppBundlePath()
{
    const auto* path = NSBundle.mainBundle.bundlePath.UTF8String;
    return path != nullptr ? std::string {path} : std::string {};
}

bool isPostEventAccessGranted()
{
    return CGPreflightPostEventAccess();
}

AutomationPermission automationPermission(std::string_view bundleId,
                                          bool promptIfNeeded)
{
    NSString* identifier = [[NSString alloc] initWithBytes:bundleId.data()
                                                    length:bundleId.size()
                                                  encoding:NSUTF8StringEncoding];
    if (identifier == nil)
        return AutomationPermission::unavailable;

    const auto* bytes = identifier.UTF8String;
    auto target = AEDesc {};
    const auto created = AECreateDesc(typeApplicationBundleID,
                                      bytes,
                                      static_cast<Size>(strlen(bytes)),
                                      &target);
    if (created != noErr)
        return AutomationPermission::unavailable;

    const auto verdict =
        AEDeterminePermissionToAutomateTarget(&target, typeWildCard, typeWildCard, promptIfNeeded);
    AEDisposeDesc(&target);

    switch (verdict)
    {
        case noErr:
            return AutomationPermission::granted;
        case errAEEventNotPermitted:
            return AutomationPermission::denied;
        case errAEEventWouldRequireUserConsent:
            return AutomationPermission::notDetermined;
        default:
            return AutomationPermission::unavailable;
    }
}
} // namespace rudder::ui_automation
