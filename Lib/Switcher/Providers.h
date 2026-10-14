#pragma once

#include "Items.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace rudder
{
// What a provider app says first on the context socket: `hello {app,
// bundleId, pid}`.
struct ProviderHello
{
    std::string app;
    std::string bundleId;
    std::int64_t pid = 0;

    MIRO_REFLECT(app, bundleId, pid)
};

// Every connected provider and the contexts it last pushed, keyed by app id
// (the bundle id -- the same id the panel's app rows carry). The socket layer
// to come feeds this: hello -> connect, `contexts [...]` -> setContexts, the
// connection closing -> disconnect. `send` is that connection's way back, for
// `activate {contextId}`.
class ContextProviders
{
public:
    using SendActivate = std::function<void(const std::string& contextId)>;

    void connect(const ProviderHello& hello, SendActivate send);
    void setContexts(const std::string& appId, std::vector<RudderContext> contexts);
    void disconnect(const std::string& appId);

    const std::vector<RudderContext>& contextsFor(const std::string& appId) const;

    // Asks the owning app to bring the context forward. False when no
    // provider is connected for it (the caller then just activates the app).
    bool activate(const std::string& appId, const std::string& contextId) const;

    // Fired after any change, so an open panel can re-rank.
    std::function<void()> onChanged = [] {};

private:
    struct Provider
    {
        ProviderHello hello;
        SendActivate send;
        std::vector<RudderContext> contexts;
    };

    std::map<std::string, Provider> providers;
};
} // namespace rudder
