#include "Platform.h"
#include "Permissions.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <algorithm>
#include <map>
#include <utility>

// Offers the dragged file for copy or link -- what Finder and System
// Settings' privacy lists accept from another app.
@interface RudderFileDragSource : NSObject <NSDraggingSource>
@end

@implementation RudderFileDragSource
- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context
{
    return NSDragOperationCopy | NSDragOperationLink | NSDragOperationGeneric;
}
@end

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

// A hung app must not hang the switcher: every AX round trip gives up fast.
constexpr auto axTimeoutSeconds = 0.25f;

// The window a user would call "the app's window": focused, else main, else
// the first one listed. Returned +1 (Create rule), or null.
AXUIElementRef copyFrontWindow(pid_t pid)
{
    auto appElement = AXUIElementCreateApplication(pid);

    if (appElement == nullptr)
        return nullptr;

    AXUIElementSetMessagingTimeout(appElement, axTimeoutSeconds);

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

// Each app's front window element, so a preview raise is one AX call rather
// than a walk to find the window first. Refreshed when the app activates (its
// front window may have changed) and when a raise reports the element stale.
std::map<pid_t, AXUIElementRef>& frontWindows()
{
    static auto windows = std::map<pid_t, AXUIElementRef> {};
    return windows;
}

void forgetFrontWindow(pid_t pid)
{
    auto& windows = frontWindows();

    if (auto found = windows.find(pid); found != windows.end())
    {
        if (found->second != nullptr)
            CFRelease(found->second);

        windows.erase(found);
    }
}

AXUIElementRef refreshFrontWindow(pid_t pid)
{
    forgetFrontWindow(pid);
    auto window = copyFrontWindow(pid);

    if (window != nullptr)
        frontWindows()[pid] = window;

    return window;
}

AXUIElementRef cachedFrontWindow(pid_t pid)
{
    auto& windows = frontWindows();

    if (auto found = windows.find(pid); found != windows.end())
        return found->second;

    return refreshFrontWindow(pid);
}

bool raiseFrontWindow(pid_t pid)
{
    auto window = cachedFrontWindow(pid);

    if (window != nullptr && AXUIElementPerformAction(window, kAXRaiseAction) == kAXErrorSuccess)
        return true;

    // Closed or replaced since it was cached: look again, once.
    window = refreshFrontWindow(pid);

    if (window == nullptr)
    {
        NSLog(@"Rudder: preview raise failed for pid %d: no window found "
              @"(trusted: %d)",
              pid,
              AXIsProcessTrusted());
        return false;
    }

    auto error = AXUIElementPerformAction(window, kAXRaiseAction);

    if (error != kAXErrorSuccess)
        NSLog(@"Rudder: preview raise failed for pid %d: AXError %d", pid, (int) error);

    return error == kAXErrorSuccess;
}

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
        AXUIElementSetMessagingTimeout(appElement, axTimeoutSeconds);
        AXUIElementSetAttributeValue(appElement, kAXFrontmostAttribute, kCFBooleanTrue);
        CFRelease(appElement);

        raiseFrontWindow(app.pid);
        activated = true;
    }

    return activated;
}

namespace
{
CFMachPortRef captureTap = nullptr;
KeyCapture activeCapture;
NSWindow* capturePanel = nil;

eacp::Graphics::KeyEvent toKeyEvent(CGEventType type, CGEventRef event)
{
    auto key = eacp::Graphics::KeyEvent {};
    key.type = type == kCGEventKeyUp ? eacp::Graphics::KeyEventType::Up
                                     : eacp::Graphics::KeyEventType::Down;
    key.keyCode =
        (std::uint16_t) CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
    key.isRepeat = CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat) != 0;
    key.timestamp = (double) CGEventGetTimestamp(event) / 1e9;

    auto flags = CGEventGetFlags(event);
    key.modifiers.shift = (flags & kCGEventFlagMaskShift) != 0;
    key.modifiers.control = (flags & kCGEventFlagMaskControl) != 0;
    key.modifiers.alt = (flags & kCGEventFlagMaskAlternate) != 0;
    key.modifiers.command = (flags & kCGEventFlagMaskCommand) != 0;

    UniChar buffer[8];
    UniCharCount length = 0;
    CGEventKeyboardGetUnicodeString(event, 8, &length, buffer);
    key.characters = toString([NSString stringWithCharacters:buffer length:length]);

    // What the key types with no modifiers held, as AppKit reports it.
    auto* plain = [NSEvent eventWithCGEvent:event];
    key.charactersIgnoringModifiers =
        plain != nil && (type == kCGEventKeyDown || type == kCGEventKeyUp)
            ? toString(plain.charactersIgnoringModifiers)
            : std::string {};

    return key;
}

CGEventRef captureCallback(CGEventTapProxy, CGEventType type, CGEventRef event, void*)
{
    // macOS switches a slow tap off; switch it straight back on.
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput)
    {
        if (captureTap != nullptr)
            CGEventTapEnable(captureTap, true);

        return event;
    }

    if (type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown)
    {
        auto at = CGEventGetLocation(event);
        auto frame = capturePanel != nil ? capturePanel.frame : NSZeroRect;

        // CG measures down from the primary display's top, Cocoa up from its
        // bottom.
        auto primaryHeight = NSScreen.screens.firstObject.frame.size.height;
        auto point = NSMakePoint(at.x, primaryHeight - at.y);

        if (!NSPointInRect(point, frame) && activeCapture.onMouseDownOutside)
            activeCapture.onMouseDownOutside();

        return event;
    }

    if (type != kCGEventKeyDown && type != kCGEventKeyUp)
        return event;

    if (activeCapture.onKey && activeCapture.onKey(toKeyEvent(type, event)))
        return nullptr;

    return event;
}
} // namespace

bool startKeyCapture(eacp::Graphics::Window& panel, KeyCapture capture)
{
    activeCapture = std::move(capture);
    capturePanel = (__bridge NSWindow*) panel.getHandle();

    if (captureTap == nullptr)
    {
        auto mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp)
                    | CGEventMaskBit(kCGEventLeftMouseDown)
                    | CGEventMaskBit(kCGEventRightMouseDown);

        captureTap = CGEventTapCreate(kCGSessionEventTap,
                                      kCGHeadInsertEventTap,
                                      kCGEventTapOptionDefault,
                                      mask,
                                      captureCallback,
                                      nullptr);

        if (captureTap == nullptr)
        {
            NSLog(@"Rudder: key capture unavailable (trusted: %d)", AXIsProcessTrusted());
            return false;
        }

        auto source = CFMachPortCreateRunLoopSource(nullptr, captureTap, 0);
        CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
        CFRelease(source);
    }

    CGEventTapEnable(captureTap, true);
    return true;
}

void stopKeyCapture()
{
    if (captureTap != nullptr)
        CGEventTapEnable(captureTap, false);

    activeCapture = {};
    capturePanel = nil;
}

bool previewApp(const RunningApp& app)
{
    if (!AXIsProcessTrusted())
        return false;

    auto appElement = AXUIElementCreateApplication(app.pid);
    AXUIElementSetMessagingTimeout(appElement, axTimeoutSeconds);
    auto error =
        AXUIElementSetAttributeValue(appElement, kAXFrontmostAttribute, kCFBooleanTrue);
    CFRelease(appElement);

    if (error != kAXErrorSuccess)
        NSLog(@"Rudder: preview activation failed for %s: AXError %d",
              app.name.c_str(),
              (int) error);

    return raiseFrontWindow(app.pid) && error == kAXErrorSuccess;
}

void rememberPreviewOrigin(const RunningApp& origin)
{
    if (previewOrigin != nullptr)
    {
        CFRelease(previewOrigin);
        previewOrigin = nullptr;
    }

    if (origin.pid != 0 && AXIsProcessTrusted())
    {
        previewOrigin = cachedFrontWindow(origin.pid);

        if (previewOrigin != nullptr)
            CFRetain(previewOrigin);
    }
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
    AXUIElementSetMessagingTimeout(appElement, axTimeoutSeconds);
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
    return ui_automation::isAccessibilityTrusted(prompt);
}

bool openAccessibilitySettings()
{
    return ui_automation::openAccessibilitySettings();
}

std::string appBundlePath()
{
    return ui_automation::currentAppBundlePath();
}

bool startFileDrag(eacp::Graphics::View& view, const std::string& path)
{
    auto* nsView = (__bridge NSView*) view.getHandle();
    auto* event = NSApp.currentEvent;

    if (nsView == nil || event == nil || path.empty())
        return false;

    static auto* source = [RudderFileDragSource new];

    auto* filePath = [NSString stringWithUTF8String:path.c_str()];
    auto* item = [[NSDraggingItem alloc]
        initWithPasteboardWriter:[NSURL fileURLWithPath:filePath]];

    auto* image = [NSWorkspace.sharedWorkspace iconForFile:filePath];
    image.size = NSMakeSize(64, 64);

    auto at = [nsView convertPoint:event.locationInWindow fromView:nil];
    [item setDraggingFrame:NSMakeRect(at.x - 32, at.y - 32, 64, 64) contents:image];

    return [nsView beginDraggingSessionWithItems:@[item] event:event source:source]
           != nil;
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
                          {
                              if (AXIsProcessTrusted())
                                  refreshFrontWindow(app.processIdentifier);

                              shared->onActivated(describe(app));
                          }
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
                          {
                              forgetFrontWindow(app.processIdentifier);
                              shared->onTerminated(describe(app));
                          }
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
