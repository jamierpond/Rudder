#include "QuickWindow.h"

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#include <tamber_apps/Apps/ReopenHandler.h>

#include <utility>

namespace tamber::quick_window
{
namespace
{
std::function<void(const char*)> logger;

void logEvent(const char* message)
{
    if (logger)
        logger(message);
}

NSWindow* nativeWindowOf(eacp::Graphics::Window& window)
{
    return static_cast<NSWindow*>(window.getHandle());
}

NSPanel* fullscreenHost = nil;

// A child window won't move on its own performWindowDragWithEvent:, but moving
// its host slides the child with it. Every other window keeps the original
// implementation.
IMP originalPerformWindowDrag = nullptr;

void redirectedPerformWindowDrag(id self, SEL cmd, NSEvent* event)
{
    auto* window = static_cast<NSWindow*>(self);
    if (fullscreenHost != nil && window.parentWindow == fullscreenHost)
        [fullscreenHost performWindowDragWithEvent:event];
    else
        reinterpret_cast<void (*)(id, SEL, NSEvent*)>(originalPerformWindowDrag)(
            self, cmd, event);
}

// A pid rather than the NSRunningApplication, so this file needs no
// retain/release care in either ARC mode.
pid_t rememberedFrontPid = 0;

bool activateRememberedApp()
{
    if (rememberedFrontPid == 0)
        return false;
    auto* app = [NSRunningApplication
        runningApplicationWithProcessIdentifier:rememberedFrontPid];
    if (app == nil || app.terminated)
        return false;
    if (@available(macOS 14.0, *))
        return [app
            activateFromApplication:NSRunningApplication.currentApplication
                            options:0];
    return [app activateWithOptions:0];
}

std::function<void()> reopenHandler;

// While set, around a panel-driven LaunchServices self-open, reopen events are
// OURS and swallowing them keeps the big window down.
double suppressReopenUntil = 0;

void dispatchReopenHandler()
{
    if (CFAbsoluteTimeGetCurrent() < suppressReopenUntil)
    {
        logEvent("reopen suppressed (panel activation escalation)");
        return;
    }
    logEvent("reopen event -> app reopen handler");

    if (reopenHandler)
        reopenHandler();
}
} // namespace

void setLogger(std::function<void(const char*)> log)
{
    logger = std::move(log);
}

void enableWindowDragThroughHost()
{
    static dispatch_once_t once;
    dispatch_once(&once, ^{
      SEL selector = @selector(performWindowDragWithEvent:);
      Method method = class_getInstanceMethod([NSWindow class], selector);
      originalPerformWindowDrag = method_getImplementation(method);
      method_setImplementation(
          method, reinterpret_cast<IMP>(redirectedPerformWindowDrag));
    });
}

void rememberFrontmostApp()
{
    auto* front = NSWorkspace.sharedWorkspace.frontmostApplication;
    if (front == nil
        || front.processIdentifier
               == NSRunningApplication.currentApplication.processIdentifier)
        return;
    rememberedFrontPid = front.processIdentifier;
    auto* name = front.localizedName;
    logEvent([NSString stringWithFormat:@"remembered front app: %@",
                                        name != nil ? name : @"?"]
                 .UTF8String);
}

// macOS calls a background app's plain NSWindow "visible" on a full-screen Space
// yet refuses to COMPOSITE it there; only true NSPanels are. Rebuilding the eacp
// window as a panel would break its show/hide plumbing, so it becomes a CHILD of
// an invisible non-activating one.
void raiseAboveFullScreen(eacp::Graphics::Window& window)
{
    auto* native = nativeWindowOf(window);
    if (native == nil)
        return;

    if (fullscreenHost == nil)
    {
        fullscreenHost = [[NSPanel alloc]
            initWithContentRect:NSMakeRect(0, 0, 1, 1)
                      styleMask:NSWindowStyleMaskBorderless
                                | NSWindowStyleMaskNonactivatingPanel
                        backing:NSBackingStoreBuffered
                          defer:NO];
        fullscreenHost.opaque = NO;
        fullscreenHost.backgroundColor = NSColor.clearColor;
        fullscreenHost.hasShadow = NO;
        fullscreenHost.hidesOnDeactivate = NO;
        // It lingers at pop-up level between shows, so it must never swallow a
        // click meant for the app underneath.
        fullscreenHost.ignoresMouseEvents = YES;
        fullscreenHost.level = NSPopUpMenuWindowLevel;
        fullscreenHost.collectionBehavior =
            NSWindowCollectionBehaviorCanJoinAllSpaces
            | NSWindowCollectionBehaviorFullScreenAuxiliary
            | NSWindowCollectionBehaviorIgnoresCycle;
    }

    native.level = NSPopUpMenuWindowLevel;
    native.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
        | NSWindowCollectionBehaviorFullScreenAuxiliary
        | NSWindowCollectionBehaviorIgnoresCycle;
    native.hidesOnDeactivate = NO;

    [fullscreenHost setFrameOrigin:native.frame.origin];
    [fullscreenHost orderFrontRegardless];

    // An orderOut can sever the child link, so re-adopt on every show.
    if (native.parentWindow != fullscreenHost)
        [fullscreenHost addChildWindow:native ordered:NSWindowAbove];

    [native orderFrontRegardless];
}

void takeKeyForTyping(eacp::Graphics::Window& window)
{
    auto* native = nativeWindowOf(window);
    if (native == nil)
        return;

    [native makeKeyWindow];

    // Activation brings forward BOTH key and main, and borderless windows refuse
    // main by default — so the big window kept that badge and rode along.
    static dispatch_once_t canBecomeMainOnce;
    dispatch_once(&canBecomeMainOnce, ^{
      Class panelClass = NSClassFromString(@"EacpKeyableBorderlessWindow");
      if (panelClass != nil)
          class_addMethod(panelClass,
                          @selector(canBecomeMainWindow),
                          imp_implementationWithBlock(^BOOL(id) { return YES; }),
                          "c@:");
    });
    [native makeMainWindow];
    logEvent("reveal: panel is key + main");
}

void activateForPanel()
{
    if (NSApp.isActive)
    {
        logEvent("activateForPanel: already active");
        return;
    }

    logEvent("activateForPanel: requesting cooperative activation");
    if (@available(macOS 14.0, *))
        [NSApp activate];
    else
        [NSApp activateIgnoringOtherApps:YES];

    // Cooperative activation is silently declined while the user is "actively
    // working" elsewhere — exactly a hotkey summon over the DAW — so escalate to
    // a LaunchServices open, which counts as a user-level activation.
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, (int64_t) (0.3 * NSEC_PER_SEC)),
        dispatch_get_main_queue(),
        ^{
          if (NSApp.isActive)
          {
              logEvent("activateForPanel: cooperative won");
              return;
          }

          auto* bundle = NSBundle.mainBundle;
          if (![bundle.bundlePath.pathExtension isEqualToString:@"app"])
          {
              logEvent("activateForPanel: not a bundle, giving up");
              return;
          }

          logEvent("activateForPanel: escalating via LaunchServices");
          suppressReopenUntil = CFAbsoluteTimeGetCurrent() + 3.0;
          auto* configuration = [NSWorkspaceOpenConfiguration configuration];
          configuration.activates = YES;
          [NSWorkspace.sharedWorkspace openApplicationAtURL:bundle.bundleURL
                                              configuration:configuration
                                          completionHandler:nil];
        });
}

// A key window ordering out while the app is active makes AppKit promote the
// next visible one — the big window. [NSApp deactivate] is ASYNCHRONOUS, so
// hiding on the next line still promotes; hence the wait for resign-active,
// with a fallback timer in case the system declines the handoff.
void hideYieldingActivation(eacp::Graphics::Window& window,
                            std::function<void()> hide)
{
    auto* native = nativeWindowOf(window);
    if (native == nil || !native.isKeyWindow || !NSApp.isActive)
    {
        logEvent("hideYieldingActivation: immediate hide");
        hide();
        return;
    }

    auto sharedHide = std::make_shared<std::function<void()>>(std::move(hide));
    auto done = std::make_shared<bool>(false);
    auto observer = std::make_shared<id>(nil);

    auto runOnce = [sharedHide, done, observer](const char* reason)
    {
        if (*done)
            return;
        *done = true;
        if (*observer != nil)
            [NSNotificationCenter.defaultCenter removeObserver:*observer];
        logEvent(reason);
        (*sharedHide)();
    };

    *observer = [NSNotificationCenter.defaultCenter
        addObserverForName:NSApplicationDidResignActiveNotification
                    object:NSApp
                     queue:NSOperationQueue.mainQueue
                usingBlock:^(NSNotification*)
                { runOnce("hideYieldingActivation: hid after resign"); }];

    // Hand focus straight back to the app the user summoned the panel over, so
    // a copy lands ready to paste. Deactivate only guesses at that app.
    if (activateRememberedApp())
        logEvent("hideYieldingActivation: handed off to front app");
    else
    {
        logEvent("hideYieldingActivation: deactivating (no handoff)");
        [NSApp deactivate];
    }

    // If the system declines to deactivate us, the panel must still hide —
    // promotion is then the lesser evil.
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, (int64_t) (0.25 * NSEC_PER_SEC)),
        dispatch_get_main_queue(),
        ^{ runOnce("hideYieldingActivation: fallback hide"); });
}

// Never install an NSApplicationDelegate here: a process has exactly one and the
// last writer wins, which used to unhook eacp's applicationShouldTerminate: and
// with it every Apps::setQuitHandler. Tamber::Apps also keeps a copy this
// process can fire itself (Apps::requestReopen).
void installReopenHandler(std::function<void()> onReopen)
{
    reopenHandler = std::move(onReopen);
    Tamber::Apps::installReopenHandler(dispatchReopenHandler);
}
} // namespace tamber::quick_window
