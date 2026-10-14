#include "RudderClient.h"

namespace rudder
{
// Rudder has no Linux build, so there is never anyone to connect to.
struct RudderClient::Native
{
};

RudderClient::RudderClient(protocol::Hello)
    : native(std::make_unique<Native>())
{
}

RudderClient::~RudderClient() = default;

void RudderClient::pushContexts(std::vector<protocol::Context>) {}

bool RudderClient::isConnected() const
{
    return false;
}
} // namespace rudder
