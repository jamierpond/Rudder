#include "Platform.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <algorithm>
#include <map>
#include <utility>

namespace rudder::platform
{
namespace
{
std::string toString(NSString* text)
{
    return text != nil ? std::string {text.UTF8String} : std::string {};
}

std::string idOf(NSRunningApplication* app)
{
    if (app.bundleIdentifier != nil)
        return toString(app.bundleIdentifier);

    return "pid:" + std::to_string(app.processIdentifier);
}

bool isSelf(NSRunningApplication* app)
{
    return app.processIdentifier
           == NSRunningApplication.currentApplication.processIdentifier;
}

RunningApp describe(NSRunningApplication* app)
{
    return {idOf(app), toString(app.localizedName), app.processIdentifier};
}

NSRunningApplication* lookUp(const RunningApp& app)
{
    return [NSRunningApplication runningApplicationWithProcessIdentifier:app.pid];
}

// The window a user would call "the app's window": focused, else main, else
// the first one listed. Returned +1 (Create rule), or null.
AXUIElementRef copyFrontWindow(pid_t pid)
{
    auto appElement = AXUIElementCreateApplication(pid);

    if (appElement == nullptr)
        return nullptr;

    CFTypeRef window = nullptr;

    for (auto attribute: {kAXFocusedWindowAttribute, kAXMainWindowAttribute})
        if (AXUIElementCopyAttributeValue(appElement, attribute, &window)
                == kAXErrorSuccess
            && window != nullptr)
            break;

    if (window == nullptr)
    {
        CFTypeRef windows = nullptr;

        if (AXUIElementCopyAttributeValue(appElement, kAXWindowsAttribute, &windows)
                == kAXErrorSuccess
            && windows != nullptr)
        {
            auto list = (CFArrayRef) windows;

            if (CFArrayGetCount(list) > 0)
                window = CFRetain(CFArrayGetValueAtIndex(list, 0));

            CFRelease(windows);
        }
    }

    CFRelease(appElement);
    return (AXUIElementRef) window;
}

std::map<std::string, eacp::Graphics::Image>& iconCache()
{
    static auto cache = std::map<std::string, eacp::Graphics::Image> {};
    return cache;
}

AXUIElementRef previewOrigin = nullptr;

id activateObserver = nil;
id launchObserver = nil;
id terminateObserver = nil;
} // namespace

std::vector<RunningApp> runningApps()
{
    auto apps = std::vector<RunningApp> {};

    for (NSRunningApplication* app in NSWorkspace.sharedWorkspace.runningApplications)
        if (app.activationPolicy == NSApplicationActivationPolicyRegular
            && !app.terminated && !isSelf(app))
            apps.push_back(describe(app));

    return apps;
}

RunningApp frontmostApp()
{
    auto* front = NSWorkspace.sharedWorkspace.frontmostApplication;

    if (front == nil || isSelf(front))
        return {};

    return describe(front);
}

const eacp::Graphics::Image& appIcon(const RunningApp& app)
{
    auto& cache = iconCache();

    if (auto found = cache.find(app.id); found != cache.end())
        return found->second;

    auto& image = cache[app.id];
    auto* icon = lookUp(app).icon;

    if (icon == nil)
        return image;

    // 64px: crisp at the panel's 32pt rows on Retina.
    constexpr auto size = 64;
    auto pixels = eacp::Graphics::ImageData(size * size * 4);

    auto* space = CGColorSpaceCreateDeviceRGB();
    auto context = CGBitmapContextCreate(pixels.data(),
                                         size,
                                         size,
                                         8,
                                         size * 4,
                                         space,
                                         kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(space);

    if (context == nullptr)
        return image;

    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext.currentContext =
        [NSGraphicsContext graphicsContextWithCGContext:context flipped:NO];
    [icon drawInRect:NSMakeRect(0, 0, size, size)
            fromRect:NSZeroRect
           operation:NSCompositingOperationCopy
            fraction:1.0];
    [NSGraphicsContext restoreGraphicsState];
    CGContextRelease(context);

    // CoreGraphics only renders premultiplied; eacp images are straight alpha.
    for (auto i = 0; i < size * size * 4; i += 4)
    {
        auto alpha = (int) pixels[i + 3];

        if (alpha == 0 || alpha == 255)
            continue;

        for (auto c = 0; c < 3; ++c)
            pixels[i + c] = (std::uint8_t) std::min(255, (int) pixels[i + c] * 255 / alpha);
    }

    image = eacp::Graphics::Image(size, size, std::move(pixels));
    return image;
}

void forgetIcon(const std::string& appId)
{
    iconCache().erase(appId);
}

bool activateApp(const RunningApp& app)
{
    auto* running = lookUp(app);

    if (running == nil || running.terminated)
        return false;

    // Rudder is a background agent when the back hotkey fires, and macOS 14's
    // cooperative activation ignores requests from an inactive app -- but
    // setting AXFrontmost goes through regardless. The NSRunningApplication
    // call covers the no-permission case (and the panel, where Rudder is the
    // active app and so may yield).
    auto activated = false;

    if (@available(macOS 14.0, *))
        if (NSApp.isActive)
            activated = [running activateFromApplication:NSRunningApplication
                                                             .currentApplication
                                                 options:0];

    if (!activated)
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        activated =
            [running activateWithOptions:NSApplicationActivateIgnoringOtherApps];
#pragma clang diagnostic pop
    }

    if (AXIsProcessTrusted())
    {
        auto appElement = AXUIElementCreateApplication(app.pid);
        AXUIElementSetAttributeValue(appElement, kAXFrontmostAttribute, kCFBooleanTrue);
        CFRelease(appElement);

        if (auto window = copyFrontWindow(app.pid))
        {
            AXUIElementPerformAction(window, kAXRaiseAction);
            CFRelease(window);
        }

        activated = true;
    }

    return activated;
}

bool previewApp(const RunningApp& app)
{
    if (!AXIsProcessTrusted())
        return false;

    auto window = copyFrontWindow(app.pid);

    if (window == nullptr)
        return false;

    auto raised = AXUIElementPerformAction(window, kAXRaiseAction) == kAXErrorSuccess;
    CFRelease(window);
    return raised;
}

void rememberPreviewOrigin()
{
    if (previewOrigin != nullptr)
    {
        CFRelease(previewOrigin);
        previewOrigin = nullptr;
    }

    auto front = frontmostApp();

    if (front.pid != 0 && AXIsProcessTrusted())
        previewOrigin = copyFrontWindow(front.pid);
}

void restorePreviewOrigin()
{
    if (previewOrigin != nullptr)
        AXUIElementPerformAction(previewOrigin, kAXRaiseAction);
}

std::vector<std::string> windowTitles(const RunningApp& app)
{
    auto titles = std::vector<std::string> {};

    if (!AXIsProcessTrusted())
        return titles;

    auto appElement = AXUIElementCreateApplication(app.pid);
    CFTypeRef windows = nullptr;

    if (AXUIElementCopyAttributeValue(appElement, kAXWindowsAttribute, &windows)
            == kAXErrorSuccess
        && windows != nullptr)
    {
        auto list = (CFArrayRef) windows;

        for (CFIndex i = 0; i < CFArrayGetCount(list); ++i)
        {
            auto window = (AXUIElementRef) CFArrayGetValueAtIndex(list, i);
            CFTypeRef title = nullptr;

            if (AXUIElementCopyAttributeValue(window, kAXTitleAttribute, &title)
                    == kAXErrorSuccess
                && title != nullptr)
            {
                auto text = toString((__bridge NSString*) title);

                if (!text.empty())
                    titles.push_back(std::move(text));

                CFRelease(title);
            }
        }

        CFRelease(windows);
    }

    CFRelease(appElement);
    return titles;
}

bool accessibilityTrusted(bool prompt)
{
    if (!prompt)
        return AXIsProcessTrusted();

    auto* options = @{(__bridge NSString*) kAXTrustedCheckOptionPrompt: @YES};
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef) options);
}

void observeWorkspace(WorkspaceEvents events)
{
    auto* center = NSWorkspace.sharedWorkspace.notificationCenter;

    for (id observer: {activateObserver, launchObserver, terminateObserver})
        if (observer != nil)
            [center removeObserver:observer];

    auto shared = std::make_shared<WorkspaceEvents>(std::move(events));

    auto appIn = [](NSNotification* note) -> NSRunningApplication*
    {
        NSRunningApplication* app = note.userInfo[NSWorkspaceApplicationKey];

        if (app == nil || isSelf(app)
            || app.activationPolicy != NSApplicationActivationPolicyRegular)
            return nil;

        return app;
    };

    activateObserver =
        [center addObserverForName:NSWorkspaceDidActivateApplicationNotification
                            object:nil
                             queue:NSOperationQueue.mainQueue
                        usingBlock:^(NSNotification* note) {
                          if (auto* app = appIn(note))
                              shared->onActivated(describe(app));
                        }];

    launchObserver =
        [center addObserverForName:NSWorkspaceDidLaunchApplicationNotification
                            object:nil
                             queue:NSOperationQueue.mainQueue
                        usingBlock:^(NSNotification* note) {
                          if (auto* app = appIn(note))
                              shared->onLaunched(describe(app));
                        }];

    // A quitting app may already report a prohibited policy, so terminate
    // skips the policy check; the app side ignores ids it never listed.
    terminateObserver =
        [center addObserverForName:NSWorkspaceDidTerminateApplicationNotification
                            object:nil
                             queue:NSOperationQueue.mainQueue
                        usingBlock:^(NSNotification* note) {
                          NSRunningApplication* app =
                              note.userInfo[NSWorkspaceApplicationKey];

                          if (app != nil && !isSelf(app))
                              shared->onTerminated(describe(app));
                        }];
}

eacp::Graphics::Point panelOrigin(float width, float height)
{
    auto mouse = NSEvent.mouseLocation;
    auto* screen = NSScreen.mainScreen;

    for (NSScreen* candidate in NSScreen.screens)
        if (NSMouseInRect(mouse, candidate.frame, NO))
            screen = candidate;

    auto area = screen.visibleFrame;
    auto x = area.origin.x + (area.size.width - width) / 2;
    auto bottom = area.origin.y + (area.size.height - height) / 2;

    // Cocoa measures up from the primary display's bottom; eacp down from
    // its top.
    auto primaryHeight = NSScreen.screens.firstObject.frame.size.height;
    return {(float) x, (float) (primaryHeight - bottom - height)};
}

void announceStarted()
{
    [NSDistributedNotificationCenter.defaultCenter
        postNotificationName:@"rudder.started"
                      object:nil
                    userInfo:nil
          deliverImmediately:YES];
}
} // namespace rudder::platform
