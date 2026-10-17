#include "BackStack.h"
#include "CaptureGate.h"
#include "Favourites.h"
#include "Focus.h"
#include "Frecency.h"
#include "ContextServer.h"
#include "Icon.h"
#include "Keybinding.h"
#include "Mru.h"
#include "PanelFrame.h"
#include "PanelView.h"
#include "Platform.h"
#include "Providers.h"
#include "QuickWindow.h"
#include "Ranking.h"
#include "Renames.h"
#include "Web.h"

#include <eacp/Core/App/LoginItem.h>
#include <eacp/UI/UI.h>
#include <emberstore/AppDatabase.h>
#include <emberstore/FileWatcher.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace eacp;
using namespace Graphics;
using namespace rudder;

namespace
{
// Set RUDDER_LOG=1 to trace the panel's show/hide handshake on stderr.
void logEvent(const char* message)
{
    static const auto enabled = std::getenv("RUDDER_LOG") != nullptr;

    if (!enabled)
        return;

    using namespace std::chrono;
    auto ms = duration_cast<milliseconds>(steady_clock::now().time_since_epoch());
    std::fprintf(stderr, "[rudder %lld] %s\n", (long long) ms.count() % 100000, message);
}

ModifierKeys modifiersOf(const Chord& chord)
{
    auto modifiers = ModifierKeys {};
    modifiers.shift = chord.shift;
    modifiers.control = chord.control;
    modifiers.alt = chord.option;
    modifiers.command = chord.command;
    return modifiers;
}
} // namespace

struct RudderApp
{
    RudderApp()
    {
        Apps::setDockIconVisible(false);

        if (!Apps::isLaunchAtLogin())
            Apps::setLaunchAtLogin(true);

        quick_window::setLogger(logEvent);

        // Opening Rudder.app again while it runs (Finder, Spotlight) arrives
        // as a reopen: summon the panel. quick_window swallows the reopens its
        // own activation escalation causes.
        quick_window::installReopenHandler(
            [this]
            {
                logEvent("reopen");
                reveal(false);
            });

        bindPanel();
        loadFavourites();
        watchInstalledApps();
        loadKeybindings();
        registerHotKeys();
        observeWorkspace();

        tray.setTemplateRendering(true);
        tray.setIcon(loadTrayIcon());
        tray.setTooltip("Rudder");
        tray.setMenu(createTrayMenu());

        window.setVisible(false);
        window.events.onActivationChanged = [this](bool isKey)
        { onPanelActivationChanged(isKey); };

        // Prime the history with where the user is now, and render every
        // running app's icon up front so the first summon has nothing to do.
        if (auto front = platform::frontmostApp(); !front.id.empty())
            backStack.record(front.id);

        for (auto& app: platform::runningApps())
            platform::appIcon(app);

        publishSettings();
        serveContexts();

        // As Tamby does: a launch without the grant opens the pane for it.
        if (!platform::accessibilityTrusted(false))
            platform::openAccessibilitySettings();

        platform::announceStarted();
        probeIfAsked();
    }

    ~RudderApp() { platform::observeWorkspace({}); }

    // --- Wiring ---------------------------------------------------------------

    void bindPanel()
    {
        panel.iconFor = [this](const std::string& appId) -> const Image&
        {
            if (auto app = findRunning(appId))
                return platform::appIcon(*app);

            if (auto found = installedPaths.find(appId); found != installedPaths.end())
                return platform::bundleIcon(appId, found->second);

            // The web row, asked for by its id: the browser it opens in.
            if (appId.starts_with("web:"))
            {
                auto handler =
                    platform::handlerFor("https://example.com", favourites.browser);
                return handler.empty() ? noIcon : platform::bundleIcon(appId, handler);
            }

            // A url or path favourite, asked for by its row id: the icon of
            // what it opens in, or of the file itself.
            for (auto& item: allItems)
            {
                if (item.id != appId || item.kind != "favourite")
                    continue;

                if (!item.path.empty())
                    return platform::bundleIcon(appId, item.path);

                auto handler = platform::handlerFor(
                    item.url, browserFor(item.openWith, favourites.browser));

                if (!handler.empty())
                    return platform::bundleIcon(appId, handler);
            }

            return noIcon;
        };

        panel.onQueryChange = [this](const std::string& text) { setQuery(text); };

        // RUDDER_LOG latency trace: hotkey to the first frame on screen, and
        // each key event (by its OS timestamp) to the frame that shows it.
        panel.onFrame = [this]
        {
            if (pendingPreview)
            {
                auto item = *std::exchange(pendingPreview, std::nullopt);
                Threads::callAsync([this, item] { previewNow(item); });
            }

            auto now = uptime();

            if (hotkeyTime > 0.0)
            {
                logEvent(("hotkey -> first frame " + millis(now - hotkeyTime)).c_str());
                hotkeyTime = 0.0;
                snapshotIfAsked();
            }

            if (panel.lastKeyTime > 0.0)
            {
                logEvent(("key -> frame " + millis(now - panel.lastKeyTime)).c_str());
                panel.lastKeyTime = 0.0;
            }
        };
        panel.onMove = [this](int delta) { moveSelection(delta); };
        panel.onAccept = [this] { Threads::callAsync([this] { activateSelection(); }); };
        panel.onChoose = [this](int index)
        {
            if (index < 0 || index >= (int) filtered.size())
                return;

            auto chosen = filtered[(std::size_t) index];
            Threads::callAsync([this, chosen] { pickItem(chosen); });
        };
        panel.onQuitSelected = [this] { Threads::callAsync([this] { quitSelected(); }); };
        panel.onToggleFavourite = [this]
        { Threads::callAsync([this] { toggleFavouriteSelected(); }); };
        panel.onRenameStart = [this] { return renamableSelection(); };
        panel.onRename = [this](const std::string& name)
        { Threads::callAsync([this, name] { renameSelected(name); }); };

        panel.onCancel = [this]
        {
            logEvent("panel: escape");
            Threads::callAsync([this] { cancel(); });
        };

        // While a chord is recorded the old ones must not fire -- pressing
        // ⌥Space to rebind it would otherwise close the panel.
        panel.onCaptureStart = [this]
        {
            logEvent("chord capture: start");
            setCapturing(true);
        };
        panel.onCaptureEnd = [this](const std::string& action, const std::string& chord)
        {
            settingsError.clear();
            logEvent(("chord capture: " + action + " = '" + chord + "'").c_str());

            if (!chord.empty())
                setBinding(action, chord);

            setCapturing(false);
            publishSettings();
        };

        panel.onRequestAccessibility = [] { platform::openAccessibilitySettings(); };

        // A stale grant (on in the list, pinned to an old signature) never
        // turns into trust: forget it and ask again. The gate keeps polling.
        panel.setPermissionsResettable(platform::canResetPermissions());
        panel.onResetPermissions = []
        {
            logEvent("resetting permissions");
            platform::resetPermissions();
        };

        // Starting the drag opens the pane, so the list is there to drop on.
        panel.onUnlockDrag = [this]
        {
            platform::startFileDrag(panel, platform::appBundlePath());
            platform::openAccessibilitySettings();
        };

        panel.onFrameDrag = [this](FrameDrag phase, std::uint8_t edges, Point at)
        { onFrameDrag(phase, edges, at); };

        panel.onCloseSettings = [this]
        {
            settingsError.clear();
            panel.reset(false);
            publishSettings();
            publishResults();
        };
    }

    // Borderless, rounded, above everything and on every Space. Built once at
    // startup and kept hidden, so a summon is only show + focus.
    static WindowOptions panelWindowOptions()
    {
        auto options = WindowOptions();

        options.width = (int) panelDefaultWidth;
        options.height = (int) panelDefaultHeight;
        options.isPrimary = false;

        // NonactivatingPanel is NOT honored: the mask only works on a real
        // NSPanel and eacp builds an NSWindow, so reveal() activates instead.
        options.flags = {WindowFlags::Borderless};
        options.cornerRadius = 14.f;

        options.alwaysOnTop = true;
        options.visibleOnAllWorkspaces = true;
        // So setVisible() only orderFronts; activating is reveal()'s job.
        options.showInactive = true;

        return options;
    }

    Menu createTrayMenu()
    {
        auto menu = Menu();
        menu.add(MenuItem::withAction("Open Rudder", [this] { reveal(false); }));
        menu.add(MenuItem::withAction("Settings…", [this] { reveal(true); }));
        menu.addSeparator();
        menu.add(MenuItem::withAction("Quit Rudder", [] { Apps::quit(); }));
        return menu;
    }

    // --- Hotkeys --------------------------------------------------------------

    void loadKeybindings()
    {
        if (auto stored = settingsStore.get(keybindingsDoc))
            bindings = *stored;
    }

    void registerHotKeys()
    {
        summonKey.reset();
        backKey.reset();

        if (capturing)
            return;

        for (auto* text: {&bindings.summon, &bindings.back})
            if (auto chord = parseChord(*text);
                chord && !GlobalHotKey::isAvailable(modifiersOf(*chord), *keyCodeFor(chord->key)))
                logEvent(("hotkey clashes with a system shortcut: " + *text).c_str());

        if (auto chord = parseChord(bindings.summon))
            summonKey.emplace(modifiersOf(*chord),
                              *keyCodeFor(chord->key),
                              [this] { togglePanel(); });

        if (auto chord = parseChord(bindings.back))
            backKey.emplace(
                modifiersOf(*chord), *keyCodeFor(chord->key), [this] { goBack(); });
    }

    void setCapturing(bool shouldCapture)
    {
        if (capturing == shouldCapture)
            return;

        capturing = shouldCapture;
        registerHotKeys();

        if (capturingKeys)
            platform::setKeyCapturePassThrough(passThroughKeys());
    }

    void setBinding(const std::string& action, const std::string& text)
    {
        auto chord = parseChord(text);

        if (!chord)
        {
            settingsError = "Use a modifier with that key (or a function key).";
            return;
        }

        auto canonical = serialiseChord(*chord);

        for (auto& [name, bound]: {std::pair {"summon", &bindings.summon},
                                   std::pair {"back", &bindings.back},
                                   std::pair {"quit", &bindings.quit},
                                   std::pair {"favourite", &bindings.favourite},
                                   std::pair {"rename", &bindings.rename}})
        {
            if (action != name && *bound == canonical)
            {
                settingsError = "That chord is already bound to " + std::string {name} + ".";
                return;
            }
        }

        // Quit, favourite and rename only act inside the panel, where Rudder has the
        // keyboard; the two global hotkeys must not be ones the OS keeps.
        // Recording unregistered Rudder's own (setCapturing), so the chord
        // bound now reads as free and can be kept.
        if (action != "quit" && action != "favourite" && action != "rename"
            && !GlobalHotKey::isAvailable(modifiersOf(*chord), *keyCodeFor(chord->key)))
        {
            if constexpr (eacp::Platform::isWindows())
                settingsError = "Windows or another app already uses "
                                + displayChord(*chord) + ".";
            else
                settingsError = "macOS already uses " + displayChord(*chord)
                                + " (Keyboard Shortcuts in System Settings).";
            return;
        }

        if (action == "summon")
            bindings.summon = canonical;
        else if (action == "back")
            bindings.back = canonical;
        else if (action == "quit")
            bindings.quit = canonical;
        else if (action == "favourite")
            bindings.favourite = canonical;
        else if (action == "rename")
            bindings.rename = canonical;

        settingsStore.doc(keybindingsDoc).set(bindings);

        if (capturingKeys)
            platform::setKeyCapturePassThrough(passThroughKeys());
    }

    // --- Back stack -----------------------------------------------------------

    // The "go back" hotkey: straight to the previous context, no panel. Each
    // press walks one further back; apps that quit meanwhile are skipped.
    void goBack()
    {
        if (panelVisible)
            cancel();

        logStack("back pressed");

        while (auto target = backStack.back())
        {
            if (auto app = findRunning(*target))
            {
                logEvent(("back -> " + app->name).c_str());
                lateRaise.closedOnto(app->id, uptime());
                platform::activateApp(*app);
                return;
            }

            backStack.forget(*target);
        }
    }

    void observeWorkspace()
    {
        auto events = platform::WorkspaceEvents {};

        // Previews activate apps while the panel is up; only what the panel
        // commits to counts (openItem records it).
        events.onActivated = [this](const platform::RunningApp& app)
        {
            if (panelVisible)
                return;

            if (auto target = lateRaise.activated(app.id, uptime()))
            {
                if (auto back = findRunningNow(*target))
                {
                    logEvent(("late raise by " + app.name + "; back to " + back->name)
                                 .c_str());
                    platform::activateApp(*back);
                    return;
                }
            }

            recordActivation(app.id);
            settleFocusLater();
        };

        // Rendering the icon now keeps the next summon free of it.
        events.onLaunched = [](const platform::RunningApp& app)
        { platform::appIcon(app); };

        events.onTerminated = [this](const platform::RunningApp& app)
        {
            if (panelVisible)
                markQuit(app.id);

            backStack.forget(app.id);
            platform::forgetIcon(app.id);
            forgetContexts(idOnlyContextKeys(app.id, providers.contextsFor(app.id)));
            providers.disconnect(app.id);
            focus.forget(app.id);
        };

        platform::observeWorkspace(std::move(events));
    }

    // Closed apps are searchable too. The list arrives from a background scan
    // and again whenever an app folder changes; an open panel picks it up at
    // its next summon.
    void watchInstalledApps()
    {
        platform::watchInstalledApps(
            [this](std::vector<platform::InstalledApp> apps)
            {
                installedItems.clear();
                installedPaths.clear();

                for (auto& app: apps)
                {
                    auto item = RudderItem {};
                    item.id = item.appId = app.id;
                    item.title = app.name;
                    item.subtitle = "Not running";
                    item.running = false;
                    item.path = app.path;
                    installedPaths[app.id] = app.path;
                    installedItems.push_back(std::move(item));
                }

                logEvent(("installed apps: " + std::to_string(apps.size())).c_str());
            });
    }

    // Every activation, through Rudder or not: the app and the context it
    // shows (a CowTerm pane) are stamped at the same moment, so the pane sits
    // just above its app in the empty query's list.
    void recordActivation(const std::string& appId)
    {
        auto now = grazer::MruStore::nowMs();
        mru.touch(appId, now);

        if (auto key = focus.activated(appId, now))
            mru.touch(*key, now);

        backStack.record(appId);
        logStack("activated " + appId);
    }

    void logStack(const std::string& what)
    {
        auto text = what + " | stack:";

        for (auto& id: backStack.entries())
            text += " " + id;

        logEvent((text + (backStack.walking() ? " (walking)" : "")).c_str());
    }

    // --- Panel frame ------------------------------------------------------------

    // Where the user left the panel on the screen under the mouse, or centred.
    void placePanel()
    {
        auto area = platform::screenAreaUnderMouse();
        applyFrame(placedFrame(panelFrames.savedFor(area), area));
    }

    Frame currentFrame()
    {
        auto position = window.getPosition();
        auto size = window.getSize();
        return {position.x, position.y, size.x, size.y};
    }

    void applyFrame(const Frame& frame)
    {
        quick_window::setPanelFrame(
            window, {frame.x, frame.y}, {frame.width, frame.height});
    }

    // The screen the panel's centre is on: where its frame is filed.
    Frame screenUnderPanel(const Frame& frame)
    {
        return platform::screenAreaAt(
            {frame.x + frame.width / 2.f, frame.y + frame.height / 2.f});
    }

    // A press on the panel's background or edge. Worked in screen points (the
    // window's position plus where the pointer is in it), so the window moving
    // under the pointer doesn't feed back into the drag. Clicks, focus and key
    // capture are untouched: the press never reaches a row or the query.
    void onFrameDrag(FrameDrag phase, std::uint8_t edges, Point at)
    {
        auto origin = window.getPosition();
        auto pointer = Point {origin.x + at.x, origin.y + at.y};

        if (phase == FrameDrag::reset)
        {
            frameDrag.reset();
            auto area = screenUnderPanel(currentFrame());
            panelFrames.forget(area);
            applyFrame(placedFrame(std::nullopt, area));
            logEvent("panel frame: reset");
            return;
        }

        if (phase == FrameDrag::begin)
        {
            frameDrag = FrameDragState {currentFrame(), pointer, edges};
            return;
        }

        if (!frameDrag)
            return;

        auto frame = draggedFrame(frameDrag->start,
                                  frameDrag->edges,
                                  pointer.x - frameDrag->pointer.x,
                                  pointer.y - frameDrag->pointer.y);

        if (frame != currentFrame())
            applyFrame(frame);

        if (phase != FrameDrag::end)
            return;

        auto moved = frame != frameDrag->start;
        frameDrag.reset();

        // A plain click is no move: nothing to remember.
        if (!moved)
            return;

        // Dropped part off the screen: back inside it, and remembered there.
        auto area = screenUnderPanel(frame);
        auto kept = clampedFrame(frame, area);

        if (kept != frame)
            applyFrame(kept);

        panelFrames.save(area, kept);
    }

    struct FrameDragState
    {
        Frame start;
        Point pointer;
        std::uint8_t edges = 0;
    };

    std::optional<FrameDragState> frameDrag;

    // --- Key capture ------------------------------------------------------------

    // While the panel is up the keyboard is ours, whichever app the preview
    // has activated. The two hotkeys pass through so they still toggle and go
    // back -- except while a new chord is being recorded.
    void startKeyCapture()
    {
        if (!platform::accessibilityTrusted(false))
            return;

        auto capture = platform::KeyCapture {};
        capture.onKey = [this](const KeyEvent& event) { onCapturedKey(event); };
        capture.onMouseDownOutside = [this]
        { Threads::callAsync([this] { cancel(); }); };

        // Capture has already switched itself off; the panel follows.
        capture.onAbort = [this](const std::string& reason)
        {
            logEvent(("key capture aborted: " + reason).c_str());
            capturingKeys = false;
            cancel();
        };

        capturingKeys =
            platform::startKeyCapture(window, std::move(capture), passThroughKeys());
    }

    void stopKeyCapture()
    {
        if (!capturingKeys)
            return;

        capturingKeys = false;
        platform::stopKeyCapture();
    }

    // The keys the tap lets through to the global hotkeys: none while a new
    // chord is being recorded.
    std::vector<CaptureKey> passThroughKeys() const
    {
        auto keys = std::vector<CaptureKey> {};

        if (capturing)
            return keys;

        for (auto* text: {&bindings.summon, &bindings.back})
            if (auto chord = parseChord(*text))
                if (auto key = captureKeyFor(*chord))
                    keys.push_back(*key);

        return keys;
    }

    void onCapturedKey(const KeyEvent& event)
    {
        if (capturing && event.type == KeyEventType::Down)
            logEvent(("captured key " + std::to_string(event.keyCode)).c_str());

        // RUDDER_DEBUG_STALL_MS: hold the main thread on every key press, to
        // prove a stuck Rudder can't take the keyboard with it.
        static const auto stallMs = []
        {
            auto* text = std::getenv("RUDDER_DEBUG_STALL_MS");
            return text != nullptr ? std::atoi(text) : 0;
        }();

        if (stallMs > 0 && event.type == KeyEventType::Down)
        {
            logEvent(("debug stall " + std::to_string(stallMs) + " ms").c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds {stallMs});
        }

        if (!capturing && (isChord(event, bindings.summon) || isChord(event, bindings.back)))
            return;

        if (event.type == KeyEventType::Down)
            panel.keyDown(event);
    }

    static bool isChord(const KeyEvent& event, const std::string& text)
    {
        auto chord = parseChord(text);

        auto& mods = event.modifiers;
        return chord
               && matchesChord(
                   *chord, event.keyCode, mods.control, mods.alt, mods.shift, mods.command);
    }

    // From the OS at the moment of the call, never the summon's snapshot.
    std::optional<platform::RunningApp> findRunningNow(const std::string& id)
    {
        for (auto& app: platform::runningApps())
            if (app.id == id)
                return app;

        return std::nullopt;
    }

    std::optional<platform::RunningApp> findRunning(const std::string& id)
    {
        for (auto& app: running)
            if (app.id == id)
                return app;

        for (auto& app: platform::runningApps())
            if (app.id == id)
                return app;

        return std::nullopt;
    }

    // --- Panel ----------------------------------------------------------------

    void togglePanel()
    {
        logEvent("summon hotkey");
        hotkeyTime = uptime();

        if (panelVisible)
            cancel();
        else
            reveal(false);
    }

    void reveal(bool showSettings)
    {
        logEvent("reveal: begin");

        // BEFORE the panel takes key: that app gets focus back on the way out.
        quick_window::rememberFrontmostApp();
        origin = platform::frontmostApp();
        platform::rememberPreviewOrigin(origin);
        lateRaise.reset();
        previewUndo.reset(providers.focusedContexts());

        panel.reset(showSettings);
        // A failed open's message lasts one summon; a bad favourites file's
        // until it's fixed.
        panel.setFooterError(favouritesError);
        resetPanel();
        settingsError.clear();
        publishSettings();

        if (!window.isVisible())
            placePanel();

        window.setVisible(true);
        panelVisible = true;

        quick_window::raiseAboveFullScreen(window);
        quick_window::takeKeyForTyping(window);
        // Without this, macOS routes every keystroke to the app underneath.
        quick_window::activateForPanel();

        panel.focus();
        syncAccessibilityGate(true);
        startKeyCapture();
        platform::watchWindows(running,
                               [this](const std::string& appId) { onWindowsChanged(appId); });
        logEvent("reveal: shown");
    }

    void hidePanel()
    {
        if (!panelVisible)
            return;

        logEvent("hidePanel: begin");
        panelVisible = false;
        setCapturing(false);
        syncAccessibilityGate(false);
        stopKeyCapture();
        platform::stopWatchingWindows();
        previewedId.clear();

        // Deferred on macOS: hiding a key panel while the app is active
        // promotes another window; deactivation must land first.
        quick_window::hideYieldingActivation(window,
                                             [this]
                                             {
                                                 // A summon landing inside
                                                 // that deferral makes this
                                                 // hide stale.
                                                 if (panelVisible)
                                                     return;

                                                 // Never a tap without a
                                                 // panel.
                                                 stopKeyCapture();
                                                 window.setVisible(false);
                                                 logEvent("hidePanel: ordered out");
                                             });
    }

    // The unlock overlay shows while the panel is up without the grant. macOS
    // has no notification for a TCC change, so the grant is checked once a
    // second -- but only while the overlay is on screen, so an idle Rudder
    // never wakes for it. `opening` is a fresh summon: like Tamby's gate, it
    // opens the pane right away.
    void syncAccessibilityGate(bool opening)
    {
        auto trusted = platform::accessibilityTrusted(false);
        auto gate = panelVisible && !trusted;

        panel.setAccessibilityGate(gate);

        if (!gate)
        {
            accessibilityPoll.reset();
            return;
        }

        if (opening)
            platform::openAccessibilitySettings();

        if (!accessibilityPoll)
            accessibilityPoll.emplace([this] { checkAccessibility(); }, 1);
    }

    void checkAccessibility()
    {
        if (!platform::accessibilityTrusted(false))
            return;

        logEvent("accessibility granted");

        // Live preview works from here on: capture the origin window that
        // couldn't be read without the grant, and lift the overlay.
        platform::rememberPreviewOrigin(origin);
        syncAccessibilityGate(false);
        publishSettings();
        panel.focus();
    }

    void onPanelActivationChanged(bool isKey)
    {
        logEvent(isKey ? "panel became key" : "panel lost key");

        // The unlock flow sends the user to System Settings and back; the
        // panel waits for them instead of hiding.
        if (isKey || !window.isVisible() || panel.accessibilityGateShown()
            || capturingKeys)
            return;

        Threads::callAsync(
            [this]
            {
                if (window.isVisible() && panelVisible)
                    cancel();
            });
    }

    // A fresh list for this summon: every running app, most recent first,
    // with its contexts attached, selection on the previous app.
    void resetPanel()
    {
        frontAppId = origin.id;
        running = platform::runningApps();
        query.clear();
        allItems.clear();

        for (auto& app: running)
        {
            auto item = RudderItem {};
            item.id = app.id;
            item.title = app.name;
            item.appId = app.id;
            item.contexts = contextsFor(app);
            item.subtitle = app.id == frontAppId ? "Current" : "";
            allItems.push_back(std::move(item));
        }

        orderBaseList();

        filtered = rankWithWeb(allItems, query, webSettings(), frecencyLookup(), recencyLookup());
        selected = initialSelection(filtered, query, frontAppId);
        pendingPreview.reset();
        previewedId.clear();
        previewedAppId.clear();
        previewWindows.clear();
        previewRaised = false;
        publishResults();
    }

    // The base list: the running apps and the favourites together, most
    // recently used first (favourites never used below running apps never
    // used), then the installed apps that aren't running. Rebuilds the
    // favourite rows from `favourites` and the closed ones from
    // installedItems; keeps the running app rows as they are.
    void orderBaseList()
    {
        std::erase_if(allItems,
                      [](const RudderItem& item)
                      { return item.kind == "favourite" || !item.running; });

        auto starred = favouriteItems(favourites);
        allItems.insert(allItems.end(), starred.begin(), starred.end());

        // The app the panel was summoned over is on top whatever its stamp
        // says, so the previous app is always the second row.
        allItems = orderByRecency(std::move(allItems),
                                  [this](const std::string& key)
                                  {
                                      return key == frontAppId ? INT64_MAX
                                                               : mru.lastUsed(key);
                                  });

        auto closed = orderByRecency(installedItems,
                                     [this](const std::string& key)
                                     { return mru.lastUsed(key); });
        allItems = mergeInstalled(std::move(allItems), closed);
    }

    void setQuery(const std::string& text)
    {
        if (!panelVisible)
            return;

        query = text;
        logEvent(("query '" + query + "'").c_str());
        filtered = rankWithWeb(allItems, query, webSettings(), frecencyLookup(), recencyLookup());
        selected = initialSelection(filtered, query, frontAppId);
        previewSelection();
        publishResults();
    }

    void moveSelection(int delta)
    {
        if (!panelVisible || filtered.empty())
            return;

        selected = std::clamp(selected + delta, 0, (int) filtered.size() - 1);
        previewSelection();
        publishResults();
    }

    // Grazer's palette preview, for windows: whenever the results change -- a
    // keystroke or an arrow -- the selection comes to the front behind the
    // panel at once. Skipped when the selection didn't change. The app the
    // panel was summoned over previews as the screen was: its origin window.
    void previewSelection()
    {
        if (filtered.empty())
            return;

        auto& item = filtered[(std::size_t) selected];

        if (item.id == previewedId)
            return;

        previewedId = item.id;
        previewedAppId = item.appId;
        previewWindows.clear();
        previewRaised = false;

        // A closed app has no window to show: whatever was previewed last
        // stays where it is. Nothing is ever launched for a preview, and no
        // page opened: a web row waits for Enter.
        if (!item.running || item.kind == "web")
            return;

        // A favourite previews only what is already open: the tab showing its
        // url, or its app if running. Anything else stays unopened.
        auto target = item.kind == "favourite" ? openTargetFor(item)
                                               : std::optional<RudderItem> {item};

        if (!target)
            return;

        target->id = item.id;
        previewedAppId = target->appId;

        // After the next frame: the list shows the keystroke first, and the
        // activation (an AX round trip to the target) follows at once.
        pendingPreview = *target;
        panel.repaint();
    }

    // The one place a preview touches another app.
    void previewNow(const RudderItem& item)
    {
        if (!panelVisible || previewedId != item.id)
            return;

        auto app = findRunningNow(item.appId);
        auto action = previewActionFor(item, app.has_value(), frontAppId);

        if (action == PreviewAction::none)
            return;

        auto start = uptime();

        // A provider's context: the app only switches to it (never raising
        // itself -- that would land late, over wherever the user went next),
        // then the usual preview raises the app.
        if (auto request = contextRequestFor(item, ContextPhase::preview))
            if (providers.activate(item.appId, request->contextId, request->raise))
                previewUndo.previewed(item.appId);

        // Whatever raises it -- the provider above, or the preview below --
        // must not land after the panel has closed onto something else.
        lateRaise.previewed(item.appId);

        // Under key capture the preview can activate for real -- the only
        // way the window gets above everything -- and typing still lands in
        // the panel. Without it (no grant), nothing is raised: the pane shows
        // the selection instead.
        if (!capturingKeys)
            return;

        // Off the main thread: a hung app answers late, and only its own
        // preview waits.
        auto id = item.id;
        auto label = app->name + (item.kind == "window" ? " / " + item.title : std::string {});
        auto windowId = item.kind == "window" ? item.contextId : std::string {};

        platform::previewApp(
            *app,
            windowId,
            item.kind != "window" && action == PreviewAction::restoreOrigin,
            [this, id, label, start](platform::PreviewResult result)
            {
                if (!panelVisible || previewedId != id)
                    return;

                previewRaised = result.raised;
                previewWindows = std::move(result.windowTitles);

                logEvent(("preview " + label + " " + millis(uptime() - start)
                          + (previewRaised ? "" : " (not raised)"))
                             .c_str());

                publishResults();
            });
    }

    // --- Favourites ---------------------------------------------------------------

    // ~/.config/rudder.json: one pretty-printed document, read and written
    // through emberstore (a Document at that exact path), Rudder's own writes
    // in a stable (sorted-key) order, and meant to be edited by hand or by an
    // agent. Edits from outside are picked up as they land; a bad one is
    // reported in the footer and the last good list stays.
    static eacp::FilePath favouritesPath()
    {
        return eacp::FilePath::homeDirectory() / ".config" / "rudder.json";
    }

    void loadFavourites()
    {
        auto path = favouritesPath();
        std::fprintf(stderr, "Rudder favourites: %s\n", path.str().c_str());

        auto folder = std::filesystem::path {path.str()}.parent_path();
        auto error = std::error_code {};
        std::filesystem::create_directories(folder, error);

        migrateFavourites();

        if (!std::filesystem::exists(path.str()))
            favouritesDocument.set(favourites);

        reloadFavourites();

        // The watcher sits on ~/.config and reacts to rudder.json alone, so an
        // editor's save-by-rename is seen and other files' churn isn't.
        favouritesWatcher.emplace(path, [this] { reloadFavourites(); });
    }

    // Favourites used to live in Rudder's emberstore folder. Moved over once,
    // only when the new file isn't there yet, then the old one goes.
    void migrateFavourites()
    {
        auto old = (durable.directory() / "favourites.json").str();
        auto now = favouritesPath().str();

        if (!std::filesystem::exists(old) || std::filesystem::exists(now))
            return;

        auto error = std::error_code {};
        std::filesystem::copy_file(old, now, error);

        if (error)
        {
            logEvent(("favourites: couldn't move " + old + ": " + error.message()).c_str());
            return;
        }

        std::filesystem::remove(old, error);
        logEvent(("favourites: moved " + old + " to " + now).c_str());
    }

    void reloadFavourites()
    {
        auto file = std::ifstream {favouritesPath().str()};

        if (!file)
            return;

        auto text = std::string {std::istreambuf_iterator<char> {file}, {}};
        auto result = parseFavourites(text);

        if (!result.list)
        {
            logEvent(("favourites: " + result.error).c_str());
            favouritesError = result.error;
            panel.setFooterError(favouritesError);
            return;
        }

        favourites = std::move(*result.list);
        favouritesError.clear();
        panel.setFooterError({});
        logEvent(("favourites: " + std::to_string(favourites.favourites.size())).c_str());

        if (panelVisible)
            rebuildFavouriteRows();
    }

    // The favourite rows changed under an open panel: swap them in, keep the
    // query.
    void rebuildFavouriteRows()
    {
        orderBaseList();
        filtered = rankWithWeb(allItems, query, webSettings(), frecencyLookup(), recencyLookup());
        selected = std::clamp(selected, 0, std::max((int) filtered.size() - 1, 0));
        publishResults();
    }

    // The rename binding: only a provider's context can be renamed. The
    // selected row's title to start the editor from, or nothing.
    std::optional<std::string> renamableSelection()
    {
        if (!panelVisible || filtered.empty())
            return std::nullopt;

        auto& item = filtered[(std::size_t) selected];

        if (item.kind != "context")
            return std::nullopt;

        return item.title;
    }

    // The editor's Enter: name the selection (empty clears the name), keyed
    // so it sticks to the branch or directory, not this one pane.
    void renameSelected(const std::string& name)
    {
        if (!panelVisible || filtered.empty())
            return;

        auto& item = filtered[(std::size_t) selected];

        if (item.kind != "context")
            return;

        auto key = frecencyKey(item);
        renames.set(key, name);
        logEvent(("rename " + key + " -> '" + renames.nameFor(key) + "'").c_str());

        for (auto& app: running)
            for (auto& row: allItems)
                if (row.kind == "app" && row.running && row.appId == app.id)
                    row.contexts = contextsFor(app);

        rerankKeepingSelection();
    }

    // The favourite binding: star the selection, or unstar a favourite.
    void toggleFavouriteSelected()
    {
        if (!panelVisible || filtered.empty())
            return;

        auto favourite = favouriteFor(filtered[(std::size_t) selected]);

        if (!favourite)
            return;

        auto added = toggleFavourite(favourites, *favourite);
        logEvent(((added ? "favourited " : "unfavourited ") + favourite->title).c_str());
        favouritesDocument.set(favourites);
        rebuildFavouriteRows();
    }

    // What a favourite stands for among things already open: the tab showing
    // its url, or its app's row if the app is running.
    std::optional<RudderItem> openTargetFor(const RudderItem& favourite)
    {
        if (!favourite.url.empty())
            return contextForUrl(allItems, favourite.url);

        if (!favourite.appId.empty())
            if (auto app = findRunningNow(favourite.appId))
            {
                auto row = RudderItem {};
                row.id = row.appId = app->id;
                row.title = app->name;
                return row;
            }

        return std::nullopt;
    }

    void openFavourite(const RudderItem& favourite)
    {
        if (!favourite.url.empty())
        {
            openLink(favourite.url, favourite.openWith, favourite.title);
            return;
        }

        // The running app is reused, not launched again.
        if (auto open = openTargetFor(favourite))
        {
            openItem(*open);
            return;
        }

        if (!favourite.appId.empty())
        {
            if (auto path = installedPaths.find(favourite.appId); path != installedPaths.end())
            {
                auto closed = RudderItem {};
                closed.id = closed.appId = favourite.appId;
                closed.title = favourite.title;
                closed.running = false;
                closed.path = path->second;
                openItem(closed);
            }

            return;
        }

        if (favourite.path.empty())
            return;

        logEvent(("open favourite -> " + favourite.title).c_str());
        quick_window::rememberAppForHandoff(0);
        hidePanel();

        // Whichever app opens it is meant to come forward, a previewed one
        // included.
        lateRaise.reset();
        platform::openPath(favourite.path);
    }

    WebSettings webSettings() const { return {favourites.browser, favourites.searchUrl}; }

    // Every link Rudder opens goes through here: a url favourite, the web
    // row. A provider tab already showing the url is reused; otherwise it
    // opens in `openWith`, else the configured browser, else the default.
    void openLink(const std::string& url, const std::string& openWith, const std::string& label)
    {
        if (auto open = contextForUrl(allItems, url))
        {
            openItem(*open);
            return;
        }

        auto app = browserFor(openWith, favourites.browser);

        // Checked before the panel goes, so the footer can say why nothing
        // opened -- never quietly the default browser instead.
        if (!app.empty() && !platform::hasApp(app))
        {
            logEvent(("open link: no app " + app).c_str());
            panel.setFooterError("No app with bundle id " + app);
            return;
        }

        logEvent(("open link -> " + label).c_str());
        quick_window::rememberAppForHandoff(0);
        hidePanel();

        // Whichever app opens it is meant to come forward, a previewed one
        // included.
        lateRaise.reset();
        platform::openUrl(url, app);
    }

    // --- Contexts ---------------------------------------------------------------

    // Provider apps (Grazer's tabs, ...) connect over the contexts socket. The
    // server is up before rudder.started goes out, so the apps that hear it
    // can connect straight away.
    void serveContexts()
    {
        contextServer.onHello = [this](int connection, const protocol::Hello& hello)
        {
            connectionHellos[connection] = hello;
            connectProvider(connection, hello);
        };

        contextServer.onContexts =
            [this](int connection, std::vector<protocol::Context> pushed)
        {
            // An app that said hello before its first window showed is filed
            // under its bundle id until a push finds its row.
            if (auto hello = connectionHellos.find(connection);
                hello != connectionHellos.end()
                && providerAppId(hello->second) != connectionApps[connection])
                connectProvider(connection, hello->second);

            auto app = connectionApps.find(connection);

            if (app == connectionApps.end() || providerConnection[app->second] != connection)
                return;

            auto contexts = std::vector<RudderContext> {};

            for (auto& context: pushed)
            {
                auto converted =
                    RudderContext {context.id, context.title, context.subtitle, context.url};
                converted.open = context.open;
                converted.path = context.path;
                converted.branch = context.branch;
                converted.focused = context.focused;
                contexts.push_back(std::move(converted));
            }

            logEvent(("provider contexts: " + app->second + " "
                      + std::to_string(contexts.size()))
                         .c_str());
            trackFocus(app->second, contexts);
            forgetContexts(
                goneContextKeys(app->second, providers.contextsFor(app->second), contexts));
            providers.setContexts(app->second, std::move(contexts));
        };

        contextServer.onClosed = [this](int connection)
        {
            auto app = connectionApps.find(connection);

            if (app == connectionApps.end())
                return;

            auto bundleId = app->second;
            connectionApps.erase(app);
            connectionHellos.erase(connection);

            if (providerConnection[bundleId] != connection)
                return;

            logEvent(("provider gone: " + bundleId).c_str());
            providerConnection.erase(bundleId);
            forgetContexts(idOnlyContextKeys(bundleId, providers.contextsFor(bundleId)));
            providers.disconnect(bundleId);
            focus.forget(bundleId);
        };

        providers.onChanged = [this] { refreshContexts(); };

        if (!contextServer.start())
            logEvent("contexts socket unavailable");
    }

    void connectProvider(int connection, const protocol::Hello& hello)
    {
        auto appId = providerAppId(hello);
        logEvent(("provider hello: " + hello.app + " (" + appId + ")").c_str());

        if (auto previous = connectionApps.find(connection);
            previous != connectionApps.end() && previous->second != appId
            && providerConnection[previous->second] == connection)
        {
            providerConnection.erase(previous->second);
            providers.disconnect(previous->second);
        }

        connectionApps[connection] = appId;

        // A newer connection from the same app takes over its contexts; the
        // older one closing later mustn't drop them.
        providerConnection[appId] = connection;
        auto sendClose = ContextProviders::SendClose {};

        if (hello.canClose)
            sendClose = [this, connection](const std::string& contextId)
            { contextServer.send(connection, protocol::Close {contextId}); };

        providers.connect(
            {hello.app, appId, hello.pid},
            [this, connection](const std::string& contextId, bool raise)
            { contextServer.send(connection, protocol::Activate {contextId, raise}); },
            std::move(sendClose));
    }

    // The id of the app row a provider's contexts belong under: the running
    // app with the provider's pid. On macOS that is its bundle id anyway;
    // Windows has no bundle ids, and names apps by executable.
    std::string providerAppId(const protocol::Hello& hello)
    {
        for (auto& app: platform::runningApps())
            if (app.pid == hello.pid)
                return app.id;

        return hello.bundleId;
    }

    // Contexts that are gone -- closed from Rudder or in their app, expired,
    // or only ever named by an id their app won't keep -- lose their
    // frecency, their recency and their rename. Otherwise a context that
    // turns up on the same key later (Grazer reuses tab ids after a restart)
    // would inherit them.
    void forgetContexts(const std::vector<std::string>& keys)
    {
        for (auto& key: keys)
        {
            logEvent(("forget " + key).c_str());
            frecency.forget(key);
            mru.forget(key);
            renames.forget(key);
            focus.forgetKey(key);
        }
    }

    // A push may move the provider's focus: that's the user working there.
    // Never while the panel is up -- previews move it, and only a pick counts
    // then; Esc's undo puts it back to what the tracker last saw.
    void trackFocus(const std::string& appId, const std::vector<RudderContext>& contexts)
    {
        if (panelVisible)
            return;

        auto now = FrecencyStore::nowMs();
        auto frontmost = platform::frontmostApp().id == appId;

        if (auto key = focus.pushed(appId, contexts, frontmost, now))
        {
            logEvent(("focus -> " + *key).c_str());
            mru.touch(*key, now);
            settleFocusLater();
        }
    }

    // A dwell later: the front app's focused context, if it held, earns its
    // fraction of a pick.
    void settleFocusLater()
    {
        Threads::callAfter(Time::MS {FocusTracker::dwellMs + 250},
                           [this]
                           {
                               if (panelVisible)
                                   return;

                               auto now = FrecencyStore::nowMs();

                               if (auto key = focus.settle(platform::frontmostApp().id, now))
                               {
                                   logEvent(("focus held: " + *key).c_str());
                                   frecency.touch(*key, now, FocusTracker::focusWeight);
                               }
                           });
    }

    // A provider pushed or went away while the panel is up: every running
    // app's rows pick up their current contexts and windows.
    void refreshContexts()
    {
        if (!panelVisible)
            return;

        for (auto& app: running)
            refreshAppRows(app.id);
    }

    // What an app row carries as children: a provider's contexts, then the
    // app's own windows as Rudder last saw them.
    std::vector<RudderContext> contextsFor(const platform::RunningApp& app)
    {
        auto contexts = providers.contextsFor(app.id);

        for (auto& context: contexts)
            context.name = renames.nameFor(contextKey(app.id, context));

        for (auto& window: platform::cachedWindows(app))
        {
            auto context = RudderContext {};
            context.id = window.id;
            context.title = window.title;
            context.subtitle = window.minimized ? "Minimised" : "";
            context.kind = "window";
            contexts.push_back(std::move(context));
        }

        return contexts;
    }

    // An app's windows changed while the panel is up (the first enumeration
    // landing, or an AX observer): refresh its rows, keeping the selection on
    // the same row where it still exists.
    void onWindowsChanged(const std::string& appId)
    {
        if (panelVisible)
            refreshAppRows(appId);
    }

    void refreshAppRows(const std::string& appId)
    {
        auto app = findRunning(appId);

        if (!app)
            return;

        for (auto& item: allItems)
            if (item.appId == appId && item.kind == "app" && item.running)
                item.contexts = contextsFor(*app);

        rerankKeepingSelection();
    }

    // The list again (the empty query's too: it lists recent contexts), the
    // selection staying on the same row where it still exists.
    void rerankKeepingSelection()
    {
        auto selectedId = filtered.empty() ? std::string {}
                                           : filtered[(std::size_t) selected].id;
        filtered = rankWithWeb(allItems, query, webSettings(), frecencyLookup(), recencyLookup());

        auto again = std::find_if(filtered.begin(),
                                  filtered.end(),
                                  [&](const RudderItem& item) { return item.id == selectedId; });

        // A row that went (a context just closed) leaves the selection where
        // it was, on the row that took its place, so the next quit binding
        // closes that one rather than whatever is on top.
        selected = again != filtered.end()
                       ? (int) (again - filtered.begin())
                       : std::clamp(selected, 0, std::max((int) filtered.size() - 1, 0));
        publishResults();
    }

    // The quit binding (⌃Q by default): quit the selection, cmd-tab style. The panel stays up; the row
    // turns "not running" when the app is actually gone (markQuit), so an app
    // that refuses -- an unsaved-changes dialog -- simply stays running.
    // On an open context of an app that closes contexts (canClose), it
    // closes just that tab or pane instead; the row goes when the app's next
    // push leaves it out.
    void quitSelected()
    {
        if (!panelVisible || filtered.empty())
            return;

        auto& item = filtered[(std::size_t) selected];

        if (item.kind == "context" && providers.close(item.appId, item.contextId))
        {
            logEvent(("close -> " + item.appId + " " + item.contextId).c_str());
            return;
        }

        if (auto app = findRunningNow(item.appId))
        {
            logEvent(("quit -> " + app->name).c_str());
            platform::quitApp(*app);
        }
    }

    // An app quit while the panel is up: its rows become closed apps in place
    // (or go, if it isn't installed anywhere Rudder lists), keeping the query
    // and the selection.
    void markQuit(const std::string& appId)
    {
        auto path = installedPaths.find(appId);

        auto update = [&](std::vector<RudderItem>& items)
        {
            if (path == installedPaths.end())
            {
                std::erase_if(items,
                              [&](const RudderItem& item) { return item.appId == appId; });
                return;
            }

            for (auto& item: items)
            {
                if (item.appId != appId)
                    continue;

                item.running = false;
                item.subtitle = "Not running";
                item.path = path->second;
            }
        };

        update(allItems);
        update(filtered);
        selected = std::clamp(selected, 0, std::max((int) filtered.size() - 1, 0));

        if (previewedAppId == appId)
        {
            previewedId.clear();
            previewedAppId.clear();
        }

        std::erase_if(running,
                      [&](const platform::RunningApp& app) { return app.id == appId; });
        publishResults();
    }

    // Enter: open whatever is selected -- with no query that's the previous
    // app, cmd-tab style.
    void activateSelection()
    {
        if (!panelVisible || filtered.empty())
            return;

        pickItem(filtered[(std::size_t) selected]);
    }

    // Each row's frecency, all decayed to the same moment.
    FrecencyFn frecencyLookup()
    {
        return [this, now = FrecencyStore::nowMs()](const std::string& key)
        { return frecency.frecency(key, now); };
    }

    // The empty query's recency: the app summoned over on top, then by stamp.
    Recency recencyLookup()
    {
        return {[this](const std::string& key)
                { return key == frontAppId ? INT64_MAX : mru.lastUsed(key); },
                grazer::MruStore::nowMs(),
                frontAppId};
    }

    // Enter or a click on a row: what frecency counts. Not a preview, and not
    // the tab or app a favourite turns out to open (openItem from
    // openFavourite), only the row picked. A web row is a fresh query each
    // time, so it isn't counted.
    void pickItem(const RudderItem& item)
    {
        if (!panelVisible)
            return;

        auto now = FrecencyStore::nowMs();

        if (item.kind != "web"
            && pickMoves(item, frontAppId, providers.contextsFor(frontAppId)))
            frecency.touch(frecencyKey(item), now);

        // The focus tracker was fed nothing while the panel was up. A picked
        // context is where the app's focus goes (already counted); an app or
        // window picked shows whatever its provider last pushed.
        if (item.kind == "context")
            focus.picked(item.appId, frecencyKey(item), now);
        else if (item.kind == "app" || item.kind == "window")
            focus.pushed(item.appId, providers.contextsFor(item.appId), false, now);

        // Apps are stamped when they activate; a favourite and a context
        // here too, under the same key, so they take their place in the
        // recency order (a context's provider may report its focus only
        // after the panel is gone and its app in front).
        if (item.kind == "favourite" || item.kind == "context")
            mru.touch(frecencyKey(item));

        openItem(item);
    }

    void openItem(const RudderItem& item)
    {
        if (!panelVisible)
            return;

        if (item.kind == "favourite")
        {
            openFavourite(item);
            return;
        }

        if (item.kind == "web")
        {
            openLink(item.url, {}, item.title);
            return;
        }

        // A context goes to its provider as a raising `activate`; the app is
        // activated either way so the context's window comes forward.
        if (auto request = contextRequestFor(item, ContextPhase::commit))
            providers.activate(item.appId, request->contextId, request->raise);

        auto app = findRunningNow(item.appId);

        // Not running: Enter is the only thing that launches it. The launch
        // activates it, so focus isn't handed back anywhere on the way out.
        if (!app && !item.path.empty())
        {
            logEvent(("launch -> " + item.title).c_str());
            quick_window::rememberAppForHandoff(0);
            hidePanel();
            lateRaise.closedOnto(item.appId, uptime());
            platform::launchApp(item.path);
            recordActivation(item.appId);
            return;
        }


        if (!app)
        {
            cancel();
            return;
        }

        logEvent(("open -> " + app->name).c_str());
        quick_window::rememberAppForHandoff(app->pid);
        hidePanel();
        lateRaise.closedOnto(app->id, uptime());

        // A window row: that exact window, un-minimised if need be.
        if (item.kind != "window" || !platform::focusWindow(*app, item.contextId, true))
            platform::activateApp(*app);

        recordActivation(app->id);
    }

    // Esc / losing focus: put the screen back exactly as it was.
    void cancel()
    {
        if (!panelVisible)
            return;

        logEvent("cancel");

        // A preview activated another app, or another window, pane or tab
        // of this one (anything but its own app row); hand the screen back as
        // it was.
        auto previewed = !previewedId.empty() && previewedId != frontAppId;
        hidePanel();

        // Apps whose contexts were previewed go back to the one they showed.
        for (auto& [appId, contextId]: previewUndo.undo())
            providers.activate(appId, contextId, false);

        if (!origin.id.empty())
            lateRaise.closedOnto(origin.id, uptime());

        if (previewed && !origin.id.empty())
        {
            platform::activateApp(origin);
            platform::restorePreviewOrigin();
        }
    }

    void publishResults()
    {
        panel.setModel({filtered, selected, previewWindows, previewRaised});
    }

    void publishSettings()
    {
        auto label = [](const std::string& text)
        {
            auto chord = parseChord(text);
            return chord ? displayChord(*chord) : text;
        };

        panel.setSettings({label(bindings.summon),
                           label(bindings.back),
                           label(bindings.quit),
                           label(bindings.favourite),
                           label(bindings.rename),
                           bindings.quit,
                           bindings.favourite,
                           bindings.rename,
                           favouritesPath().str(),
                           platform::accessibilityTrusted(false),
                           settingsError});
    }

    static double uptime() { return platform::uptime(); }

    static std::string millis(double seconds)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%.1f ms", seconds * 1000.0);
        return text;
    }

    // RUDDER_DEBUG_CAPTURE_PROBE=<n>: summon the panel, post n presses of F20
    // (unbound, types nothing) through the real event taps 100 ms apart,
    // print which got past Rudder's capture and when, then dismiss and quit.
    // With RUDDER_DEBUG_STALL_MS or RUDDER_DEBUG_STALL_TAP_MS it shows a stuck
    // Rudder handing the keyboard back.
    void probeIfAsked()
    {
        auto* text = std::getenv("RUDDER_DEBUG_CAPTURE_PROBE");

        if (text == nullptr)
            return;

        probeTimer.emplace(
            [this, count = std::atoi(text)]
            {
                ++probeTicks;

                if (probeTicks == 2)
                    reveal(false);
                else if (probeTicks == 3)
                {
                    std::fprintf(stderr,
                                 "probe: panel %s, capturing keys %s\n",
                                 panelVisible ? "up" : "down",
                                 capturingKeys ? "yes" : "no");

                    platform::probeKeyFlow(
                        90,
                        count,
                        0.1,
                        [this](const std::string& report)
                        {
                            std::fprintf(stderr, "%s\n", report.c_str());
                            Threads::callAsync([this] { probeDone = true; });
                        });
                }
                else if (probeDone)
                {
                    std::fprintf(stderr,
                                 "probe: done; panel %s, capturing keys %s\n",
                                 panelVisible ? "up" : "down",
                                 capturingKeys ? "yes" : "no");
                    cancel();
                    Apps::quit();
                }
            },
            1);
    }

    std::optional<Threads::Timer> probeTimer;
    int probeTicks = 0;
    bool probeDone = false;

    // RUDDER_SNAPSHOT=<path.png>: save what the panel drew on its first frame
    // -- the real panel's pixels, for a machine where screencapture has no
    // screen-recording grant.
    void snapshotIfAsked()
    {
        auto* path = std::getenv("RUDDER_SNAPSHOT");

        if (path == nullptr)
            return;

        auto target = std::string {path};
        Threads::callAsync(
            [this, target]
            {
                auto image = panel.renderToImage(2.f);

                if (image.isValid())
                    image.save(FilePath {target});
            });
    }

    static constexpr auto keybindingsDoc = "keybindings";

    emberstore::Database durable {emberstore::appDataDirectory("pond", "Rudder"),
                                  emberstore::Durability::Durable};
    emberstore::Database atomic {durable.directory(), emberstore::Durability::Atomic};
    emberstore::Collection<Keybindings> settingsStore {
        durable.collection<Keybindings>("settings")};
    grazer::MruStore mru {atomic};
    FrecencyStore frecency {atomic};
    RenameStore renames {durable};
    PanelFrameStore panelFrames {durable};
    FocusTracker focus;
    emberstore::Document<FavouriteList> favouritesDocument {favouritesPath()};
    FavouriteList favourites;
    // rudder.json's parse error, if any: the footer's standing message.
    std::string favouritesError;
    std::optional<emberstore::FileWatcher> favouritesWatcher;
    BackStack backStack;
    ContextProviders providers;
    Keybindings bindings;

    std::optional<GlobalHotKey> summonKey;
    std::optional<GlobalHotKey> backKey;
    bool capturing = false;

    bool panelVisible = false;
    std::string settingsError;
    std::string frontAppId;
    std::string query;
    std::string previewedId;
    std::string previewedAppId;
    std::vector<std::string> previewWindows;
    bool previewRaised = false;
    std::vector<platform::RunningApp> running;
    std::vector<RudderItem> installedItems;
    std::map<std::string, std::string> installedPaths;
    std::vector<RudderItem> allItems;
    std::vector<RudderItem> filtered;
    int selected = 0;
    Image noIcon;
    double hotkeyTime = 0.0;
    platform::RunningApp origin;
    bool capturingKeys = false;
    std::optional<RudderItem> pendingPreview;
    LateRaiseGuard lateRaise;
    PreviewUndo previewUndo;

    platform::ContextServer contextServer;
    std::map<int, std::string> connectionApps;
    std::map<int, protocol::Hello> connectionHellos;
    std::map<std::string, int> providerConnection;
    std::optional<Threads::Timer> accessibilityPoll;

    TrayIcon tray;
    PanelView panel;
    Window window {panel, panelWindowOptions()};
};

namespace
{
// `Rudder --snapshot <dir>`: render the panel with no window -- the switcher
// over the apps running now, then the unlock overlay -- to panel.png and
// unlock.png. Real pixels from the real views, for docs, on a machine where
// screencapture has no screen-recording grant.
int snapshot(const std::string& directory)
{
    return Apps::run(
        [directory]
        {
            auto panel = PanelView {};
            auto items = std::vector<RudderItem> {};
            auto apps = platform::runningApps();
            auto front = platform::frontmostApp();

            for (auto& app: apps)
            {
                auto item = RudderItem {};
                item.id = item.appId = app.id;
                item.title = app.name;
                item.subtitle = app.id == front.id ? "Current" : "";
                items.push_back(std::move(item));
            }

            panel.iconFor = [&](const std::string& id) -> const Image&
            {
                static const auto none = Image {};

                for (auto& app: apps)
                    if (app.id == id)
                        return platform::appIcon(app);

                return none;
            };

            panel.setBounds({0.f, 0.f, 640.f, 420.f});
            panel.resized();
            panel.setSettings(
                {"⌥Space", "⌥`", "⌃Q", "⌘D", "⌃R", "control+q", "command+d", "control+r", "", true, {}});

            auto selected = items.size() > 1 ? 1 : 0;
            auto windows = selected < (int) apps.size()
                               ? platform::windowTitles(apps[(std::size_t) selected])
                               : std::vector<std::string> {};
            panel.setModel({items, selected, windows, !windows.empty()});

            auto save = [&](const std::string& name)
            {
                panel.renderToImage(2.f);
                auto image = panel.renderToImage(2.f);

                if (image.isValid())
                    image.save(FilePath {directory + "/" + name});
            };

            save("panel.png");
            panel.setPermissionsResettable(platform::canResetPermissions());
            panel.setAccessibilityGate(true);
            save("unlock.png");
        });
}
} // namespace

int main(int argc, char* argv[])
{
    if (argc == 3 && std::string {argv[1]} == "--snapshot")
        return snapshot(argv[2]);

    return Apps::run<RudderApp>(argc, argv);
}
