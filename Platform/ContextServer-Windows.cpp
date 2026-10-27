#include "ContextServer.h"

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/Logging.h>
#include <eacp/Network/IPC/Channel.h>

#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace rudder::platform
{
namespace
{
// Every blocking wait is sliced, so a teardown never waits longer than one.
constexpr auto acceptSlice = eacp::Time::MS {200};
constexpr auto interruptRetry = eacp::Time::MS {5};
} // namespace

// The pipe has no readiness source to hang on the main thread's loop the way
// a dispatch source hangs on a socket, so each connection gets a reader
// thread parked in a blocking read, and an acceptor thread waits for the next
// app. They only ever hand finished messages to the main thread, where every
// callback runs. `stop` is shared with those deliveries, so one landing after
// the server is gone does nothing.
struct ContextServer::Native
{
    struct Shared
    {
        // Main thread only.
        ContextServer* owner = nullptr;
        std::atomic<bool> stop = false;
    };

    struct Connection
    {
        explicit Connection(eacp::IPC::Channel accepted)
            : channel(std::move(accepted))
        {
        }

        eacp::IPC::Channel channel;
        std::atomic<bool> readerDone = false;
        std::thread reader;
    };

    using ConnectionPtr = std::shared_ptr<Connection>;

    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    std::optional<eacp::IPC::ChannelServer> server;
    std::thread acceptor;

    // By the id the callbacks know the connection by. The acceptor adds,
    // the main thread removes; the lock is only ever held for the lookup.
    std::mutex mutex;
    std::map<int, ConnectionPtr> connections;

    // The acceptor thread's alone.
    int nextId = 1;

    ConnectionPtr find(int id)
    {
        auto guard = std::lock_guard {mutex};
        auto found = connections.find(id);
        return found != connections.end() ? found->second : nullptr;
    }

    ConnectionPtr take(int id)
    {
        auto guard = std::lock_guard {mutex};
        auto found = connections.find(id);

        if (found == connections.end())
            return nullptr;

        auto connection = found->second;
        connections.erase(found);
        return connection;
    }

    static void onMain(const std::shared_ptr<Shared>& shared,
                       std::function<void(ContextServer&)> action)
    {
        eacp::Threads::callAsync(
            [shared, action = std::move(action)]
            {
                if (!shared->stop && shared->owner != nullptr)
                    action(*shared->owner);
            });
    }

    static void read(const std::shared_ptr<Shared>& shared,
                     const ConnectionPtr& connection,
                     int id)
    {
        auto lines = protocol::LineBuffer {};
        char buffer[16384];

        try
        {
            while (!shared->stop)
            {
                auto count =
                    connection->channel.receive(buffer, (int) sizeof(buffer));

                if (count == 0 || shared->stop)
                    break;

                for (auto& line: lines.append({buffer, (std::size_t) count}))
                {
                    auto message = protocol::decode(line);

                    if (!message)
                        continue;

                    // Activate and close only ever go the other way; an app
                    // sending one is ignored like any other line Rudder has
                    // no use for.
                    if (auto* hello = std::get_if<protocol::Hello>(&*message))
                        onMain(shared,
                               [id, hello = *hello](ContextServer& server)
                               {
                                   if (server.native->find(id) != nullptr)
                                       server.onHello(id, hello);
                               });
                    else if (auto* contexts =
                                 std::get_if<protocol::Contexts>(&*message))
                        onMain(shared,
                               [id, list = std::move(contexts->contexts)](
                                   ContextServer& server) mutable
                               {
                                   if (server.native->find(id) != nullptr)
                                       server.onContexts(id, std::move(list));
                               });
                }
            }
        }
        catch (const eacp::IPC::Error&)
        {
            // A broken pipe means the app is gone, the same as a clean close.
        }

        connection->readerDone = true;

        if (!shared->stop)
            onMain(shared,
                   [id](ContextServer& server) { server.native->close(id); });
    }

    // A connection is listed before its reader starts, so the main thread
    // never hears from one it can't find, and a teardown finds every reader.
    void acceptLoop()
    {
        while (!shared->stop)
        {
            auto accepted = std::optional<eacp::IPC::Channel> {};

            try
            {
                accepted = server->accept(acceptSlice);
            }
            catch (const eacp::IPC::Error& error)
            {
                eacp::LOG("Rudder: contexts channel stopped accepting: ",
                          error.what());
                return;
            }

            if (!accepted)
                continue;

            auto id = nextId++;
            auto connection = std::make_shared<Connection>(std::move(*accepted));

            {
                auto guard = std::lock_guard {mutex};
                connections[id] = connection;
            }

            connection->reader = std::thread([shared = shared, connection, id]
                                             { read(shared, connection, id); });
        }
    }

    // Main thread. Knocks until the reader has left -- a cancel only reaches
    // a read already in flight -- then joins it.
    static void retire(const ConnectionPtr& connection)
    {
        while (connection->reader.joinable() && !connection->readerDone)
        {
            connection->channel.interrupt();
            eacp::Time::sleep(interruptRetry);
        }

        if (connection->reader.joinable())
            connection->reader.join();

        connection->channel.close();
    }

    void close(int id)
    {
        auto connection = take(id);

        if (connection == nullptr)
            return;

        retire(connection);
        shared->owner->onClosed(id);
    }
};

ContextServer::ContextServer()
    : native(std::make_unique<Native>())
{
    native->shared->owner = this;
}

ContextServer::~ContextServer()
{
    native->shared->stop = true;
    native->shared->owner = nullptr;

    if (native->acceptor.joinable())
        native->acceptor.join();

    // The acceptor has stopped, so nothing is added from here on.
    for (auto& [id, connection]: native->connections)
        Native::retire(connection);

    native->connections.clear();
}

bool ContextServer::start(const std::string& endpoint)
{
    // A Rudder that died leaves nothing to sweep: the pipe went with it.
    // Another live Rudder holding the name is the one failure.
    try
    {
        native->server.emplace(endpoint);
    }
    catch (const eacp::IPC::Error& error)
    {
        eacp::LOG("Rudder: contexts channel unavailable: ", error.what());
        return false;
    }

    native->acceptor =
        std::thread([server = native.get()] { server->acceptLoop(); });
    return true;
}

void ContextServer::send(int connection, const protocol::Message& message)
{
    auto found = native->find(connection);

    if (found == nullptr)
        return;

    try
    {
        found->channel.send(protocol::encode(message));
    }
    catch (const eacp::IPC::Error&)
    {
        native->close(connection);
    }
}
} // namespace rudder::platform
