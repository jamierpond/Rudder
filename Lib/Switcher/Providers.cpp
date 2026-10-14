#include "Providers.h"

#include <algorithm>
#include <utility>

namespace rudder
{
void ContextProviders::connect(const ProviderHello& hello, SendActivate send)
{
    providers[hello.bundleId] = {hello, std::move(send), {}};
    onChanged();
}

void ContextProviders::setContexts(const std::string& appId,
                                   std::vector<RudderContext> contexts)
{
    auto found = providers.find(appId);

    if (found == providers.end())
        return;

    found->second.contexts = std::move(contexts);
    onChanged();
}

void ContextProviders::disconnect(const std::string& appId)
{
    if (providers.erase(appId) > 0)
        onChanged();
}

const std::vector<RudderContext>&
    ContextProviders::contextsFor(const std::string& appId) const
{
    static const auto none = std::vector<RudderContext> {};
    auto found = providers.find(appId);
    return found != providers.end() ? found->second.contexts : none;
}

bool ContextProviders::activate(const std::string& appId,
                                const std::string& contextId,
                                bool raise) const
{
    auto found = providers.find(appId);

    if (found == providers.end())
        return false;

    auto& contexts = found->second.contexts;
    auto known = std::any_of(contexts.begin(),
                             contexts.end(),
                             [&](const RudderContext& context)
                             { return context.id == contextId; });

    if (!known || !found->second.send)
        return false;

    found->second.send(contextId, raise);
    return true;
}

std::optional<ContextRequest> contextRequestFor(const RudderItem& item, ContextPhase phase)
{
    if (item.kind != "context" || item.contextId.empty())
        return std::nullopt;

    if (phase == ContextPhase::commit)
        return ContextRequest {item.contextId, true};

    // running mirrors the context's `open`: a place that isn't open yet is
    // only ever opened on Enter.
    if (!item.running)
        return std::nullopt;

    return ContextRequest {item.contextId, false};
}

void LateRaiseGuard::reset()
{
    previewedApps.clear();
    target.clear();
    deadline = 0.0;
}

void LateRaiseGuard::previewed(const std::string& appId)
{
    previewedApps.insert(appId);
}

void LateRaiseGuard::closedOnto(const std::string& targetAppId, double now)
{
    target = targetAppId;
    deadline = now + window;
}

std::optional<std::string> LateRaiseGuard::activated(const std::string& appId, double now)
{
    if (target.empty())
        return std::nullopt;

    if (now > deadline)
    {
        reset();
        return std::nullopt;
    }

    // The target's own activation lands first; keep watching after it.
    if (appId == target)
        return std::nullopt;

    // Whatever comes next settles it: a stale preview gets undone, anything
    // else is the user moving on.
    auto stale = previewedApps.contains(appId);
    auto putBack = target;
    reset();

    if (!stale)
        return std::nullopt;

    return putBack;
}
} // namespace rudder
