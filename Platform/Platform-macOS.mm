#include "Platform.h"
#include "Permissions.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <CoreServices/CoreServices.h>

#include <os/log.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <set>
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

// Where the preview and activation AX work runs, in order, so a slow or hung
// target app costs the panel nothing. The front window cache and the preview
// origin belong to it.
dispatch_queue_t axQueue()
{
    static auto* queue = dispatch_queue_create("rudder.ax", DISPATCH_QUEUE_SERIAL);
    return queue;
}

// AX says Rudder has lost accessibility -- an in-place rebuild or a tccutil
// reset while running. Key capture must stop at once (defined with it).
void noteAXError(AXError error);

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

    if (window != nullptr)
        AXUIElementSetMessagingTimeout((AXUIElementRef) window, axTimeoutSeconds);

    return (AXUIElementRef) window;
}

std::map<std::string, eacp::Graphics::Image>& iconCache()
{
    static auto cache = std::map<std::string, eacp::Graphics::Image> {};
    return cache;
}

// axQueue only.
AXUIElementRef previewOrigin = nullptr;

// axQueue only. Each app's front window element, so a preview raise is one
// AX call rather than a walk to find the window first. Refreshed when the app activates (its
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
    {
        NSLog(@"Rudder: preview raise failed for pid %d: AXError %d", pid, (int) error);
        noteAXError(error);
    }

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

eacp::Graphics::Image renderIcon(NSImage* icon);

const eacp::Graphics::Image& appIcon(const RunningApp& app)
{
    auto& cache = iconCache();

    if (auto found = cache.find(app.id); found != cache.end())
        return found->second;

    auto& image = cache[app.id];
    image = renderIcon(lookUp(app).icon);
    return image;
}

eacp::Graphics::Image renderIcon(NSImage* icon)
{
    auto image = eacp::Graphics::Image {};

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

    return eacp::Graphics::Image(size, size, std::move(pixels));
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
        auto pid = app.pid;

        dispatch_async(axQueue(),
                       ^{
                         auto appElement = AXUIElementCreateApplication(pid);
                         AXUIElementSetMessagingTimeout(appElement, axTimeoutSeconds);
                         AXUIElementSetAttributeValue(
                             appElement, kAXFrontmostAttribute, kCFBooleanTrue);
                         CFRelease(appElement);

                         raiseFrontWindow(pid);
                       });

        activated = true;
    }

    return activated;
}

// --- Key capture ---------------------------------------------------------------
//
// The tap is the one piece of Rudder that can hold up every key in the login
// session, so it lives on its own thread and its callback only reads atomics:
// no AppKit, no AX, no locks the main thread could be holding. Swallowed keys
// reach the main thread by dispatch_async.

namespace
{
// Shared with the tap thread.
std::atomic<bool> captureOn {false};
std::atomic<bool> capturePanelShown {false};
std::atomic<CFMachPortRef> captureTap {nullptr};
std::atomic<std::uint64_t> swallowedKeys {0};
std::atomic<std::uint64_t> handledKeys {0};
std::atomic<std::uint64_t> lastKeyProgress {0};
std::atomic<bool> abortQueued {false};

constexpr std::size_t maxPassThrough = 4;
std::array<std::atomic<std::uint32_t>, maxPassThrough> passThroughKeys {};
std::atomic<std::size_t> passThroughCount {0};

// RUDDER_DEBUG_STALL_TAP_MS: the tap thread itself sleeps on each key press,
// so the OS times the tap out -- for proving that is honoured.
const int tapStallMs = []
{
    auto* text = std::getenv("RUDDER_DEBUG_STALL_TAP_MS");
    return text != nullptr ? std::atoi(text) : 0;
}();

// Main thread only.
KeyCapture activeCapture;
__weak NSWindow* capturePanel = nil;
id mouseMonitor = nil;
NSTimer* captureWatch = nil;
CFRunLoopSourceRef captureSource = nullptr;
CFRunLoopRef tapRunLoop = nullptr;
std::atomic<bool> trustProbeInFlight {false};

std::uint64_t uptimeNanos()
{
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

std::uint8_t captureModifiersOf(CGEventFlags flags)
{
    auto modifiers = std::uint8_t {0};

    if ((flags & kCGEventFlagMaskControl) != 0)
        modifiers |= captureModifiers::control;
    if ((flags & kCGEventFlagMaskAlternate) != 0)
        modifiers |= captureModifiers::option;
    if ((flags & kCGEventFlagMaskShift) != 0)
        modifiers |= captureModifiers::shift;
    if ((flags & kCGEventFlagMaskCommand) != 0)
        modifiers |= captureModifiers::command;

    return modifiers;
}

std::uint32_t pack(CaptureKey key)
{
    return ((std::uint32_t) key.keyCode << 8) | key.modifiers;
}

CaptureKey unpack(std::uint32_t packed)
{
    return {(std::uint16_t) (packed >> 8), (std::uint8_t) (packed & 0xff)};
}

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

void disableTap()
{
    captureOn = false;

    if (auto tap = captureTap.load())
        CGEventTapEnable(tap, false);
}

void endCapture()
{
    disableTap();
    capturePanelShown = false;

    if (mouseMonitor != nil)
    {
        [NSEvent removeMonitor:mouseMonitor];
        mouseMonitor = nil;
    }

    [captureWatch invalidate];
    captureWatch = nil;
    capturePanel = nil;
}

// Main thread. Capture has stopped itself; the app drops the panel.
void abortCapture(const std::string& reason)
{
    if (!activeCapture.onKey)
        return;

    NSLog(@"Rudder: key capture aborted: %s", reason.c_str());

    auto onAbort = std::move(activeCapture.onAbort);
    endCapture();
    activeCapture = {};

    if (onAbort)
        onAbort(reason);
}

// Any thread, at most one in flight.
void queueAbort(const char* reason)
{
    if (abortQueued.exchange(true))
        return;

    dispatch_async(dispatch_get_main_queue(),
                   ^{
                     abortQueued = false;
                     abortCapture(reason);
                   });
}

// Main thread, for each key the tap took. When Rudder is the one that is
// stuck, keys pile up here and run late -- by then capture is off and they
// are dropped.
void handleCapturedKey(CGEventType type, CGEventRef event)
{
    if (captureOn && activeCapture.onKey)
        activeCapture.onKey(toKeyEvent(type, event));

    handledKeys += 1;
    lastKeyProgress = uptimeNanos();
}

CaptureEvent classify(CGEventType type)
{
    switch (type)
    {
        case kCGEventKeyDown:
            return CaptureEvent::keyDown;
        case kCGEventKeyUp:
            return CaptureEvent::keyUp;
        case kCGEventTapDisabledByTimeout:
            return CaptureEvent::tapTimedOut;
        case kCGEventTapDisabledByUserInput:
            return CaptureEvent::tapDisabledByUser;
        default:
            return CaptureEvent::other;
    }
}

// The tap thread. O(1): atomics, the event's own fields, a copy and a
// dispatch_async.
CGEventRef captureCallback(CGEventTapProxy, CGEventType type, CGEventRef event, void*)
{
    auto now = uptimeNanos();
    auto kind = classify(type);
    auto key = std::optional<CaptureKey> {};

    if (kind == CaptureEvent::keyDown || kind == CaptureEvent::keyUp)
        key = CaptureKey {
            (std::uint16_t) CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode),
            captureModifiersOf(CGEventGetFlags(event))};

    auto backlog = CaptureBacklog {};
    backlog.swallowed = swallowedKeys;
    backlog.handled = handledKeys;
    backlog.lastProgress = lastKeyProgress;

    auto state = CaptureState {};
    state.capturing = captureOn;
    state.panelShown = capturePanelShown;
    state.stalled = mainStalled(backlog, now);

    auto keys = std::array<CaptureKey, maxPassThrough> {};
    auto count = std::min(passThroughCount.load(), maxPassThrough);

    for (std::size_t i = 0; i < count; ++i)
        keys[i] = unpack(passThroughKeys[i]);

    auto verdict = decideCapture(kind, state, key, std::span {keys.data(), count});

    if (tapStallMs > 0 && verdict == CaptureVerdict::swallow
        && kind == CaptureEvent::keyDown)
        usleep((useconds_t) tapStallMs * 1000);

    switch (verdict)
    {
        case CaptureVerdict::pass:
            return event;

        case CaptureVerdict::passAndStop:
            disableTap();
            return event;

        case CaptureVerdict::passAndAbort:
        {
            disableTap();

            auto* reason = kind == CaptureEvent::tapTimedOut ? "the OS timed the tap out"
                           : kind == CaptureEvent::tapDisabledByUser
                               ? "the OS disabled the tap (user input)"
                               : "the main thread stopped taking keys";
            os_log_error(OS_LOG_DEFAULT, "Rudder: key capture off: %{public}s", reason);
            queueAbort(reason);
            return event;
        }

        case CaptureVerdict::swallow:
            break;
    }

    // A fresh backlog starts the stall clock now.
    if (handledKeys.load() >= swallowedKeys.load())
        lastKeyProgress = now;

    swallowedKeys += 1;

    auto copy = CGEventCreateCopy(event);

    dispatch_async(dispatch_get_main_queue(),
                   ^{
                     handleCapturedKey(type, copy);
                     CFRelease(copy);
                   });

    return nullptr;
}

// The tap's thread: a run loop of its own, started once, idle (blocked in
// mach_msg) whenever the tap is off.
CFRunLoopRef startTapThread()
{
    static dispatch_once_t once;

    dispatch_once(&once,
                  ^{
                    auto ready = dispatch_semaphore_create(0);

                    auto* thread = [[NSThread alloc] initWithBlock:^{
                      tapRunLoop = CFRunLoopGetCurrent();
                      [NSRunLoop.currentRunLoop addPort:NSMachPort.port
                                                forMode:NSDefaultRunLoopMode];
                      dispatch_semaphore_signal(ready);

                      while (true)
                          [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode
                                                 beforeDate:NSDate.distantFuture];
                    }];

                    thread.name = @"rudder.key-capture";
                    thread.qualityOfService = NSQualityOfServiceUserInteractive;
                    [thread start];
                    dispatch_semaphore_wait(ready, DISPATCH_TIME_FOREVER);
                  });

    return tapRunLoop;
}

bool ensureTap()
{
    auto tap = captureTap.load();

    if (tap != nullptr && CFMachPortIsValid(tap))
        return true;

    auto runLoop = startTapThread();

    if (tap != nullptr)
    {
        captureTap = nullptr;
        CFRunLoopRemoveSource(runLoop, captureSource, kCFRunLoopCommonModes);
        CFRelease(captureSource);
        captureSource = nullptr;
        CFMachPortInvalidate(tap);
        CFRelease(tap);
    }

    // Keys only. Clicks go to a passive monitor, so the mouse can never wait
    // on Rudder.
    auto mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp);

    tap = CGEventTapCreate(kCGSessionEventTap,
                           kCGHeadInsertEventTap,
                           kCGEventTapOptionDefault,
                           mask,
                           captureCallback,
                           nullptr);

    if (tap == nullptr)
        return false;

    // Created enabled; nothing may be swallowed before start says so.
    CGEventTapEnable(tap, false);

    captureSource = CFMachPortCreateRunLoopSource(nullptr, tap, 0);
    CFRunLoopAddSource(runLoop, captureSource, kCFRunLoopCommonModes);
    CFRunLoopWakeUp(runLoop);
    captureTap = tap;
    return true;
}

// Main thread, twice a second while capture is on (never otherwise): the
// panel must still be on screen and Rudder still trusted. AXIsProcessTrusted
// can keep saying yes after a rebuild in place, so AX itself is asked, off
// the main thread.
void watchCapture()
{
    auto* panel = capturePanel;

    if (panel == nil || !panel.isVisible)
    {
        capturePanelShown = false;
        abortCapture("the panel is no longer on screen");
        return;
    }

    if (!AXIsProcessTrusted())
    {
        abortCapture("accessibility was revoked");
        return;
    }

    if (trustProbeInFlight.exchange(true))
        return;

    dispatch_async(axQueue(),
                   ^{
                     auto systemWide = AXUIElementCreateSystemWide();
                     AXUIElementSetMessagingTimeout(systemWide, 0.1f);
                     CFTypeRef focused = nullptr;
                     auto error = AXUIElementCopyAttributeValue(
                         systemWide, kAXFocusedApplicationAttribute, &focused);

                     if (focused != nullptr)
                         CFRelease(focused);

                     CFRelease(systemWide);
                     trustProbeInFlight = false;
                     noteAXError(error);
                   });
}

void setPassThrough(const std::vector<CaptureKey>& passThrough)
{
    auto count = std::min(passThrough.size(), maxPassThrough);
    passThroughCount = 0;

    for (std::size_t i = 0; i < count; ++i)
        passThroughKeys[i] = pack(passThrough[i]);

    passThroughCount = count;
}
} // namespace

namespace
{
void noteAXError(AXError error)
{
    if (error != kAXErrorAPIDisabled)
        return;

    // Straight off at the tap, then the panel goes.
    if (captureOn)
    {
        disableTap();
        queueAbort("accessibility was revoked (AX API disabled)");
    }
}
} // namespace

bool startKeyCapture(eacp::Graphics::Window& panel,
                     KeyCapture capture,
                     const std::vector<CaptureKey>& passThrough)
{
    endCapture();

    if (!ensureTap())
    {
        NSLog(@"Rudder: key capture unavailable (trusted: %d)", AXIsProcessTrusted());
        activeCapture = {};
        return false;
    }

    activeCapture = std::move(capture);
    capturePanel = (__bridge NSWindow*) panel.getHandle();
    setPassThrough(passThrough);

    mouseMonitor = [NSEvent
        addGlobalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown
                                              | NSEventMaskRightMouseDown
                                      handler:^(NSEvent*) {
                                        if (activeCapture.onMouseDownOutside)
                                            activeCapture.onMouseDownOutside();
                                      }];

    captureWatch = [NSTimer scheduledTimerWithTimeInterval:0.5
                                                   repeats:YES
                                                     block:^(NSTimer*) { watchCapture(); }];
    [NSRunLoop.mainRunLoop addTimer:captureWatch forMode:NSRunLoopCommonModes];

    lastKeyProgress = uptimeNanos();
    capturePanelShown = capturePanel != nil && capturePanel.isVisible;
    captureOn = true;
    CGEventTapEnable(captureTap.load(), true);
    return true;
}

void setKeyCapturePassThrough(const std::vector<CaptureKey>& passThrough)
{
    setPassThrough(passThrough);
}

void stopKeyCapture()
{
    endCapture();
    activeCapture = {};
}

namespace
{
// axQueue only. The preview's activation: AXFrontmost plus a raise of the
// cached front window.
bool previewFrontWindow(pid_t pid, const std::string& name)
{
    auto appElement = AXUIElementCreateApplication(pid);
    AXUIElementSetMessagingTimeout(appElement, axTimeoutSeconds);
    auto error =
        AXUIElementSetAttributeValue(appElement, kAXFrontmostAttribute, kCFBooleanTrue);
    CFRelease(appElement);

    if (error != kAXErrorSuccess)
    {
        NSLog(@"Rudder: preview activation failed for %s: AXError %d",
              name.c_str(),
              (int) error);
        noteAXError(error);
    }

    return raiseFrontWindow(pid) && error == kAXErrorSuccess;
}

// axQueue only. That exact window to the front, its app frontmost.
bool raiseWindow(pid_t pid, AXUIElementRef window, bool unminimize, const std::string& title)
{
    AXUIElementSetMessagingTimeout(window, axTimeoutSeconds);

    if (unminimize)
        AXUIElementSetAttributeValue(window, kAXMinimizedAttribute, kCFBooleanFalse);

    auto appElement = AXUIElementCreateApplication(pid);
    AXUIElementSetMessagingTimeout(appElement, axTimeoutSeconds);
    AXUIElementSetAttributeValue(appElement, kAXFrontmostAttribute, kCFBooleanTrue);
    CFRelease(appElement);

    AXUIElementSetAttributeValue(window, kAXMainAttribute, kCFBooleanTrue);
    auto error = AXUIElementPerformAction(window, kAXRaiseAction);

    if (error != kAXErrorSuccess)
    {
        NSLog(@"Rudder: raising window '%s' failed: AXError %d", title.c_str(), (int) error);
        noteAXError(error);
    }

    return error == kAXErrorSuccess;
}

std::vector<std::string> titlesOf(pid_t pid);

std::atomic<std::uint64_t> latestPreview {0};
} // namespace

void rememberPreviewOrigin(const RunningApp& origin)
{
    auto pid = origin.pid;
    auto trusted = pid != 0 && AXIsProcessTrusted();

    dispatch_async(axQueue(),
                   ^{
                     if (previewOrigin != nullptr)
                     {
                         CFRelease(previewOrigin);
                         previewOrigin = nullptr;
                     }

                     if (trusted)
                     {
                         previewOrigin = cachedFrontWindow(pid);

                         if (previewOrigin != nullptr)
                             CFRetain(previewOrigin);
                     }
                   });
}

namespace
{
// axQueue only.
void raisePreviewOrigin()
{
    if (previewOrigin != nullptr)
        AXUIElementPerformAction(previewOrigin, kAXRaiseAction);
}
} // namespace

void restorePreviewOrigin()
{
    dispatch_async(axQueue(), ^{ raisePreviewOrigin(); });
}

namespace
{
struct CachedWindow
{
    AXUIElementRef element = nullptr;
    AppWindow window;
};

// Windows per pid. Main thread only; the background enumeration builds a
// fresh list and hands it over.
std::map<pid_t, std::vector<CachedWindow>>& windowCache()
{
    static auto cache = std::map<pid_t, std::vector<CachedWindow>> {};
    return cache;
}

void releaseWindows(std::vector<CachedWindow>& windows)
{
    for (auto& cached: windows)
        if (cached.element != nullptr)
            CFRelease(cached.element);

    windows.clear();
}

std::string copyStringAttribute(AXUIElementRef element, CFStringRef attribute)
{
    CFTypeRef value = nullptr;

    if (AXUIElementCopyAttributeValue(element, attribute, &value) != kAXErrorSuccess
        || value == nullptr)
        return {};

    auto text = CFGetTypeID(value) == CFStringGetTypeID()
                    ? toString((__bridge NSString*) value)
                    : std::string {};
    CFRelease(value);
    return text;
}

bool copyBoolAttribute(AXUIElementRef element, CFStringRef attribute)
{
    CFTypeRef value = nullptr;

    if (AXUIElementCopyAttributeValue(element, attribute, &value) != kAXErrorSuccess
        || value == nullptr)
        return false;

    auto result = CFGetTypeID(value) == CFBooleanGetTypeID()
                  && CFBooleanGetValue((CFBooleanRef) value);
    CFRelease(value);
    return result;
}

// Any thread. Retained elements, front to back.
std::vector<CachedWindow> enumerateWindows(pid_t pid)
{
    auto windows = std::vector<CachedWindow> {};
    auto appElement = AXUIElementCreateApplication(pid);
    AXUIElementSetMessagingTimeout(appElement, axTimeoutSeconds);

    CFTypeRef list = nullptr;

    if (AXUIElementCopyAttributeValue(appElement, kAXWindowsAttribute, &list)
            == kAXErrorSuccess
        && list != nullptr)
    {
        auto array = (CFArrayRef) list;

        for (CFIndex i = 0; i < CFArrayGetCount(array); ++i)
        {
            auto element = (AXUIElementRef) CFArrayGetValueAtIndex(array, i);
            auto title = copyStringAttribute(element, kAXTitleAttribute);
            auto subrole = copyStringAttribute(element, kAXSubroleAttribute);

            if (title.empty() || subrole != toString((__bridge NSString*) kAXStandardWindowSubrole))
                continue;

            auto id = [NSString stringWithFormat:@"%lx", (unsigned long) CFHash(element)];
            CFRetain(element);
            windows.push_back(
                {element,
                 {toString(id), title, copyBoolAttribute(element, kAXMinimizedAttribute)}});
        }

        CFRelease(list);
    }

    CFRelease(appElement);
    return windows;
}

std::function<void(const std::string&)> windowsChanged;
std::map<pid_t, AXObserverRef> windowObservers;
std::map<pid_t, std::string> watchedIds;
dispatch_queue_t windowQueue = nullptr;
int watchGeneration = 0;

void observeWindows(pid_t pid);

void storeWindows(pid_t pid, std::vector<CachedWindow> windows)
{
    auto& cache = windowCache();
    releaseWindows(cache[pid]);
    cache[pid] = std::move(windows);
}

void refreshWindowsLater(pid_t pid)
{
    auto generation = watchGeneration;

    dispatch_async(windowQueue,
                   ^{
                     auto windows = std::make_shared<std::vector<CachedWindow>>(
                         enumerateWindows(pid));

                     dispatch_async(dispatch_get_main_queue(),
                                    ^{
                                      if (generation != watchGeneration)
                                      {
                                          releaseWindows(*windows);
                                          return;
                                      }

                                      storeWindows(pid, std::move(*windows));
                                      observeWindows(pid);

                                      if (windowsChanged && watchedIds.contains(pid))
                                          windowsChanged(watchedIds[pid]);
                                    });
                   });
}

void windowNotification(AXObserverRef, AXUIElementRef, CFStringRef, void* context)
{
    refreshWindowsLater((pid_t) (intptr_t) context);
}

// (Re)registers the observer for one app: window created on the app,
// destroyed / retitled / minimised on each window it has now.
void observeWindows(pid_t pid)
{
    if (auto found = windowObservers.find(pid); found != windowObservers.end())
    {
        CFRunLoopRemoveSource(CFRunLoopGetMain(),
                              AXObserverGetRunLoopSource(found->second),
                              kCFRunLoopDefaultMode);
        CFRelease(found->second);
        windowObservers.erase(found);
    }

    AXObserverRef observer = nullptr;

    if (AXObserverCreate(pid, windowNotification, &observer) != kAXErrorSuccess)
        return;

    auto context = (void*) (intptr_t) pid;
    auto appElement = AXUIElementCreateApplication(pid);
    AXObserverAddNotification(observer, appElement, kAXWindowCreatedNotification, context);
    CFRelease(appElement);

    for (auto& cached: windowCache()[pid])
        for (auto name: {kAXUIElementDestroyedNotification,
                         kAXTitleChangedNotification,
                         kAXWindowMiniaturizedNotification,
                         kAXWindowDeminiaturizedNotification})
            AXObserverAddNotification(observer, cached.element, name, context);

    CFRunLoopAddSource(
        CFRunLoopGetMain(), AXObserverGetRunLoopSource(observer), kCFRunLoopDefaultMode);
    windowObservers[pid] = observer;
}
} // namespace

std::vector<AppWindow> cachedWindows(const RunningApp& app)
{
    auto windows = std::vector<AppWindow> {};

    if (auto found = windowCache().find(app.pid); found != windowCache().end())
        for (auto& cached: found->second)
            windows.push_back(cached.window);

    return windows;
}

void watchWindows(const std::vector<RunningApp>& apps,
                  std::function<void(const std::string& appId)> onChanged)
{
    stopWatchingWindows();

    if (!AXIsProcessTrusted())
        return;

    if (windowQueue == nullptr)
        windowQueue = dispatch_queue_create("rudder.windows", DISPATCH_QUEUE_SERIAL);

    windowsChanged = std::move(onChanged);

    for (auto& app: apps)
    {
        watchedIds[app.pid] = app.id;
        refreshWindowsLater(app.pid);
    }
}

void stopWatchingWindows()
{
    ++watchGeneration;
    windowsChanged = nullptr;
    watchedIds.clear();

    for (auto& [pid, observer]: windowObservers)
    {
        CFRunLoopRemoveSource(
            CFRunLoopGetMain(), AXObserverGetRunLoopSource(observer), kCFRunLoopDefaultMode);
        CFRelease(observer);
    }

    windowObservers.clear();
}

namespace
{
// Main thread: the cached element for a window row, retained, or null.
AXUIElementRef copyCachedWindow(pid_t pid, const std::string& windowId, AppWindow& window)
{
    auto found = windowCache().find(pid);

    if (found == windowCache().end())
        return nullptr;

    for (auto& cached: found->second)
    {
        if (cached.window.id != windowId)
            continue;

        window = cached.window;
        CFRetain(cached.element);
        return cached.element;
    }

    return nullptr;
}

// axQueue, or the main thread in --snapshot.
std::vector<std::string> titlesOf(pid_t pid)
{
    auto titles = std::vector<std::string> {};
    auto appElement = AXUIElementCreateApplication(pid);
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
            AXUIElementSetMessagingTimeout(window, axTimeoutSeconds);
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
} // namespace

std::vector<std::string> windowTitles(const RunningApp& app)
{
    if (!AXIsProcessTrusted())
        return {};

    return titlesOf(app.pid);
}

bool focusWindow(const RunningApp& app, const std::string& windowId, bool unminimize)
{
    if (!AXIsProcessTrusted())
        return false;

    auto window = AppWindow {};
    auto element = copyCachedWindow(app.pid, windowId, window);

    if (element == nullptr)
        return false;

    auto pid = app.pid;
    auto target = app;
    auto minimized = unminimize && window.minimized;
    auto title = window.title;

    dispatch_async(axQueue(),
                   ^{
                     auto raised = raiseWindow(pid, element, minimized, title);
                     CFRelease(element);

                     if (!raised)
                         dispatch_async(dispatch_get_main_queue(),
                                        ^{ activateApp(target); });
                   });

    return true;
}

void previewApp(const RunningApp& app,
                const std::string& windowId,
                bool restoreOrigin,
                std::function<void(PreviewResult)> onDone)
{
    auto generation = ++latestPreview;
    auto window = AppWindow {};
    auto element =
        windowId.empty() ? nullptr : copyCachedWindow(app.pid, windowId, window);
    auto byWindow = !windowId.empty();
    auto pid = app.pid;
    auto name = app.name;
    auto title = window.title;
    auto done = std::make_shared<std::function<void(PreviewResult)>>(std::move(onDone));

    dispatch_async(axQueue(),
                   ^{
                     // Keys outran the AX round trips: only the newest
                     // selection is worth showing.
                     if (generation != latestPreview)
                     {
                         if (element != nullptr)
                             CFRelease(element);

                         return;
                     }

                     auto result = std::make_shared<PreviewResult>();

                     if (byWindow)
                         result->raised =
                             element != nullptr && raiseWindow(pid, element, false, title);
                     else
                     {
                         result->raised = previewFrontWindow(pid, name);

                         if (restoreOrigin)
                             raisePreviewOrigin();
                     }

                     if (element != nullptr)
                         CFRelease(element);

                     result->windowTitles = titlesOf(pid);

                     dispatch_async(dispatch_get_main_queue(),
                                    ^{
                                      if (*done)
                                          (*done)(std::move(*result));
                                    });
                   });
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
                              {
                                  auto pid = app.processIdentifier;
                                  dispatch_async(axQueue(), ^{ refreshFrontWindow(pid); });
                              }

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
                              auto pid = app.processIdentifier;
                              dispatch_async(axQueue(), ^{ forgetFrontWindow(pid); });
                              releaseWindows(windowCache()[app.processIdentifier]);
                              windowCache().erase(app.processIdentifier);
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

namespace
{
std::vector<std::string> appFolders()
{
    auto folders = std::vector<std::string> {"/Applications",
                                             "/Applications/Utilities",
                                             "/System/Applications",
                                             "/System/Applications/Utilities"};

    folders.push_back(toString(NSHomeDirectory()) + "/Applications");
    return folders;
}

// Every .app directly in the folders, and one folder down for the vendor
// folders some installers make ("/Applications/Adobe Foo/Foo.app"). First
// bundle id wins.
std::vector<InstalledApp> scanInstalledApps()
{
    auto* files = NSFileManager.defaultManager;
    auto apps = std::vector<InstalledApp> {};
    auto seen = std::set<std::string> {};

    auto add = [&](NSString* path)
    {
        auto* bundle = [NSBundle bundleWithPath:path];
        auto id = toString(bundle.bundleIdentifier);

        if (id.empty() || !seen.insert(id).second)
            return;

        auto* name = [files displayNameAtPath:path];

        if ([name.pathExtension isEqualToString:@"app"])
            name = name.stringByDeletingPathExtension;

        apps.push_back({id, toString(name), toString(path)});
    };

    for (auto& folder: appFolders())
    {
        auto* root = [NSString stringWithUTF8String:folder.c_str()];

        for (NSString* entry in [files contentsOfDirectoryAtPath:root error:nil])
        {
            auto* path = [root stringByAppendingPathComponent:entry];

            if ([entry.pathExtension isEqualToString:@"app"])
            {
                add(path);
                continue;
            }

            auto isFolder = BOOL {NO};

            if ([entry hasPrefix:@"."] || ![files fileExistsAtPath:path isDirectory:&isFolder]
                || !isFolder || [path isEqualToString:@"/Applications/Utilities"])
                continue;

            for (NSString* inner in [files contentsOfDirectoryAtPath:path error:nil])
                if ([inner.pathExtension isEqualToString:@"app"])
                    add([path stringByAppendingPathComponent:inner]);
        }
    }

    return apps;
}

std::function<void(std::vector<InstalledApp>)> installedChanged;
FSEventStreamRef installedStream = nullptr;
dispatch_queue_t scanQueue = nullptr;

void rescanInstalledApps()
{
    dispatch_async(scanQueue,
                   ^{
                     auto apps = scanInstalledApps();
                     auto shared = std::make_shared<std::vector<InstalledApp>>(std::move(apps));

                     dispatch_async(dispatch_get_main_queue(),
                                    ^{
                                      if (installedChanged)
                                          installedChanged(std::move(*shared));
                                    });
                   });
}

// Apps updating themselves write deep inside their bundles all day; only a
// change to a folder itself (an app added, removed or renamed) is a rescan.
bool isFolderLevelChange(const std::string& changed)
{
    auto path = changed;

    while (path.size() > 1 && path.back() == '/')
        path.pop_back();

    for (auto& folder: appFolders())
    {
        if (path == folder)
            return true;

        // A vendor folder one level down.
        if (path.starts_with(folder + "/") && path.find('/', folder.size() + 1) == std::string::npos
            && !path.ends_with(".app"))
            return true;
    }

    return false;
}

void installedEvents(ConstFSEventStreamRef,
                     void*,
                     size_t count,
                     void* paths,
                     const FSEventStreamEventFlags*,
                     const FSEventStreamEventId*)
{
    auto** list = (char**) paths;

    for (size_t i = 0; i < count; ++i)
    {
        if (isFolderLevelChange(list[i]))
        {
            rescanInstalledApps();
            return;
        }
    }
}

std::map<std::string, eacp::Graphics::Image>& bundleIconCache()
{
    static auto cache = std::map<std::string, eacp::Graphics::Image> {};
    return cache;
}
} // namespace

void watchInstalledApps(std::function<void(std::vector<InstalledApp>)> onChange)
{
    installedChanged = std::move(onChange);

    if (scanQueue == nullptr)
        scanQueue = dispatch_queue_create("rudder.installed-apps", DISPATCH_QUEUE_SERIAL);

    if (installedStream == nullptr)
    {
        auto* folders = [NSMutableArray array];

        for (auto& folder: appFolders())
            [folders addObject:[NSString stringWithUTF8String:folder.c_str()]];

        // A two-second latency coalesces an install's burst of writes into one
        // rescan.
        installedStream = FSEventStreamCreate(nullptr,
                                              installedEvents,
                                              nullptr,
                                              (__bridge CFArrayRef) folders,
                                              kFSEventStreamEventIdSinceNow,
                                              2.0,
                                              kFSEventStreamCreateFlagNone);
        FSEventStreamSetDispatchQueue(installedStream, scanQueue);
        FSEventStreamStart(installedStream);
    }

    rescanInstalledApps();
}

namespace
{
NSString* expandedPath(const std::string& path);
}

const eacp::Graphics::Image& bundleIcon(const std::string& id, const std::string& path)
{
    auto& cache = bundleIconCache();

    if (auto found = cache.find(id); found != cache.end())
        return found->second;

    auto& image = cache[id];
    image = renderIcon([NSWorkspace.sharedWorkspace iconForFile:expandedPath(path)]);
    return image;
}

bool quitApp(const RunningApp& app)
{
    auto* running = lookUp(app);
    return running != nil && !isSelf(running) && [running terminate];
}

namespace
{
NSURL* urlFor(const std::string& text)
{
    return [NSURL URLWithString:[NSString stringWithUTF8String:text.c_str()]];
}

NSString* expandedPath(const std::string& path)
{
    return [NSString stringWithUTF8String:path.c_str()].stringByExpandingTildeInPath;
}
} // namespace

bool hasApp(const std::string& bundleId)
{
    return !bundleId.empty()
           && [NSWorkspace.sharedWorkspace
                  URLForApplicationWithBundleIdentifier:[NSString
                                                            stringWithUTF8String:bundleId.c_str()]]
                  != nil;
}

bool openUrl(const std::string& url, const std::string& openWith)
{
    auto* target = urlFor(url);

    if (target == nil)
        return false;

    if (openWith.empty())
        return [NSWorkspace.sharedWorkspace openURL:target];

    auto* app = [NSWorkspace.sharedWorkspace
        URLForApplicationWithBundleIdentifier:[NSString stringWithUTF8String:openWith.c_str()]];

    // Never quietly the default browser instead: the favourite asked for
    // this app.
    if (app == nil)
    {
        NSLog(@"Rudder: no app with bundle id %s for %s", openWith.c_str(), url.c_str());
        return false;
    }

    auto* label = [NSString stringWithFormat:@"%s with %s", url.c_str(), openWith.c_str()];

    [NSWorkspace.sharedWorkspace openURLs:@[target]
                     withApplicationAtURL:app
                            configuration:[NSWorkspaceOpenConfiguration configuration]
                        completionHandler:^(NSRunningApplication*, NSError* error) {
                          if (error != nil)
                              NSLog(@"Rudder: opening %@ failed: %@", label, error);
                        }];
    return true;
}

bool openPath(const std::string& path)
{
    return [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:expandedPath(path)]];
}

std::string handlerFor(const std::string& url, const std::string& openWith)
{
    NSURL* app = nil;

    if (!openWith.empty())
        app = [NSWorkspace.sharedWorkspace
            URLForApplicationWithBundleIdentifier:[NSString stringWithUTF8String:openWith.c_str()]];

    if (app == nil)
        if (auto* target = urlFor(url))
            app = [NSWorkspace.sharedWorkspace URLForApplicationToOpenURL:target];

    return app != nil ? toString(app.path) : std::string {};
}

bool launchApp(const std::string& path)
{
    auto* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    auto* configuration = [NSWorkspaceOpenConfiguration configuration];
    configuration.activates = YES;
    [NSWorkspace.sharedWorkspace openApplicationAtURL:url
                                        configuration:configuration
                                    completionHandler:nil];
    return url != nil;
}

bool isSystemShortcut(std::uint16_t keyCode, const eacp::Graphics::ModifierKeys& modifiers)
{
    auto wanted = UInt32 {0};

    if (modifiers.shift)
        wanted |= shiftKey;
    if (modifiers.control)
        wanted |= controlKey;
    if (modifiers.alt)
        wanted |= optionKey;
    if (modifiers.command)
        wanted |= cmdKey;

    CFArrayRef shortcuts = nullptr;

    if (CopySymbolicHotKeys(&shortcuts) != noErr || shortcuts == nullptr)
        return false;

    auto relevant = UInt32 {shiftKey | controlKey | optionKey | cmdKey};
    auto found = false;

    for (NSDictionary* shortcut in (__bridge NSArray*) shortcuts)
    {
        auto enabled = [shortcut[(__bridge NSString*) kHISymbolicHotKeyEnabled] boolValue];
        auto code = [shortcut[(__bridge NSString*) kHISymbolicHotKeyCode] unsignedIntValue];
        auto mods = [shortcut[(__bridge NSString*) kHISymbolicHotKeyModifiers] unsignedIntValue];

        if (enabled && code == keyCode && (mods & relevant) == wanted)
        {
            found = true;
            break;
        }
    }

    CFRelease(shortcuts);
    return found;
}

void announceStarted()
{
    [NSDistributedNotificationCenter.defaultCenter
        postNotificationName:@"rudder.started"
                      object:nil
                    userInfo:nil
          deliverImmediately:YES];
}

// --- Debug: key flow probe ---------------------------------------------------

namespace
{
std::atomic<std::uint64_t> probeSeen[256];
std::uint64_t probeStart = 0;

constexpr std::int64_t probeTag = 0x5275646400;

CGEventRef probeCallback(CGEventTapProxy, CGEventType type, CGEventRef event, void*)
{
    if (type != kCGEventKeyDown)
        return event;

    auto index = CGEventGetIntegerValueField(event, kCGEventSourceUserData) - probeTag;

    if (index >= 0 && index < 256 && probeSeen[index] == 0)
        probeSeen[index] = uptimeNanos();

    return event;
}
} // namespace

void probeKeyFlow(std::uint16_t keyCode,
                  int count,
                  double interval,
                  std::function<void(const std::string&)> report)
{
    count = std::min(count, 256);

    [NSThread detachNewThreadWithBlock:^{
      // A listen-only tap at the tail: it sees a key only if every tap ahead
      // of it -- Rudder's capture included -- let it through.
      auto tap = CGEventTapCreate(kCGSessionEventTap,
                                  kCGTailAppendEventTap,
                                  kCGEventTapOptionListenOnly,
                                  CGEventMaskBit(kCGEventKeyDown),
                                  probeCallback,
                                  nullptr);

      if (tap == nullptr)
      {
          report("probe: no listen tap (trusted: " + std::to_string(AXIsProcessTrusted())
                 + ")");
          return;
      }

      auto source = CFMachPortCreateRunLoopSource(nullptr, tap, 0);
      auto* thread = [[NSThread alloc] initWithBlock:^{
        CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
        CFRunLoopRun();
      }];
      [thread start];
      usleep(200000);

      auto* eventSource = CGEventSourceCreate(kCGEventSourceStatePrivate);
      probeStart = uptimeNanos();
      auto sent = std::vector<std::uint64_t>((std::size_t) count);

      for (auto i = 0; i < count; ++i)
      {
          sent[(std::size_t) i] = uptimeNanos();

          auto down = CGEventCreateKeyboardEvent(eventSource, keyCode, true);
          auto up = CGEventCreateKeyboardEvent(eventSource, keyCode, false);
          CGEventSetIntegerValueField(down, kCGEventSourceUserData, probeTag + i);
          CGEventSetIntegerValueField(up, kCGEventSourceUserData, probeTag + i);
          CGEventPost(kCGHIDEventTap, down);
          CGEventPost(kCGHIDEventTap, up);
          CFRelease(down);
          CFRelease(up);

          usleep((useconds_t) (interval * 1e6));
      }

      usleep(300000);
      CFRelease(eventSource);

      auto text = std::string {"probe: key flow, t = ms since the first post\n"};

      for (auto i = 0; i < count; ++i)
      {
          auto seen = probeSeen[i].load();
          char line[128];
          std::snprintf(line,
                        sizeof(line),
                        "  #%02d sent %6.0f  %s %6.0f\n",
                        i,
                        (sent[(std::size_t) i] - probeStart) / 1e6,
                        seen != 0 ? "passed at" : "swallowed",
                        seen != 0 ? (seen - probeStart) / 1e6 : 0.0);
          text += line;
      }

      text += "  capture tap enabled now: ";
      auto capture = captureTap.load();
      text += capture != nullptr && CGEventTapIsEnabled(capture) ? "yes" : "no";
      report(text);

      CGEventTapEnable(tap, false);
      CFRelease(source);
      CFRelease(tap);
    }];
}

} // namespace rudder::platform
