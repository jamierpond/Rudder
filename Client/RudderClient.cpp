#include "RudderClient.h"

namespace rudder
{
namespace
{
using Reply = std::optional<protocol::Message>;

Reply handle(RudderClient& client, const protocol::Activate& activate)
{
    if (activate.raise)
        client.onActivate(activate.contextId);
    else
        client.onPreview(activate.contextId);

    return std::nullopt;
}

Reply handle(RudderClient& client, const protocol::Close& close)
{
    client.onClose(close.contextId);
    return std::nullopt;
}

Reply handle(RudderClient& client, const protocol::RankingMaterialsRequest&)
{
    auto materials = client.onRankingMaterialsRequest();

    if (!materials)
        return std::nullopt;

    return *materials;
}

// Hello, contexts and materials only ever go from the app to Rudder.
template <typename Message>
Reply handle(RudderClient&, const Message&)
{
    return std::nullopt;
}
} // namespace

std::optional<protocol::Message> RudderClient::handleMessageFromRudder(const protocol::Message& message)
{
    return std::visit([this](const auto& payload) { return handle(*this, payload); }, message);
}
} // namespace rudder
