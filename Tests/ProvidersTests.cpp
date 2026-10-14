#include <Providers.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace rudder;

namespace
{
ProviderHello wim()
{
    return {"Wim", "com.pond.wim", 4242};
}
} // namespace

auto tUnknownAppHasNone = test("Providers/an app with no provider has no contexts")
    = []
{
    auto providers = ContextProviders {};
    check(providers.contextsFor("com.pond.wim").empty());
    check(!providers.activate("com.pond.wim", "tab-1"));
};

auto tPushReplaces = test("Providers/each push replaces the app's contexts") = []
{
    auto providers = ContextProviders {};
    auto changes = 0;
    providers.onChanged = [&] { ++changes; };

    providers.connect(wim(), [](auto&, bool) {});
    providers.setContexts("com.pond.wim", {{"tab-1", "GitHub", "", ""}});
    providers.setContexts("com.pond.wim",
                          {{"tab-2", "Linear", "", ""}, {"tab-3", "Docs", "", ""}});

    check(changes == 3);
    check(providers.contextsFor("com.pond.wim").size() == 2);
    check(providers.contextsFor("com.pond.wim").front().id == "tab-2");
};

auto tPushBeforeHelloIgnored = test("Providers/contexts before hello are dropped") = []
{
    auto providers = ContextProviders {};
    providers.setContexts("com.pond.wim", {{"tab-1", "GitHub", "", ""}});

    check(providers.contextsFor("com.pond.wim").empty());
};

auto tActivateSends = test("Providers/activate goes back over the connection") = []
{
    auto providers = ContextProviders {};
    auto sent = std::string {};
    auto raised = false;

    providers.connect(wim(),
                      [&](const std::string& id, bool raise)
                      {
                          sent = id;
                          raised = raise;
                      });
    providers.setContexts("com.pond.wim", {{"tab-1", "GitHub", "", ""}});

    check(providers.activate("com.pond.wim", "tab-1"));
    check(sent == "tab-1" && raised);
    check(providers.activate("com.pond.wim", "tab-1", false));
    check(!raised);
    check(!providers.activate("com.pond.wim", "tab-9"));
};

namespace
{
RudderItem contextRow(bool open = true)
{
    auto item = RudderItem {};
    item.kind = "context";
    item.id = "ctx:tab-1";
    item.appId = "com.pond.wim";
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
    guard.previewed("audio.pond.wim");
    guard.previewed("com.eacp.cowterm");
    guard.closedOnto("com.eacp.cowterm", 10.0);

    check(!guard.activated("com.eacp.cowterm", 10.005));
    check(guard.activated("audio.pond.wim", 10.04) == std::optional<std::string> {"com.eacp.cowterm"});

    // Once: the target coming back, and Wim after it, are left alone.
    check(!guard.activated("com.eacp.cowterm", 10.05));
    check(!guard.activated("audio.pond.wim", 10.2));
};

auto tLateRaiseExpires = test("Providers/a previewed app activated later is the user's doing")
    = []
{
    auto guard = LateRaiseGuard {};
    guard.previewed("audio.pond.wim");
    guard.closedOnto("com.eacp.cowterm", 10.0);

    check(!guard.activated("audio.pond.wim", 10.0 + LateRaiseGuard::window + 0.01));
};

auto tLateRaiseOnlyPreviewed = test("Providers/an app that wasn't previewed is never undone")
    = []
{
    auto guard = LateRaiseGuard {};
    guard.previewed("audio.pond.wim");
    guard.closedOnto("com.eacp.cowterm", 10.0);

    check(!guard.activated("com.tinyspeck.slackmacgap", 10.1));

    // Something else came first: the user has moved on.
    check(!guard.activated("audio.pond.wim", 10.2));
};

auto tLateRaiseTargetPreviewed = test("Providers/the target itself is never undone") = []
{
    auto guard = LateRaiseGuard {};
    guard.previewed("audio.pond.wim");
    guard.closedOnto("audio.pond.wim", 10.0);

    check(!guard.activated("audio.pond.wim", 10.1));
};

auto tResetForgetsPreviews = test("Providers/a new summon forgets the last panel's previews")
    = []
{
    auto guard = LateRaiseGuard {};
    guard.previewed("audio.pond.wim");
    guard.reset();
    guard.closedOnto("com.eacp.cowterm", 10.0);

    check(!guard.activated("audio.pond.wim", 10.1));
};

auto tDisconnectDrops = test("Providers/a closed connection drops its contexts") = []
{
    auto providers = ContextProviders {};
    providers.connect(wim(), [](auto&, bool) {});
    providers.setContexts("com.pond.wim", {{"tab-1", "GitHub", "", ""}});
    providers.disconnect("com.pond.wim");

    check(providers.contextsFor("com.pond.wim").empty());
};
