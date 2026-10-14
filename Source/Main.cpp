#include "BackStack.h"
#include "Icon.h"
#include "Keybinding.h"
#include "Mru.h"
#include "PanelView.h"
#include "Platform.h"
#include "Providers.h"
#include "QuickWindow.h"
#include "Ranking.h"

#include <eacp/Core/App/LoginItem.h>
#include <eacp/UI/UI.h>
#include <emberstore/AppDatabase.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <optional>
#include <string>
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
        watchInstalledApps();
        loadKeybindings();
        registerHotKeys();
        observeWorkspace();

        tray.setTemplateRendering(true);
        tray.setIcon(makeTrayIcon());
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

        // As Tamby does: a launch without the grant opens the pane for it.
        if (!platform::accessibilityTrusted(false))
            platform::openAccessibilitySettings();

        platform::announceStarted();
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
            Threads::callAsync([this, chosen] { openItem(chosen); });
        };
        panel.onQuitSelected = [this] { Threads::callAsync([this] { quitSelected(); }); };

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

        // Starting the drag opens the pane, so the list is there to drop on.
        panel.onUnlockDrag = [this]
        {
            platform::startFileDrag(panel, platform::appBundlePath());
            platform::openAccessibilitySettings();
        };

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

        options.width = (int) panelWidth;
        options.height = (int) panelHeight;
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
        auto& other = action == "summon" ? bindings.back : bindings.summon;

        if (canonical == other)
        {
            settingsError = "That chord is already bound to the other action.";
            return;
        }

        (action == "summon" ? bindings.summon : bindings.back) = canonical;
        settingsStore.doc(keybindingsDoc).set(bindings);
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
            if (!panelVisible)
                recordActivation(app.id);
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
            providers.disconnect(app.id);
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

    void recordActivation(const std::string& appId)
    {
        mru.touch(appId);
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

    // --- Key capture ------------------------------------------------------------

    // While the panel is up the keyboard is ours, whichever app the preview
    // has activated. The two hotkeys pass through so they still toggle and go
    // back -- except while a new chord is being recorded.
    void startKeyCapture()
    {
        if (!platform::accessibilityTrusted(false))
            return;

        auto capture = platform::KeyCapture {};
        capture.onKey = [this](const KeyEvent& event) { return onCapturedKey(event); };
        capture.onMouseDownOutside = [this]
        { Threads::callAsync([this] { cancel(); }); };

        capturingKeys = platform::startKeyCapture(window, std::move(capture));
    }

    void stopKeyCapture()
    {
        if (!capturingKeys)
            return;

        capturingKeys = false;
        platform::stopKeyCapture();
    }

    bool onCapturedKey(const KeyEvent& event)
    {
        if (capturing && event.type == KeyEventType::Down)
            logEvent(("captured key " + std::to_string(event.keyCode)).c_str());

        if (!capturing && (isChord(event, bindings.summon) || isChord(event, bindings.back)))
            return false;

        if (event.type == KeyEventType::Down)
            panel.keyDown(event);

        return true;
    }

    static bool isChord(const KeyEvent& event, const std::string& text)
    {
        auto chord = parseChord(text);

        return chord && keyCodeFor(chord->key) == event.keyCode
               && chord->control == event.modifiers.control
               && chord->option == event.modifiers.alt
               && chord->shift == event.modifiers.shift
               && chord->command == event.modifiers.command;
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

        panel.reset(showSettings);
        resetPanel();
        settingsError.clear();
        publishSettings();

        if (!window.isVisible())
            window.setPosition(platform::panelOrigin(panelWidth, panelHeight));

        window.setVisible(true);
        panelVisible = true;

        quick_window::raiseAboveFullScreen(window);
        quick_window::takeKeyForTyping(window);
        // Without this, macOS routes every keystroke to the app underneath.
        quick_window::activateForPanel();

        panel.focus();
        syncAccessibilityGate(true);
        startKeyCapture();
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
            item.contexts = providers.contextsFor(app.id);
            item.subtitle = app.id == frontAppId ? "Current" : "";
            allItems.push_back(std::move(item));
        }

        // The app the panel was summoned over is on top whatever its stamp
        // says, so the previous app is always the second row.
        allItems = orderByRecency(std::move(allItems),
                                  [this](const std::string& id)
                                  {
                                      return id == frontAppId ? INT64_MAX
                                                              : mru.lastUsed(id);
                                  });

        auto closed = orderByRecency(installedItems,
                                     [this](const std::string& id)
                                     { return mru.lastUsed(id); });
        allItems = mergeInstalled(std::move(allItems), closed);

        filtered = rankItems(allItems, query);
        selected = initialSelection(filtered, query, frontAppId);
        pendingPreview.reset();
        previewedId.clear();
        previewWindows.clear();
        previewRaised = false;
        publishResults();
    }

    void setQuery(const std::string& text)
    {
        if (!panelVisible)
            return;

        query = text;
        logEvent(("query '" + query + "'").c_str());
        filtered = rankItems(allItems, query);
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

    // Wim's palette preview, for windows: whenever the results change -- a
    // keystroke or an arrow -- the selection comes to the front behind the
    // panel at once. Skipped when the selection didn't change. The app the
    // panel was summoned over previews as the screen was: its origin window.
    void previewSelection()
    {
        if (filtered.empty())
            return;

        auto& item = filtered[(std::size_t) selected];

        if (item.appId == previewedId)
            return;

        previewedId = item.appId;
        previewWindows.clear();
        previewRaised = false;

        // A closed app has no window to show: whatever was previewed last
        // stays where it is. Nothing is ever launched for a preview.
        if (!item.running)
            return;

        // After the next frame: the list shows the keystroke first, and the
        // activation (an AX round trip to the target) follows at once.
        pendingPreview = item;
        panel.repaint();
    }

    // The one place a preview touches another app.
    void previewNow(const RudderItem& item)
    {
        if (!panelVisible || previewedId != item.appId)
            return;

        auto app = findRunningNow(item.appId);
        auto action = previewActionFor(item, app.has_value(), frontAppId);

        if (action == PreviewAction::none)
            return;

        auto start = uptime();

        // Under key capture the preview can activate for real -- the only
        // way the window gets above everything -- and typing still lands in
        // the panel. Without it (no grant), nothing is raised: the pane shows
        // the selection instead.
        if (capturingKeys)
        {
            previewRaised = platform::previewApp(*app);

            if (action == PreviewAction::restoreOrigin)
                platform::restorePreviewOrigin();
        }

        logEvent(("preview " + app->name + " " + millis(uptime() - start)
                  + (previewRaised ? "" : " (not raised)"))
                     .c_str());

        previewWindows = platform::windowTitles(*app);
        publishResults();
    }

    // ⌘Q: quit the selection, cmd-tab style. The panel stays up; the row
    // turns "not running" when the app is actually gone (markQuit), so an app
    // that refuses -- an unsaved-changes dialog -- simply stays running.
    void quitSelected()
    {
        if (!panelVisible || filtered.empty())
            return;

        auto& item = filtered[(std::size_t) selected];

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

        if (previewedId == appId)
            previewedId.clear();

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

        openItem(filtered[(std::size_t) selected]);
    }

    void openItem(const RudderItem& item)
    {
        if (!panelVisible)
            return;

        // A context goes to its provider as `activate {contextId}`; the app
        // is activated either way so the context's window comes forward.
        if (item.kind == "context")
            providers.activate(item.appId, item.contextId);

        auto app = findRunningNow(item.appId);

        // Not running: Enter is the only thing that launches it. The launch
        // activates it, so focus isn't handed back anywhere on the way out.
        if (!app && !item.path.empty())
        {
            logEvent(("launch -> " + item.title).c_str());
            quick_window::rememberAppForHandoff(0);
            hidePanel();
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
        platform::activateApp(*app);
        recordActivation(app->id);
    }

    // Esc / losing focus: put the screen back exactly as it was.
    void cancel()
    {
        if (!panelVisible)
            return;

        logEvent("cancel");

        // A preview activated another app; hand the screen back as it was.
        auto previewed = !previewedId.empty() && previewedId != frontAppId;
        hidePanel();

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
                           platform::accessibilityTrusted(false),
                           settingsError});
    }

    static double uptime()
    {
        return (double) clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1e9;
    }

    static std::string millis(double seconds)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%.1f ms", seconds * 1000.0);
        return text;
    }

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

    static constexpr auto panelWidth = 640.f;
    static constexpr auto panelHeight = 420.f;
    static constexpr auto keybindingsDoc = "keybindings";

    emberstore::Database durable {emberstore::appDataDirectory("pond", "Rudder"),
                                  emberstore::Durability::Durable};
    emberstore::Database atomic {durable.directory(), emberstore::Durability::Atomic};
    emberstore::Collection<Keybindings> settingsStore {
        durable.collection<Keybindings>("settings")};
    wim::MruStore mru {atomic};
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
            panel.setSettings({"⌥Space", "⌥`", true, {}});

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
