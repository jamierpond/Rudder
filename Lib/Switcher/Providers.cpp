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
                                const std::string& contextId) const
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

    found->second.send(contextId);
    return true;
}
} // namespace rudder
