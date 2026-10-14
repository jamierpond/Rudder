#include "Platform.h"
#include "Permissions.h"

#include <RudderProtocol.h>

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Logging.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/WinInclude.h>

#include <commctrl.h>
#include <commoncontrols.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <utility>

namespace eacp::Graphics
{
// eacp's own map from its (macOS) key codes to Windows virtual keys, the one
// GlobalHotKey registers with.
int virtualKeyFromKeyCode(uint16_t keyCode);
} // namespace eacp::Graphics

namespace rudder::platform
{
namespace
{
using eacp::Strings::narrow;
using eacp::Strings::widen;

// --- Processes and their windows ---------------------------------------------
//
// Windows has no notion of a running "app": there are processes, and the
// windows they put in the taskbar. An app here is every process of one
// executable that owns such a window, and its id is that executable's file
// name ("chrome.exe") -- stable across updates that move the binary, and
// short enough to write by hand in rudder.json.

std::wstring lowered(std::wstring text)
{
    if (!text.empty())
        CharLowerBuffW(text.data(), (DWORD) text.size());

    return text;
}

std::wstring fileNameOf(const std::wstring& path)
{
    auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring stemOf(const std::wstring& path)
{
    auto name = fileNameOf(path);
    auto dot = name.find_last_of(L'.');
    return dot == std::wstring::npos ? name : name.substr(0, dot);
}

std::string idForExecutable(const std::wstring& path)
{
    return narrow(lowered(fileNameOf(path)));
}

std::wstring processPath(DWORD pid)
{
    auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

    if (process == nullptr)
        return {};

    auto buffer = std::wstring(MAX_PATH, L'\0');
    auto size = (DWORD) buffer.size();

    if (QueryFullProcessImageNameW(process, 0, buffer.data(), &size) == 0)
        size = 0;

    CloseHandle(process);
    buffer.resize(size);
    return buffer;
}

// The name the executable gives itself ("Google Chrome"), from its version
// resource, else its file name. Cached: reading the resource is a file read.
std::string displayNameFor(const std::wstring& path)
{
    static auto names = std::map<std::wstring, std::string> {};

    if (auto found = names.find(path); found != names.end())
        return found->second;

    auto name = narrow(stemOf(path));
    auto ignored = DWORD {0};
    auto size = GetFileVersionInfoSizeW(path.c_str(), &ignored);

    if (size > 0)
    {
        auto data = std::vector<BYTE>(size);

        struct Translation
        {
            WORD language;
            WORD codePage;
        };

        auto* translations = (Translation*) nullptr;
        auto length = UINT {0};

        if (GetFileVersionInfoW(path.c_str(), 0, size, data.data())
            && VerQueryValueW(data.data(),
                              L"\\VarFileInfo\\Translation",
                              (void**) &translations,
                              &length)
            && length >= sizeof(Translation))
        {
            wchar_t key[64];
            swprintf_s(key,
                       L"\\StringFileInfo\\%04x%04x\\FileDescription",
                       translations->language,
                       translations->codePage);

            auto* description = (wchar_t*) nullptr;

            if (VerQueryValueW(data.data(), key, (void**) &description, &length)
                && length > 1 && description != nullptr && description[0] != L'\0')
                name = narrow(description);
        }
    }

    names[path] = name;
    return name;
}

bool isCloaked(HWND window)
{
    auto cloaked = DWORD {0};
    return SUCCEEDED(DwmGetWindowAttribute(
               window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))
           && cloaked != 0;
}

std::wstring classOf(HWND window)
{
    wchar_t name[256] = {};
    GetClassNameW(window, name, 256);
    return name;
}

std::string titleOf(HWND window)
{
    // GetWindowTextW reads another process's title without sending it a
    // message, so a hung app can't stall this.
    auto length = GetWindowTextLengthW(window);

    if (length <= 0)
        return {};

    auto text = std::wstring((std::size_t) length + 1, L'\0');
    text.resize((std::size_t) GetWindowTextW(window, text.data(), length + 1));
    return narrow(text);
}

// The windows the taskbar and Alt+Tab would show: visible, uncloaked, top
// level, owned only if they ask to be listed, and not a tool palette. The
// shell's own desktop and taskbar windows never count.
bool isAppWindow(HWND window)
{
    if (!IsWindowVisible(window) || isCloaked(window))
        return false;

    auto exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    auto listed = (exStyle & WS_EX_APPWINDOW) != 0;

    if (!listed
        && (GetWindow(window, GW_OWNER) != nullptr
            || (exStyle & WS_EX_TOOLWINDOW) != 0
            || (exStyle & WS_EX_NOACTIVATE) != 0))
        return false;

    auto name = classOf(window);
    return name != L"Progman" && name != L"WorkerW" && name != L"Shell_TrayWnd"
           && name != L"Shell_SecondaryTrayWnd";
}

// A Store app's frame belongs to ApplicationFrameHost; the app itself owns
// the core window inside it. That inner process is the app, when it's there
// (a suspended app's core window can be detached, leaving the host).
DWORD ownerOf(HWND window, const std::wstring& path)
{
    auto pid = DWORD {0};
    GetWindowThreadProcessId(window, &pid);

    if (lowered(fileNameOf(path)) != L"applicationframehost.exe")
        return pid;

    struct Search
    {
        DWORD host;
        DWORD inner;
    };

    auto search = Search {pid, 0};

    EnumChildWindows(
        window,
        [](HWND child, LPARAM param) -> BOOL
        {
            auto& state = *(Search*) param;
            auto childPid = DWORD {0};
            GetWindowThreadProcessId(child, &childPid);

            if (childPid == state.host)
                return TRUE;

            state.inner = childPid;
            return FALSE;
        },
        (LPARAM) &search);

    return search.inner != 0 ? search.inner : pid;
}

struct WindowEntry
{
    HWND window = nullptr;
    DWORD pid = 0;
    std::wstring path;
    std::string id;
};

// Every app window, front to back, Rudder's own left out.
std::vector<WindowEntry> appWindows()
{
    auto handles = std::vector<HWND> {};

    EnumWindows(
        [](HWND window, LPARAM param) -> BOOL
        {
            ((std::vector<HWND>*) param)->push_back(window);
            return TRUE;
        },
        (LPARAM) &handles);

    auto self = GetCurrentProcessId();
    auto paths = std::map<DWORD, std::wstring> {};
    auto entries = std::vector<WindowEntry> {};

    for (auto window: handles)
    {
        if (!isAppWindow(window))
            continue;

        auto framePid = DWORD {0};
        GetWindowThreadProcessId(window, &framePid);

        if (framePid == self)
            continue;

        if (!paths.contains(framePid))
            paths[framePid] = processPath(framePid);

        auto pid = ownerOf(window, paths[framePid]);

        if (!paths.contains(pid))
            paths[pid] = processPath(pid);

        auto& path = paths[pid];

        if (path.empty())
            continue;

        entries.push_back({window, pid, path, idForExecutable(path)});
    }

    return entries;
}

RunningApp describe(const WindowEntry& entry)
{
    return {entry.id, displayNameFor(entry.path), (int) entry.pid};
}

// The app's windows, front to back.
std::vector<WindowEntry> windowsOf(const std::string& appId)
{
    auto windows = appWindows();
    std::erase_if(windows,
                  [&](const WindowEntry& entry) { return entry.id != appId; });
    return windows;
}

std::wstring executableOf(const RunningApp& app)
{
    auto path = processPath((DWORD) app.pid);

    if (!path.empty())
        return path;

    auto windows = windowsOf(app.id);
    return windows.empty() ? std::wstring {} : windows.front().path;
}

// --- Foreground -------------------------------------------------------------

// Windows hands the foreground only to the process that had the last input
// -- which a hotkey press counts as, so the summon and back hotkeys can. When
// it still refuses (the input went elsewhere since), joining the foreground
// thread's input queue lends Rudder its right for the call, and
// SwitchToThisWindow, what Alt+Tab itself uses, is the last word.
bool bringToFront(HWND window, bool unminimize)
{
    if (!IsWindow(window))
        return false;

    if (unminimize && IsIconic(window))
        ShowWindowAsync(window, SW_RESTORE);

    if (SetForegroundWindow(window) && GetForegroundWindow() == window)
        return true;

    auto foreground = GetForegroundWindow();
    auto thisThread = GetCurrentThreadId();
    auto foregroundThread = foreground != nullptr
                                ? GetWindowThreadProcessId(foreground, nullptr)
                                : thisThread;
    auto attached = foregroundThread != thisThread
                    && AttachThreadInput(foregroundThread, thisThread, TRUE);

    BringWindowToTop(window);
    SetForegroundWindow(window);

    if (attached)
        AttachThreadInput(foregroundThread, thisThread, FALSE);

    if (GetForegroundWindow() != window)
        SwitchToThisWindow(window, TRUE);

    return GetForegroundWindow() == window;
}

// Into the top of the normal band, right under the (topmost) panel, without
// activating it: the preview never takes the keyboard from the panel.
// HWND_TOP is quietly ignored for another process's window; a round trip
// through the topmost band is not, and leaving it lands the window at the top
// of the normal one. Async, so a hung app's window costs Rudder nothing.
bool raiseBehindPanel(HWND window)
{
    if (!IsWindow(window) || IsIconic(window))
        return false;

    constexpr auto flags = UINT {SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
                                 | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS};

    return SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, flags) != 0
           && SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, flags) != 0;
}

std::string idOfWindow(HWND window)
{
    char text[32];
    std::snprintf(
        text, sizeof(text), "%llx", (unsigned long long) (std::uintptr_t) window);
    return text;
}

HWND windowOfId(const std::string& id)
{
    return (HWND) (std::uintptr_t) std::strtoull(id.c_str(), nullptr, 16);
}

// --- Icons ------------------------------------------------------------------

// A 32bpp icon carries straight alpha in its colour bitmap; an old one
// without alpha says what is transparent in its mask instead.
eacp::Graphics::Image imageFromIcon(HICON icon)
{
    auto image = eacp::Graphics::Image {};
    auto info = ICONINFO {};

    if (icon == nullptr || !GetIconInfo(icon, &info))
        return image;

    auto bitmap = BITMAP {};
    GetObjectW(info.hbmColor != nullptr ? info.hbmColor : info.hbmMask,
               sizeof(bitmap),
               &bitmap);

    auto width = bitmap.bmWidth;
    auto height = info.hbmColor != nullptr ? bitmap.bmHeight : bitmap.bmHeight / 2;

    auto header = BITMAPINFO {};
    header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    header.bmiHeader.biWidth = width;
    header.bmiHeader.biHeight = -height;
    header.bmiHeader.biPlanes = 1;
    header.bmiHeader.biBitCount = 32;
    header.bmiHeader.biCompression = BI_RGB;

    auto count = (std::size_t) width * (std::size_t) height;
    auto colour = std::vector<std::uint32_t>(count);
    auto mask = std::vector<std::uint32_t>(count);
    auto dc = GetDC(nullptr);

    auto read =
        info.hbmColor != nullptr
        && GetDIBits(
               dc, info.hbmColor, 0, height, colour.data(), &header, DIB_RGB_COLORS)
               == height;
    GetDIBits(dc, info.hbmMask, 0, height, mask.data(), &header, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);

    if (info.hbmColor != nullptr)
        DeleteObject(info.hbmColor);

    DeleteObject(info.hbmMask);

    if (!read || width <= 0 || height <= 0)
        return image;

    auto hasAlpha =
        std::any_of(colour.begin(),
                    colour.end(),
                    [](std::uint32_t pixel) { return (pixel >> 24) != 0; });

    auto pixels = eacp::Graphics::ImageData(count * 4);

    for (std::size_t i = 0; i < count; ++i)
    {
        auto pixel = colour[i];
        auto alpha =
            hasAlpha ? (pixel >> 24) : ((mask[i] & 0xffffff) != 0 ? 0u : 255u);

        pixels[i * 4 + 0] = (std::uint8_t) ((pixel >> 16) & 0xff);
        pixels[i * 4 + 1] = (std::uint8_t) ((pixel >> 8) & 0xff);
        pixels[i * 4 + 2] = (std::uint8_t) (pixel & 0xff);
        pixels[i * 4 + 3] = (std::uint8_t) alpha;
    }

    return eacp::Graphics::Image(width, height, std::move(pixels));
}

// The shell's 48px icon for any file -- an exe, a shortcut (its target's
// icon, no arrow), a folder, a document.
eacp::Graphics::Image renderIcon(const std::wstring& path)
{
    if (path.empty())
        return {};

    auto info = SHFILEINFOW {};

    if (SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_SYSICONINDEX)
        == 0)
        return {};

    auto* list = (IImageList*) nullptr;

    if (FAILED(SHGetImageList(SHIL_EXTRALARGE, IID_IImageList, (void**) &list)))
        return {};

    auto icon = HICON {};
    list->GetIcon(info.iIcon, ILD_TRANSPARENT, &icon);
    list->Release();

    auto image = imageFromIcon(icon);

    if (icon != nullptr)
        DestroyIcon(icon);

    return image;
}

std::map<std::string, eacp::Graphics::Image>& iconCache()
{
    static auto cache = std::map<std::string, eacp::Graphics::Image> {};
    return cache;
}

std::map<std::string, eacp::Graphics::Image>& bundleIconCache()
{
    static auto cache = std::map<std::string, eacp::Graphics::Image> {};
    return cache;
}

// --- Window events ----------------------------------------------------------
//
// WinEvent hooks out of context: the system queues each event to Rudder's
// main thread and runs nothing inside the other apps. Coalesced, so a burst
// (an app opening a dozen windows) is one look at the window list.

struct WinEventHooks
{
    std::vector<HWINEVENTHOOK> hooks;

    void add(DWORD first, DWORD last, WINEVENTPROC callback)
    {
        auto hook = SetWinEventHook(first,
                                    last,
                                    nullptr,
                                    callback,
                                    0,
                                    0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

        if (hook != nullptr)
            hooks.push_back(hook);
    }

    void clear()
    {
        for (auto hook: hooks)
            UnhookWinEvent(hook);

        hooks.clear();
    }
};

bool isTopLevelEvent(HWND window, LONG object, LONG child)
{
    return window != nullptr && object == OBJID_WINDOW && child == CHILDID_SELF;
}

// --- Workspace --------------------------------------------------------------

std::shared_ptr<WorkspaceEvents> workspaceEvents;
WinEventHooks workspaceHooks;
std::map<std::string, RunningApp> knownApps;
std::string lastActivated;
bool appsRecountPending = false;

std::map<std::string, RunningApp> appsById()
{
    auto apps = std::map<std::string, RunningApp> {};

    for (auto& entry: appWindows())
        if (!apps.contains(entry.id))
            apps[entry.id] = describe(entry);

    return apps;
}

// Launches and quits are read off the window list: an app arrives with its
// first taskbar window and leaves with its last.
void recountApps()
{
    appsRecountPending = false;

    if (!workspaceEvents)
        return;

    auto events = workspaceEvents;
    auto now = appsById();
    auto before = std::exchange(knownApps, now);

    for (auto& [id, app]: now)
        if (!before.contains(id))
            events->onLaunched(app);

    for (auto& [id, app]: before)
        if (!now.contains(id))
            events->onTerminated(app);
}

void recountAppsSoon()
{
    if (appsRecountPending)
        return;

    appsRecountPending = true;
    eacp::Threads::callAfter(eacp::Time::MS {250}, recountApps);
}

void CALLBACK onForeground(
    HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child, DWORD, DWORD)
{
    if (!workspaceEvents || !isTopLevelEvent(window, object, child))
        return;

    // Each window of the app reports; only a change of app is an activation.
    for (auto& entry: appWindows())
    {
        if (entry.window != window)
            continue;

        if (entry.id != lastActivated)
        {
            lastActivated = entry.id;
            workspaceEvents->onActivated(describe(entry));
        }

        return;
    }
}

void CALLBACK onWindowListChange(
    HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child, DWORD, DWORD)
{
    if (isTopLevelEvent(window, object, child)
        && GetAncestor(window, GA_PARENT) == GetDesktopWindow())
        recountAppsSoon();
}

// --- Watched windows ---------------------------------------------------------

struct CachedWindow
{
    HWND window = nullptr;
    AppWindow row;
};

// By app id. Main thread only.
std::map<std::string, std::vector<CachedWindow>>& windowCache()
{
    static auto cache = std::map<std::string, std::vector<CachedWindow>> {};
    return cache;
}

std::function<void(const std::string&)> windowsChanged;
std::set<std::string> watchedIds;
WinEventHooks windowHooks;
bool windowsRefreshPending = false;

void refreshWatchedWindows()
{
    windowsRefreshPending = false;

    if (!windowsChanged)
        return;

    auto fresh = std::map<std::string, std::vector<CachedWindow>> {};

    for (auto& entry: appWindows())
    {
        if (!watchedIds.contains(entry.id))
            continue;

        auto title = titleOf(entry.window);

        if (title.empty())
            continue;

        fresh[entry.id].push_back(
            {entry.window,
             {idOfWindow(entry.window), title, IsIconic(entry.window) != 0}});
    }

    auto changed = std::vector<std::string> {};

    for (auto& id: watchedIds)
    {
        auto& now = fresh[id];
        auto& was = windowCache()[id];

        auto same = now.size() == was.size()
                    && std::equal(now.begin(),
                                  now.end(),
                                  was.begin(),
                                  [](const CachedWindow& a, const CachedWindow& b)
                                  {
                                      return a.window == b.window
                                             && a.row.title == b.row.title
                                             && a.row.minimized == b.row.minimized;
                                  });

        if (!same)
            changed.push_back(id);

        was = std::move(now);
    }

    for (auto& id: changed)
        if (windowsChanged)
            windowsChanged(id);
}

void refreshWatchedWindowsSoon()
{
    if (windowsRefreshPending)
        return;

    windowsRefreshPending = true;
    eacp::Threads::callAfter(eacp::Time::MS {50}, refreshWatchedWindows);
}

void CALLBACK onWatchedWindowEvent(
    HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child, DWORD, DWORD)
{
    if (isTopLevelEvent(window, object, child))
        refreshWatchedWindowsSoon();
}

HWND cachedWindow(const RunningApp& app, const std::string& windowId, AppWindow& row)
{
    auto found = windowCache().find(app.id);

    if (found == windowCache().end())
        return nullptr;

    for (auto& cached: found->second)
    {
        if (cached.row.id != windowId)
            continue;

        row = cached.row;
        return IsWindow(cached.window) ? cached.window : nullptr;
    }

    return nullptr;
}

std::vector<std::string> titlesOf(const std::string& appId)
{
    auto titles = std::vector<std::string> {};

    for (auto& entry: windowsOf(appId))
        if (auto title = titleOf(entry.window); !title.empty())
            titles.push_back(std::move(title));

    return titles;
}

// --- Key capture ------------------------------------------------------------
//
// macOS needs a tap because a preview activates the app it shows, taking the
// keyboard with it. Here a preview only reorders windows under the panel, so
// the panel keeps the keyboard and its own key events are all there is to
// read. What is left of capture is noticing the user going elsewhere -- a
// click in another window, Alt+Tab, the Start menu -- which is any other
// window taking the foreground.

KeyCapture activeCapture;
HWND capturePanel = nullptr;
HWINEVENTHOOK captureHook = nullptr;

void CALLBACK
    onCaptureForeground(HWINEVENTHOOK, DWORD, HWND window, LONG, LONG, DWORD, DWORD)
{
    if (capturePanel == nullptr || window == capturePanel || window == nullptr)
        return;

    auto pid = DWORD {0};
    GetWindowThreadProcessId(window, &pid);

    if (pid == GetCurrentProcessId())
        return;

    if (activeCapture.onMouseDownOutside)
        activeCapture.onMouseDownOutside();
}

// --- Preview origin ----------------------------------------------------------

HWND previewOrigin = nullptr;

// --- Installed apps ---------------------------------------------------------
//
// What the Start menu lists: every shortcut under the user's and everyone's
// Programs folders that points at an executable. Uninstallers and help files
// are shortcuts too, so only an .exe target counts and "Uninstall …" is
// skipped. Store apps live in no folder and aren't listed.

std::vector<std::wstring> startMenuFolders()
{
    auto folders = std::vector<std::wstring> {};

    for (auto& id: {FOLDERID_Programs, FOLDERID_CommonPrograms})
    {
        auto* path = (PWSTR) nullptr;

        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)))
            folders.emplace_back(path);

        CoTaskMemFree(path);
    }

    return folders;
}

std::wstring
    shortcutTarget(IShellLinkW* link, IPersistFile* file, const std::wstring& path)
{
    if (FAILED(file->Load(path.c_str(), STGM_READ)))
        return {};

    wchar_t target[MAX_PATH] = {};

    if (FAILED(link->GetPath(target, MAX_PATH, nullptr, SLGP_RAWPATH)))
        return {};

    wchar_t expanded[MAX_PATH] = {};
    ExpandEnvironmentStringsW(target, expanded, MAX_PATH);
    return expanded;
}

std::vector<InstalledApp> scanInstalledApps()
{
    auto apps = std::vector<InstalledApp> {};
    auto seen = std::set<std::string> {};
    auto* link = (IShellLinkW*) nullptr;
    auto* file = (IPersistFile*) nullptr;

    if (FAILED(CoCreateInstance(
            CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
        return apps;

    if (FAILED(link->QueryInterface(IID_PPV_ARGS(&file))))
    {
        link->Release();
        return apps;
    }

    for (auto& folder: startMenuFolders())
    {
        auto error = std::error_code {};
        auto entries = std::filesystem::recursive_directory_iterator(
            folder,
            std::filesystem::directory_options::skip_permission_denied,
            error);

        for (auto it = entries;
             !error && it != std::filesystem::recursive_directory_iterator {};
             it.increment(error))
        {
            auto& path = it->path();

            if (lowered(path.extension().wstring()) != L".lnk")
                continue;

            auto name = path.stem().wstring();

            if (lowered(name).starts_with(L"uninstall"))
                continue;

            auto target = shortcutTarget(link, file, path.wstring());

            if (lowered(target).ends_with(L".exe") == false)
                continue;

            auto id = idForExecutable(target);

            if (!seen.insert(id).second)
                continue;

            apps.push_back({id, narrow(name), narrow(path.wstring())});
        }
    }

    file->Release();
    link->Release();
    return apps;
}

std::function<void(std::vector<InstalledApp>)> installedChanged;

// By id, as the last scan found them: where a favourite's `openWith` is
// looked up.
std::map<std::string, std::wstring> installedTargets;

void deliverInstalledApps(std::vector<InstalledApp> apps)
{
    installedTargets.clear();

    for (auto& app: apps)
        installedTargets[app.id] = widen(app.path);

    if (installedChanged)
        installedChanged(std::move(apps));
}

// The scanner's own thread: one scan now, then one per settled burst of
// changes under the Start menu folders -- an install writes several
// shortcuts, and two seconds of quiet coalesce them. It waits on change
// notifications, so nothing runs while nothing changes.
void watchStartMenu()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    auto changes = std::vector<HANDLE> {};

    for (auto& folder: startMenuFolders())
    {
        auto handle = FindFirstChangeNotificationW(
            folder.c_str(),
            TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME);

        if (handle != INVALID_HANDLE_VALUE)
            changes.push_back(handle);
    }

    while (true)
    {
        auto apps = std::make_shared<std::vector<InstalledApp>>(scanInstalledApps());
        eacp::Threads::callAsync([apps] { deliverInstalledApps(std::move(*apps)); });

        if (changes.empty())
            return;

        auto signalled = WaitForMultipleObjects(
            (DWORD) changes.size(), changes.data(), FALSE, INFINITE);

        if (signalled >= WAIT_OBJECT_0 + changes.size())
            return;

        FindNextChangeNotification(changes[signalled - WAIT_OBJECT_0]);

        // Keep draining until the folders go quiet.
        while (WaitForMultipleObjects(
                   (DWORD) changes.size(), changes.data(), FALSE, 2000)
               < WAIT_OBJECT_0 + changes.size())
            for (auto handle: changes)
                if (WaitForSingleObject(handle, 0) == WAIT_OBJECT_0)
                    FindNextChangeNotification(handle);
    }
}

// --- Paths and handlers ----------------------------------------------------

std::wstring expandedPath(const std::string& path)
{
    if (path.starts_with("~"))
        return widen(eacp::FilePath::homeDirectory().str() + path.substr(1));

    return widen(path);
}

std::wstring registeredAppPath(const std::string& id)
{
    auto key =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + widen(id);

    for (auto root: {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
    {
        wchar_t value[MAX_PATH] = {};
        auto size = (DWORD) sizeof(value);

        if (RegGetValueW(
                root, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, value, &size)
            == ERROR_SUCCESS)
        {
            auto path = std::wstring {value};

            if (path.size() > 1 && path.front() == L'"')
                path = path.substr(1, path.find(L'"', 1) - 1);

            return path;
        }
    }

    return {};
}

// Where the app with that id lives: running, in the Start menu (as its
// shortcut), or registered under App Paths, which is where browsers put
// themselves.
std::wstring findApp(const std::string& id)
{
    if (id.empty())
        return {};

    for (auto& entry: appWindows())
        if (entry.id == id)
            return entry.path;

    if (auto found = installedTargets.find(id); found != installedTargets.end())
        return found->second;

    return registeredAppPath(id);
}

bool shellOpen(const std::wstring& file, const std::wstring& parameters = {})
{
    auto result =
        (INT_PTR) ShellExecuteW(nullptr,
                                L"open",
                                file.c_str(),
                                parameters.empty() ? nullptr : parameters.c_str(),
                                nullptr,
                                SW_SHOWNORMAL);
    return result > 32;
}

std::wstring quoted(const std::string& text)
{
    return L"\"" + widen(text) + L"\"";
}
} // namespace

std::vector<RunningApp> runningApps()
{
    auto apps = std::vector<RunningApp> {};
    auto seen = std::set<std::string> {};

    for (auto& entry: appWindows())
        if (seen.insert(entry.id).second)
            apps.push_back(describe(entry));

    return apps;
}

RunningApp frontmostApp()
{
    auto foreground = GetForegroundWindow();

    for (auto& entry: appWindows())
        if (entry.window == foreground)
            return describe(entry);

    return {};
}

const eacp::Graphics::Image& appIcon(const RunningApp& app)
{
    auto& cache = iconCache();

    if (auto found = cache.find(app.id); found != cache.end())
        return found->second;

    auto& image = cache[app.id];
    image = renderIcon(executableOf(app));
    return image;
}

void forgetIcon(const std::string& appId)
{
    iconCache().erase(appId);
}

bool activateApp(const RunningApp& app)
{
    auto windows = windowsOf(app.id);

    if (windows.empty())
        return false;

    // Alt+Tab restores a minimised window; so does this.
    return bringToFront(windows.front().window, true);
}

bool startKeyCapture(eacp::Graphics::Window& panel,
                     KeyCapture capture,
                     const std::vector<CaptureKey>&)
{
    stopKeyCapture();

    activeCapture = std::move(capture);
    capturePanel = (HWND) panel.getHandle();
    captureHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND,
                                  EVENT_SYSTEM_FOREGROUND,
                                  nullptr,
                                  onCaptureForeground,
                                  0,
                                  0,
                                  WINEVENT_OUTOFCONTEXT);
    return true;
}

void stopKeyCapture()
{
    if (captureHook != nullptr)
        UnhookWinEvent(captureHook);

    captureHook = nullptr;
    capturePanel = nullptr;
    activeCapture = {};
}

// Hotkeys reach Rudder through RegisterHotKey, ahead of any window, so there
// is nothing to let through.
void setKeyCapturePassThrough(const std::vector<CaptureKey>&) {}

void previewApp(const RunningApp& app,
                const std::string& windowId,
                bool restoreOrigin,
                std::function<void(PreviewResult)> onDone)
{
    auto result = PreviewResult {};

    if (!windowId.empty())
    {
        auto row = AppWindow {};
        result.raised = raiseBehindPanel(cachedWindow(app, windowId, row));
    }
    else
    {
        auto windows = windowsOf(app.id);
        auto front = std::find_if(windows.begin(),
                                  windows.end(),
                                  [](const WindowEntry& entry)
                                  { return !IsIconic(entry.window); });

        result.raised = front != windows.end() && raiseBehindPanel(front->window);

        if (restoreOrigin)
            raiseBehindPanel(previewOrigin);
    }

    result.windowTitles = titlesOf(app.id);

    // As on macOS, the answer comes back later on the main thread.
    auto shared = std::make_shared<PreviewResult>(std::move(result));
    eacp::Threads::callAsync([shared, onDone = std::move(onDone)]
                             { onDone(std::move(*shared)); });
}

std::vector<std::string> windowTitles(const RunningApp& app)
{
    return titlesOf(app.id);
}

void rememberPreviewOrigin(const RunningApp& origin)
{
    previewOrigin = nullptr;

    if (origin.id.empty())
        return;

    // Called before the panel shows, so the foreground window is still the
    // exact one the user was in.
    auto foreground = GetForegroundWindow();

    for (auto& entry: windowsOf(origin.id))
        if (entry.window == foreground)
            previewOrigin = foreground;
}

void restorePreviewOrigin()
{
    if (previewOrigin != nullptr)
        bringToFront(previewOrigin, false);
}

std::vector<AppWindow> cachedWindows(const RunningApp& app)
{
    auto windows = std::vector<AppWindow> {};

    if (auto found = windowCache().find(app.id); found != windowCache().end())
        for (auto& cached: found->second)
            windows.push_back(cached.row);

    return windows;
}

void watchWindows(const std::vector<RunningApp>& apps,
                  std::function<void(const std::string& appId)> onChanged)
{
    stopWatchingWindows();

    windowsChanged = std::move(onChanged);

    for (auto& app: apps)
        watchedIds.insert(app.id);

    windowHooks.add(EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE, onWatchedWindowEvent);
    windowHooks.add(
        EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, onWatchedWindowEvent);
    windowHooks.add(
        EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, onWatchedWindowEvent);

    refreshWatchedWindowsSoon();
}

void stopWatchingWindows()
{
    windowHooks.clear();
    windowsChanged = nullptr;
    watchedIds.clear();
}

bool focusWindow(const RunningApp& app, const std::string& windowId, bool unminimize)
{
    auto row = AppWindow {};
    auto window = cachedWindow(app, windowId, row);

    if (window == nullptr)
        return false;

    if (!bringToFront(window, unminimize))
        activateApp(app);

    return true;
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

// Only the macOS unlock overlay drags a file out, and it never shows here.
bool startFileDrag(eacp::Graphics::View&, const std::string&)
{
    return false;
}

void observeWorkspace(WorkspaceEvents events)
{
    workspaceHooks.clear();
    workspaceEvents.reset();
    knownApps.clear();

    if (!events.onActivated && !events.onLaunched && !events.onTerminated)
        return;

    workspaceEvents = std::make_shared<WorkspaceEvents>(std::move(events));
    knownApps = appsById();

    workspaceHooks.add(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, onForeground);
    workspaceHooks.add(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, onWindowListChange);
    workspaceHooks.add(
        EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, onWindowListChange);
}

eacp::Graphics::Point panelOrigin(float width, float height)
{
    auto cursor = POINT {};
    GetCursorPos(&cursor);

    auto monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    auto info = MONITORINFO {};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);

    auto dpiX = UINT {96};
    auto dpiY = UINT {96};
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);

    // eacp places windows in points: pixels over the display's scale.
    auto scale = (float) dpiX / 96.f;
    auto& area = info.rcWork;
    auto x = (float) area.left / scale
             + ((float) (area.right - area.left) / scale - width) / 2;
    auto y = (float) area.top / scale
             + ((float) (area.bottom - area.top) / scale - height) / 2;
    return {x, y};
}

void watchInstalledApps(std::function<void(std::vector<InstalledApp>)> onChange)
{
    installedChanged = std::move(onChange);

    static auto started = false;

    if (std::exchange(started, true))
        return;

    // Lives as long as the process; it is only ever blocked in a wait.
    std::thread(watchStartMenu).detach();
}

const eacp::Graphics::Image& bundleIcon(const std::string& id,
                                        const std::string& path)
{
    auto& cache = bundleIconCache();

    if (auto found = cache.find(id); found != cache.end())
        return found->second;

    auto& image = cache[id];
    image = renderIcon(expandedPath(path));
    return image;
}

// What the window's close button does, to every window the app has: an app
// with unsaved work asks, and may stay.
bool quitApp(const RunningApp& app)
{
    auto windows = windowsOf(app.id);

    for (auto& entry: windows)
        PostMessageW(entry.window, WM_CLOSE, 0, 0);

    return !windows.empty();
}

bool openUrl(const std::string& url, const std::string& openWith)
{
    if (openWith.empty())
        return shellOpen(widen(url));

    auto app = findApp(openWith);

    // Never quietly the default browser instead: the favourite asked for
    // this app.
    if (app.empty())
    {
        eacp::LOG("Rudder: no app ", openWith, " for ", url);
        return false;
    }

    return shellOpen(app, quoted(url));
}

bool hasApp(const std::string& bundleId)
{
    return !findApp(bundleId).empty();
}

bool openPath(const std::string& path)
{
    return shellOpen(expandedPath(path));
}

std::string handlerFor(const std::string& url, const std::string& openWith)
{
    if (auto app = findApp(openWith); !app.empty())
        return narrow(app);

    auto colon = url.find(':');
    auto scheme = widen(colon == std::string::npos ? std::string {"https"}
                                                   : url.substr(0, colon));
    wchar_t path[MAX_PATH] = {};
    auto size = (DWORD) MAX_PATH;

    if (FAILED(AssocQueryStringW(ASSOCF_IS_PROTOCOL,
                                 ASSOCSTR_EXECUTABLE,
                                 scheme.c_str(),
                                 L"open",
                                 path,
                                 &size)))
        return {};

    return narrow(path);
}

bool launchApp(const std::string& path)
{
    return shellOpen(widen(path));
}

// Asking for the chord is the test: RegisterHotKey refuses one the shell or
// another app already holds, which is exactly a hotkey that would never
// fire. Rudder's own are unregistered before this is asked.
bool isSystemShortcut(std::uint16_t keyCode,
                      const eacp::Graphics::ModifierKeys& modifiers)
{
    auto key = eacp::Graphics::virtualKeyFromKeyCode(keyCode);

    if (key == 0)
        return false;

    auto flags = UINT {MOD_NOREPEAT};

    if (modifiers.shift)
        flags |= MOD_SHIFT;
    if (modifiers.control)
        flags |= MOD_CONTROL;
    if (modifiers.alt)
        flags |= MOD_ALT;
    if (modifiers.command)
        flags |= MOD_WIN;

    constexpr auto probeId = 0xBFFF;

    if (!RegisterHotKey(nullptr, probeId, flags, (UINT) key))
        return GetLastError() == ERROR_HOTKEY_ALREADY_REGISTERED;

    UnregisterHotKey(nullptr, probeId);
    return false;
}

namespace
{
// Set for as long as this Rudder runs; a client that finds it set by one
// that crashed clears it itself (RudderClient-Windows.cpp).
struct StartedEvent
{
    StartedEvent()
    {
        auto name = L"Local\\" + widen(protocol::startedNotification);
        handle = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
    }

    ~StartedEvent()
    {
        if (handle == nullptr)
            return;

        ResetEvent(handle);
        CloseHandle(handle);
    }

    HANDLE handle = nullptr;
};
} // namespace

void announceStarted()
{
    static auto event = StartedEvent {};

    if (event.handle != nullptr)
        SetEvent(event.handle);
}

double uptime()
{
    return (double) GetTickCount() / 1000.0;
}

void probeKeyFlow(std::uint16_t,
                  int,
                  double,
                  std::function<void(const std::string&)> report)
{
    report("probe: there is no key capture tap on Windows to probe");
}
} // namespace rudder::platform
