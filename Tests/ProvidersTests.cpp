#include <Providers.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace rudder;

namespace
{
ProviderHello grazer()
{
    return {"Grazer", "audio.pond.grazer", 4242};
}
} // namespace

auto tUnknownAppHasNone = test("Providers/an app with no provider has no contexts")
    = []
{
    auto providers = ContextProviders {};
    check(providers.contextsFor("audio.pond.grazer").empty());
    check(!providers.activate("audio.pond.grazer", "tab-1"));
};

auto tPushReplaces = test("Providers/each push replaces the app's contexts") = []
{
    auto providers = ContextProviders {};
    auto changes = 0;
    providers.onChanged = [&] { ++changes; };

    providers.connect(grazer(), [](auto&, bool) {});
    providers.setContexts("audio.pond.grazer", {{"tab-1", "GitHub", "", ""}});
    providers.setContexts("audio.pond.grazer",
                          {{"tab-2", "Linear", "", ""}, {"tab-3", "Docs", "", ""}});

    check(changes == 3);
    check(providers.contextsFor("audio.pond.grazer").size() == 2);
    check(providers.contextsFor("audio.pond.grazer").front().id == "tab-2");
};

auto tPushBeforeHelloIgnored = test("Providers/contexts before hello are dropped") = []
{
    auto providers = ContextProviders {};
    providers.setContexts("audio.pond.grazer", {{"tab-1", "GitHub", "", ""}});

    check(providers.contextsFor("audio.pond.grazer").empty());
};

auto tActivateSends = test("Providers/activate goes back over the connection") = []
{
    auto providers = ContextProviders {};
    auto sent = std::string {};
    auto raised = false;

    providers.connect(grazer(),
                      [&](const std::string& id, bool raise)
                      {
                          sent = id;
                          raised = raise;
                      });
    providers.setContexts("audio.pond.grazer", {{"tab-1", "GitHub", "", ""}});

    check(providers.activate("audio.pond.grazer", "tab-1"));
    check(sent == "tab-1" && raised);
    check(providers.activate("audio.pond.grazer", "tab-1", false));
    check(!raised);
    check(!providers.activate("audio.pond.grazer", "tab-9"));
};

auto tCloseSends = test("Providers/close goes back to an app that closes contexts") = []
{
    auto providers = ContextProviders {};
    auto closed = std::vector<std::string> {};

    providers.connect(
        grazer(), [](auto&, bool) {}, [&](const std::string& id) { closed.push_back(id); });

    auto place = RudderContext {"place:/p", "p", "", ""};
    place.open = false;
    providers.setContexts("audio.pond.grazer", {{"tab-1", "GitHub", "", ""}, place});

    check(providers.close("audio.pond.grazer", "tab-1"));

    // Nothing to close: a context it never pushed, a place that isn't open.
    check(!providers.close("audio.pond.grazer", "tab-9"));
    check(!providers.close("audio.pond.grazer", "place:/p"));
    check(closed == std::vector<std::string> {"tab-1"});

    // Rudder keeps showing it until the app's push leaves it out.
    check(providers.contextsFor("audio.pond.grazer").size() == 2);
    providers.setContexts("audio.pond.grazer", {place});
    check(providers.contextsFor("audio.pond.grazer").size() == 1);
};

auto tCloseNeedsTheApp = test("Providers/close is refused for an app that can't close") = []
{
    auto providers = ContextProviders {};
    check(!providers.close("audio.pond.grazer", "tab-1"));

    providers.connect(grazer(), [](auto&, bool) {});
    providers.setContexts("audio.pond.grazer", {{"tab-1", "GitHub", "", ""}});

    check(!providers.close("audio.pond.grazer", "tab-1"));
};

namespace
{
RudderItem contextRow(bool open = true)
{
    auto item = RudderItem {};
    item.kind = "context";
    item.id = "ctx:tab-1";
    item.appId = "audio.pond.grazer";
    item.contextId = "tab-1";
    item.running = open;
    return item;
}
} // namespace

auto tPreviewDoesntRaise = test("Providers/a preview switches the context without raising")
    = []
{
    check(contextRequestFor(contextRow(), ContextPhase::preview)
          == ContextRequest {"tab-1", false});
};

auto tCommitRaises = test("Providers/Enter switches the context and raises") = []
{
    check(contextRequestFor(contextRow(), ContextPhase::commit)
          == ContextRequest {"tab-1", true});
};

auto tUnopenedNeverPreviewed = test("Providers/a place not open yet opens only on Enter")
    = []
{
    check(!contextRequestFor(contextRow(false), ContextPhase::preview));
    check(contextRequestFor(contextRow(false), ContextPhase::commit)
          == ContextRequest {"tab-1", true});
};

auto tOnlyContextsAsked = test("Providers/rows that aren't contexts ask no provider") = []
{
    auto app = contextRow();
    app.kind = "app";
    auto window = contextRow();
    window.kind = "window";

    check(!contextRequestFor(app, ContextPhase::preview));
    check(!contextRequestFor(app, ContextPhase::commit));
    check(!contextRequestFor(window, ContextPhase::commit));
};

auto tLateRaiseUndone = test("Providers/a previewed app raising after Enter is undone once")
    = []
{
    auto guard = LateRaiseGuard {};
    guard.reset();
    guard.previewed("audio.pond.grazer");
    guard.previewed("com.eacp.cowterm");
    guard.closedOnto("com.eacp.cowterm", 10.0);

    check(!guard.activated("com.eacp.cowterm", 10.005));
    check(guard.activated("audio.pond.grazer", 10.04)
          == std::optional<std::string> {"com.eacp.cowterm"});

    // Once: the target coming back, and Grazer after it, are left alone.
    check(!guard.activated("com.eacp.cowterm", 10.05));
    check(!guard.activated("audio.pond.grazer", 10.2));
};

auto tLateRaiseExpires = test("Providers/a previewed app activated later is the user's doing")
    = []
{
    auto guard = LateRaiseGuard {};
    guard.previewed("audio.pond.grazer");
    guard.closedOnto("com.eacp.cowterm", 10.0);

    check(
        !guard.activated("audio.pond.grazer", 10.0 + LateRaiseGuard::window + 0.01));
};

auto tLateRaiseOnlyPreviewed = test("Providers/an app that wasn't previewed is never undone")
    = []
{
    auto guard = LateRaiseGuard {};
    guard.previewed("audio.pond.grazer");
    guard.closedOnto("com.eacp.cowterm", 10.0);

    check(!guard.activated("com.tinyspeck.slackmacgap", 10.1));

    // Something else came first: the user has moved on.
    check(!guard.activated("audio.pond.grazer", 10.2));
};

auto tLateRaiseTargetPreviewed = test("Providers/the target itself is never undone") = []
{
    auto guard = LateRaiseGuard {};
    guard.previewed("audio.pond.grazer");
    guard.closedOnto("audio.pond.grazer", 10.0);

    check(!guard.activated("audio.pond.grazer", 10.1));
};

auto tResetForgetsPreviews = test("Providers/a new summon forgets the last panel's previews")
    = []
{
    auto guard = LateRaiseGuard {};
    guard.previewed("audio.pond.grazer");
    guard.reset();
    guard.closedOnto("com.eacp.cowterm", 10.0);

    check(!guard.activated("audio.pond.grazer", 10.1));
};

auto tDisconnectDrops = test("Providers/a closed connection drops its contexts") = []
{
    auto providers = ContextProviders {};
    providers.connect(grazer(), [](auto&, bool) {});
    providers.setContexts("audio.pond.grazer", {{"tab-1", "GitHub", "", ""}});
    providers.disconnect("audio.pond.grazer");

    check(providers.contextsFor("audio.pond.grazer").empty());
};

namespace
{
ProviderHello cowTerm()
{
    return {"CowTerm", "com.eacp.cowterm", 77};
}

RudderContext focusedContext(const std::string& id)
{
    auto context = RudderContext {id, id, "", ""};
    context.focused = true;
    return context;
}
} // namespace

auto tFocusedContexts = test("Providers/each provider's focused context, if it says") = []
{
    auto providers = ContextProviders {};
    providers.connect(grazer(), [](auto&, bool) {});
    providers.connect(cowTerm(), [](auto&, bool) {});
    providers.setContexts("audio.pond.grazer", {{"tab-1", "a", "", ""}, focusedContext("tab-2")});
    providers.setContexts("com.eacp.cowterm", {{"pane:1", "a", "", ""}});

    auto focused = providers.focusedContexts();

    check(focused.size() == 1);
    check(focused["audio.pond.grazer"] == "tab-2");
};

auto tEscUndoesPreviews = test("Providers/Esc puts previewed apps back on their context")
    = []
{
    auto undo = PreviewUndo {};
    undo.reset({{"com.eacp.cowterm", "pane:1"}, {"audio.pond.grazer", "tab-2"}});

    check(undo.undo().empty(), "nothing previewed, nothing to undo");

    undo.previewed("com.eacp.cowterm");
    undo.previewed("com.eacp.cowterm");
    undo.previewed("org.no.focus");

    auto back = undo.undo();

    check(back.size() == 1, "once per app; an app with no focused context can't go back");
    check(back.front() == std::pair<std::string, std::string> {"com.eacp.cowterm", "pane:1"});

    undo.reset({});
    check(undo.undo().empty(), "a new summon forgets");
};
