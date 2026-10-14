#include "QuickWindow.h"

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/WinInclude.h>

#include <thread>
#include <utility>

namespace rudder::quick_window
{
namespace
{
constexpr auto* instanceMutexName = L"Local\\com.pond.rudder.instance";
constexpr auto* reopenEventName = L"Local\\com.pond.rudder.reopen";

std::function<void(const char*)> logger;

void logEvent(const char* message)
{
    if (logger)
        logger(message);
}

HWND nativeWindowOf(eacp::Graphics::Window& window)
{
    return (HWND) window.getHandle();
}

// The window the panel was summoned over, or the one Enter picked: where the
// keyboard goes when the panel hides.
HWND rememberedWindow = nullptr;

HWND panelWindow = nullptr;

// The process's frontmost window, in z-order: the one it was last using.
HWND frontWindowOf(DWORD pid)
{
    for (auto window = GetTopWindow(nullptr); window != nullptr;
         window = GetWindow(window, GW_HWNDNEXT))
    {
        auto owner = DWORD {0};
        GetWindowThreadProcessId(window, &owner);

        if (owner == pid && IsWindowVisible(window)
            && GetWindow(window, GW_OWNER) == nullptr)
            return window;
    }

    return nullptr;
}

// Hiding the foreground window lets Windows pick the next one itself, which
// is whatever a preview left on top. Handing the foreground over first keeps
// that choice ours. Rudder owns the foreground while the panel is up, so the
// handoff is always allowed.
bool activateRememberedWindow()
{
    if (rememberedWindow == nullptr || !IsWindow(rememberedWindow))
        return false;

    if (IsIconic(rememberedWindow))
        ShowWindowAsync(rememberedWindow, SW_RESTORE);

    return SetForegroundWindow(rememberedWindow) != 0;
}

// A second Rudder.exe -- the Start menu, a pinned icon -- is the reopen:
// it signals this event and leaves, and the Rudder already running summons
// its panel, which is what reopening the app does on macOS.
void waitForReopens(HANDLE reopen, std::function<void()> onReopen)
{
    while (WaitForSingleObject(reopen, INFINITE) == WAIT_OBJECT_0)
        eacp::Threads::callAsync(onReopen);
}
} // namespace

void setLogger(std::function<void(const char*)> log)
{
    logger = std::move(log);
}

// Dragging a borderless window moves the window itself here; there is no host
// window for it to ride.
void enableWindowDragThroughHost() {}

void rememberAppForHandoff(int pid)
{
    rememberedWindow = pid != 0 ? frontWindowOf((DWORD) pid) : nullptr;
}

void rememberFrontmostApp()
{
    auto front = GetForegroundWindow();
    auto pid = DWORD {0};
    GetWindowThreadProcessId(front, &pid);

    if (front == nullptr || pid == GetCurrentProcessId())
        return;

    rememberedWindow = front;
    logEvent("remembered front window");
}

// Topmost, and a tool window: no taskbar button and no Alt+Tab entry for a
// panel that only lives while it is summoned. The extended style only takes
// effect on the taskbar across a hide, so the first summon pays for one.
void raiseAboveFullScreen(eacp::Graphics::Window& window)
{
    auto native = nativeWindowOf(window);

    if (native == nullptr)
        return;

    auto exStyle = GetWindowLongPtrW(native, GWL_EXSTYLE);

    if ((exStyle & WS_EX_TOOLWINDOW) == 0)
    {
        ShowWindow(native, SW_HIDE);
        SetWindowLongPtrW(
            native, GWL_EXSTYLE, (exStyle | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW);
        ShowWindow(native, SW_SHOWNOACTIVATE);
    }

    SetWindowPos(native,
                 HWND_TOPMOST,
                 0,
                 0,
                 0,
                 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// A window has no key/main split here: the foreground window gets the
// keyboard. The hotkey that summoned the panel is Rudder's input, which is
// what lets it take the foreground.
void takeKeyForTyping(eacp::Graphics::Window& window)
{
    panelWindow = nativeWindowOf(window);
    window.toFront();
    logEvent("reveal: panel is foreground");
}

// The foreground lock can still refuse -- a summon from the tray menu, or
// input that went elsewhere since the hotkey. SwitchToThisWindow is what
// Alt+Tab uses, and it isn't refused.
void activateForPanel()
{
    if (panelWindow == nullptr || GetForegroundWindow() == panelWindow)
    {
        logEvent("activateForPanel: already foreground");
        return;
    }

    logEvent("activateForPanel: escalating via SwitchToThisWindow");
    SwitchToThisWindow(panelWindow, TRUE);
}

void hideYieldingActivation(eacp::Graphics::Window& window,
                            std::function<void()> hide)
{
    auto native = nativeWindowOf(window);

    if (native != nullptr && GetForegroundWindow() == native
        && activateRememberedWindow())
        logEvent("hideYieldingActivation: handed off to front window");
    else
        logEvent("hideYieldingActivation: immediate hide");

    hide();
}

void installReopenHandler(std::function<void()> onReopen)
{
    // Held for the life of the process; the system drops it with the process.
    auto instance = CreateMutexW(nullptr, FALSE, instanceMutexName);
    auto alreadyRunning = GetLastError() == ERROR_ALREADY_EXISTS;
    auto reopen = CreateEventW(nullptr, FALSE, FALSE, reopenEventName);

    if (instance == nullptr || reopen == nullptr)
        return;

    if (alreadyRunning)
    {
        logEvent("another Rudder is running: handing it the reopen");
        SetEvent(reopen);
        ExitProcess(0);
    }

    std::thread(waitForReopens, reopen, std::move(onReopen)).detach();
}
} // namespace rudder::quick_window
