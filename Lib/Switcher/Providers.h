#pragma once

#include "Items.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
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
// `activate {contextId, raise}`.
class ContextProviders
{
public:
    using SendActivate = std::function<void(const std::string& contextId, bool raise)>;

    void connect(const ProviderHello& hello, SendActivate send);
    void setContexts(const std::string& appId, std::vector<RudderContext> contexts);
    void disconnect(const std::string& appId);

    const std::vector<RudderContext>& contextsFor(const std::string& appId) const;

    // Asks the owning app to switch to the context, and with `raise` to
    // bring itself forward too. False when no provider is connected for it
    // (the caller then just activates the app).
    bool activate(const std::string& appId,
                  const std::string& contextId,
                  bool raise = true) const;

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

// What the panel asks a context's provider: a preview only switches the app
// to it (raise false; Rudder raises the app itself, and only while the row
// stays selected), Enter switches and raises. Nothing for rows that aren't a
// provider's context, and never a preview of a place that isn't open yet.
enum class ContextPhase
{
    preview,
    commit,
};

struct ContextRequest
{
    std::string contextId;
    bool raise = true;

    bool operator==(const ContextRequest&) const = default;
};

std::optional<ContextRequest> contextRequestFor(const RudderItem& item, ContextPhase phase);

// A preview's activation can land late: a provider built before previews
// stopped raising still raises its app on one, and macOS holds such an
// activation until Rudder lets go of the screen -- so it lands just after
// Enter, on top of the app the user picked. After the panel closes, the
// first activation of a previewed app (other than the target) within
// `window` seconds is a stale preview: the caller puts the target back in
// front, once.
class LateRaiseGuard
{
public:
    static constexpr double window = 0.75;

    // A summon: forget the last panel's previews.
    void reset();

    // The panel previewed one of `appId`'s rows.
    void previewed(const std::string& appId);

    // The panel closed onto `targetAppId` (Enter, or Esc back to the
    // origin) at `now`.
    void closedOnto(const std::string& targetAppId, double now);

    // An app activated at `now` with the panel closed: the app to put back
    // in front when it's a stale preview, else nothing.
    std::optional<std::string> activated(const std::string& appId, double now);

private:
    std::set<std::string> previewedApps;
    std::string target;
    double deadline = 0.0;
};
} // namespace rudder
