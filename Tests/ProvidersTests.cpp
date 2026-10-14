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

    providers.connect(wim(), [](auto&) {});
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

    providers.connect(wim(), [&](const std::string& id) { sent = id; });
    providers.setContexts("com.pond.wim", {{"tab-1", "GitHub", "", ""}});

    check(providers.activate("com.pond.wim", "tab-1"));
    check(sent == "tab-1");
    check(!providers.activate("com.pond.wim", "tab-9"));
};

auto tDisconnectDrops = test("Providers/a closed connection drops its contexts") = []
{
    auto providers = ContextProviders {};
    providers.connect(wim(), [](auto&) {});
    providers.setContexts("com.pond.wim", {{"tab-1", "GitHub", "", ""}});
    providers.disconnect("com.pond.wim");

    check(providers.contextsFor("com.pond.wim").empty());
};
