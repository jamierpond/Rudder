#include <ContextServer.h>
#include <RudderClient.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/Time.h>
#include <eacp/Core/Utils/WinInclude.h>

#include <cstdio>
#include <functional>
#include <map>
#include <memory>

using namespace nano;
using namespace rudder;

namespace
{
// Runs the main thread's loop, where both ends deliver, until `done` says so.
bool pumpUntil(const std::function<bool()>& done)
{
    auto deadline = eacp::Time::Deadline {eacp::Time::MS {5000}};

    while (!done())
    {
        if (deadline.expired())
            return false;

        eacp::Threads::runEventLoopFor(eacp::Time::MS {20});
    }

    return true;
}

// What Rudder's side heard, by connection.
struct Heard
{
    std::map<int, protocol::Hello> hellos;
    std::map<int, std::vector<protocol::Context>> contexts;
    std::vector<int> closed;
};

std::unique_ptr<platform::ContextServer> serve(Heard& heard)
{
    auto server = std::make_unique<platform::ContextServer>();
    server->onHello = [&](int id, const protocol::Hello& hello)
    { heard.hellos[id] = hello; };
    server->onContexts = [&](int id, std::vector<protocol::Context> list)
    { heard.contexts[id] = std::move(list); };
    server->onClosed = [&](int id) { heard.closed.push_back(id); };

    if (!server->start())
        return nullptr;

    return server;
}

// What a (re)started Rudder does to wake the apps waiting for it.
void announceStarted()
{
    auto name = L"Local\\" + eacp::Strings::widen(protocol::startedNotification);
    auto event = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
    SetEvent(event);
    CloseHandle(event);
}
} // namespace

auto tClientAndServerTalk =
    test("Transport/a client says hello, pushes contexts and hears activate and close") = []
{
    auto heard = Heard {};
    auto server = serve(heard);

    // A live Rudder owns the channel: there is nothing to test against.
    if (server == nullptr)
    {
        std::fprintf(stderr,
                     "skipped: a running Rudder holds the contexts channel\n");
        return;
    }

    auto client = RudderClient {{"Tests", "com.pond.rudder.tests", 0, true}};
    auto activated = std::string {};
    auto previewed = std::string {};
    auto closed = std::string {};
    client.onActivate = [&](const std::string& id) { activated = id; };
    client.onPreview = [&](const std::string& id) { previewed = id; };
    client.onClose = [&](const std::string& id) { closed = id; };

    auto pushed = std::vector<protocol::Context> {
        {"1", "one", "", ""}, {"2", "two", "sub", "https://x.y"}};
    client.pushContexts(pushed);

    check(
        pumpUntil([&] { return !heard.hellos.empty() && !heard.contexts.empty(); }));
    check(client.isConnected());

    if (heard.hellos.empty())
        return;

    auto [id, hello] = *heard.hellos.begin();
    check(hello.app == "Tests");
    check(hello.bundleId == "com.pond.rudder.tests");
    check(hello.pid == (std::int64_t) GetCurrentProcessId());
    check(hello.canClose);
    check(heard.contexts[id] == pushed);

    server->send(id, protocol::Activate {"2", true});
    check(pumpUntil([&] { return activated == "2"; }));

    server->send(id, protocol::Activate {"1", false});
    check(pumpUntil([&] { return previewed == "1"; }));
    check(activated == "2");

    server->send(id, protocol::Close {"1"});
    check(pumpUntil([&] { return closed == "1"; }));

    // Rudder quits and starts again: the app reconnects by itself and sends
    // the latest list without being asked.
    server.reset();
    check(pumpUntil([&] { return !client.isConnected(); }));

    auto again = Heard {};
    server = serve(again);
    check(server != nullptr);
    announceStarted();

    check(
        pumpUntil([&] { return !again.hellos.empty() && !again.contexts.empty(); }));

    if (!again.contexts.empty())
        check(again.contexts.begin()->second == pushed);
};

auto tClosingIsHeard =
    test("Transport/a client going away closes its connection") = []
{
    auto heard = Heard {};
    auto server = serve(heard);

    if (server == nullptr)
        return;

    {
        auto client = RudderClient {{"Tests", "com.pond.rudder.tests", 0}};
        check(pumpUntil([&] { return !heard.hellos.empty(); }));
    }

    check(pumpUntil([&] { return heard.closed.size() == 1; }));
};
