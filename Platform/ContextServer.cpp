#include "ContextServer.h"

namespace rudder::platform
{
namespace
{
void handle(ContextServer& server, int connection, protocol::Hello hello)
{
    server.onHello(connection, hello);
}

void handle(ContextServer& server, int connection, protocol::Contexts contexts)
{
    server.onContexts(connection, std::move(contexts.contexts));
}

void handle(ContextServer& server, int connection, protocol::RankingMaterials materials)
{
    server.onRankingMaterials(connection, std::move(materials));
}

// Activate, close and the materials request only ever go the other way; an
// app sending one is ignored like any other line Rudder has no use for.
template <typename Message>
void handle(ContextServer&, int, Message)
{
}
} // namespace

void ContextServer::deliver(int connection, protocol::Message message)
{
    std::visit([this, connection](auto&& payload)
               { handle(*this, connection, std::forward<decltype(payload)>(payload)); },
               std::move(message));
}
} // namespace rudder::platform
