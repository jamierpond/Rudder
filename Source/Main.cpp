#include "BackStack.h"
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

// A helm-ish ring with a hub, drawn so the app needs no asset file. The menu
// bar renders it as a template (alpha only, system tinted).
Image makeTrayIcon()
{
    constexpr int size = 36;
    auto image = Image(size, size);

    auto center = (size - 1) / 2.f;

    for (auto y = 0; y < size; ++y)
    {
        for (auto x = 0; x < size; ++x)
        {
            auto dx = (float) x - center;
            auto dy = (float) y - center;
            auto distance = std::sqrt(dx * dx + dy * dy);

            auto ring = std::clamp(2.2f - std::abs(distance - 12.f), 0.f, 1.f);
            auto hub = std::clamp(4.5f - distance, 0.f, 1.f);

            // Four spokes, out past the ring like a ship's wheel's handles.
            auto spoke = 0.f;

            if (distance > 4.f && distance < 17.f)
                spoke = std::clamp(1.4f - std::min(std::abs(dx), std::abs(dy)), 0.f, 1.f);

            auto coverage = std::max({ring, hub, spoke});

            if (coverage > 0.f)
                image.set(x, y, Color(0.f, 0.f, 0.f, coverage));
        }
    }

    return image;
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
        quick_window::installReopenHandler([this] { reveal(false); });

        bindPanel();
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
        platform::accessibilityTrusted(true);
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

            return noIcon;
        };

        panel.onQueryChange = [this](const std::string& text) { setQuery(text); };
        panel.onMove = [this](int delta) { moveSelection(delta); };
        panel.onAccept = [this] { Threads::callAsync([this] { activateSelection(); }); };
        panel.onChoose = [this](int index)
        {
            if (index < 0 || index >= (int) filtered.size())
                return;

            auto chosen = filtered[(std::size_t) index];
            Threads::callAsync([this, chosen] { openItem(chosen); });
        };
        panel.onCancel = [this] { Threads::callAsync([this] { cancel(); }); };

        // While a chord is recorded the old ones must not fire -- pressing
        // ⌥Space to rebind it would otherwise close the panel.
        panel.onCaptureStart = [this] { setCapturing(true); };
        panel.onCaptureEnd = [this](const std::string& action, const std::string& chord)
        {
            settingsError.clear();

            if (!chord.empty())
                setBinding(action, chord);

            setCapturing(false);
            publishSettings();
        };

        panel.onRequestAccessibility = [this]
        {
            platform::accessibilityTrusted(true);
            publishSettings();
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

        events.onActivated = [this](const platform::RunningApp& app)
        {
            mru.touch(app.id);
            backStack.record(app.id);
        };

        // Rendering the icon now keeps the next summon free of it.
        events.onLaunched = [](const platform::RunningApp& app)
        { platform::appIcon(app); };

        events.onTerminated = [this](const platform::RunningApp& app)
        {
            backStack.forget(app.id);
            platform::forgetIcon(app.id);
            providers.disconnect(app.id);
        };

        platform::observeWorkspace(std::move(events));
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
        platform::rememberPreviewOrigin();

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
        logEvent("reveal: shown");
    }

    void hidePanel()
    {
        if (!panelVisible)
            return;

        logEvent("hidePanel: begin");
        panelVisible = false;
        setCapturing(false);
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

    void onPanelActivationChanged(bool isKey)
    {
        if (isKey || !window.isVisible())
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
        frontAppId = platform::frontmostApp().id;
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

        filtered = allItems;
        selected = initialSelection(filtered, query, frontAppId);
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

    // Raise the selection's window behind the panel as it changes, without
    // activating it -- the panel keeps key, so typing carries on. Only on a
    // real change: re-raising the same window per keystroke is wasted IPC.
    // The app the panel was summoned over is the screen as it was, so it
    // previews by restoring the origin window.
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

        auto app = findRunning(item.appId);

        if (!app)
            return;

        if (app->id == frontAppId)
        {
            platform::restorePreviewOrigin();
            previewRaised = true;
        }
        else
            previewRaised = platform::previewApp(*app);

        previewWindows = platform::windowTitles(*app);
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

        auto app = findRunning(item.appId);

        if (!app)
        {
            cancel();
            return;
        }

        logEvent(("open -> " + app->name).c_str());
        quick_window::rememberAppForHandoff(app->pid);
        hidePanel();
        platform::activateApp(*app);
    }

    // Esc / losing focus: put the screen back exactly as it was.
    void cancel()
    {
        if (!panelVisible)
            return;

        if (!previewedId.empty() && previewedId != frontAppId)
            platform::restorePreviewOrigin();

        hidePanel();
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
    std::vector<RudderItem> allItems;
    std::vector<RudderItem> filtered;
    int selected = 0;
    Image noIcon;

    TrayIcon tray;
    PanelView panel;
    Window window {panel, panelWindowOptions()};
};

int main(int argc, char* argv[])
{
    return Apps::run<RudderApp>(argc, argv);
}
