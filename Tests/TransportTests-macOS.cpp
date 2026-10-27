#include <ContextServer.h>
#include <RudderClient.h>

#include <NanoTest/NanoTest.h>

#include <CoreFoundation/CoreFoundation.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unistd.h>

using namespace nano;
using namespace rudder;

namespace
{
constexpr auto pumpDeadline = std::chrono::seconds {5};
constexpr auto pumpSliceSeconds = 0.01;
constexpr auto serverDeafInterval = std::chrono::milliseconds {500};
constexpr auto contextsInLargePush = 3000;
constexpr auto titlePaddingBytes = 60;

using Clock = std::chrono::steady_clock;

// Runs the main run loop, where both ends deliver, until `isDone` says so.
bool pumpRunLoopUntil(const std::function<bool()>& isDone)
{
    auto deadline = Clock::now() + pumpDeadline;

    while (!isDone())
    {
        if (Clock::now() > deadline)
            return false;

        CFRunLoopRunInMode(kCFRunLoopDefaultMode, pumpSliceSeconds, true);
    }

    return true;
}

// A fresh folder per test, kept short: sun_path holds 104 bytes.
struct TemporarySocketFolder
{
    std::filesystem::path folder;

    TemporarySocketFolder()
    {
        auto pattern = std::string {"/tmp/rudder-XXXXXX"};

        if (mkdtemp(pattern.data()) != nullptr)
            folder = pattern;
    }

    ~TemporarySocketFolder()
    {
        auto ignored = std::error_code {};
        std::filesystem::remove_all(folder, ignored);
    }

    std::string getSocketPath() const { return (folder / "rudder.sock").string(); }
};

struct Heard
{
    std::map<int, protocol::Hello> hellos;
    std::map<int, std::vector<protocol::Context>> latestContexts;
    std::vector<std::vector<protocol::Context>> everyContextsList;
};

std::unique_ptr<platform::ContextServer> startServer(Heard& heard, const std::string& path)
{
    auto server = std::make_unique<platform::ContextServer>();
    server->onHello = [&](int id, const protocol::Hello& hello) { heard.hellos[id] = hello; };
    server->onContexts = [&](int id, std::vector<protocol::Context> list)
    {
        heard.everyContextsList.push_back(list);
        heard.latestContexts[id] = std::move(list);
    };

    if (!server->start(path))
        return nullptr;

    return server;
}

std::vector<protocol::Context> makeManyContexts(const std::string& tag)
{
    auto contexts = std::vector<protocol::Context> {};
    auto padding = std::string(titlePaddingBytes, 'x');

    for (auto index = 0; index < contextsInLargePush; ++index)
    {
        auto id = tag + std::to_string(index);
        contexts.push_back({id, "title " + id + padding, "/Users/someone/projects/" + id, ""});
    }

    return contexts;
}

bool hasHeardOnlyLatest(const Heard& heard, const std::vector<protocol::Context>& expected)
{
    return heard.latestContexts.size() == 1 && heard.latestContexts.begin()->second == expected;
}

void waitWithoutPumping(std::chrono::milliseconds interval)
{
    auto until = Clock::now() + interval;

    while (Clock::now() < until)
        usleep(static_cast<useconds_t>(std::chrono::microseconds {interval}.count()));
}

const auto hello = protocol::Hello {"Tests", "com.pond.rudder.tests", 0};
} // namespace

auto tLargePushSurvivesADeafServer =
    test("TransportMac/a push far larger than the socket buffer survives a server not reading") = []
{
    auto folder = TemporarySocketFolder {};
    auto heard = Heard {};
    auto server = startServer(heard, folder.getSocketPath());
    check(server != nullptr);

    auto client = RudderClient {hello, folder.getSocketPath()};
    check(client.isConnected());

    auto pushed = makeManyContexts("a");
    client.pushContexts(pushed);
    waitWithoutPumping(serverDeafInterval);
    check(client.isConnected());

    check(pumpRunLoopUntil([&] { return hasHeardOnlyLatest(heard, pushed); }));
    check(heard.hellos.size() == 1);
    check(client.isConnected());
};

auto tQuickPushesNeverTear =
    test("TransportMac/pushes while one drains end with the last list and no torn line") = []
{
    auto folder = TemporarySocketFolder {};
    auto heard = Heard {};
    auto server = startServer(heard, folder.getSocketPath());
    auto client = RudderClient {hello, folder.getSocketPath()};
    check(pumpRunLoopUntil([&] { return !heard.hellos.empty(); }));

    auto first = makeManyContexts("a");
    auto second = makeManyContexts("b");
    auto third = makeManyContexts("c");
    client.pushContexts(first);
    client.pushContexts(second);
    client.pushContexts(third);

    check(pumpRunLoopUntil([&] { return hasHeardOnlyLatest(heard, third); }));
    check(heard.everyContextsList.size() == 2);
    check(heard.everyContextsList.front() == first);
    check(client.isConnected());
};

auto tReconnectsWithoutBeingTold =
    test("TransportMac/a restarted server is reconnected to without a notification") = []
{
    auto folder = TemporarySocketFolder {};
    auto heard = Heard {};
    auto server = startServer(heard, folder.getSocketPath());
    auto client = RudderClient {hello, folder.getSocketPath()};
    auto pushed = std::vector<protocol::Context> {{"1", "one", "", ""}, {"2", "two", "sub", ""}};
    client.pushContexts(pushed);
    check(pumpRunLoopUntil([&] { return hasHeardOnlyLatest(heard, pushed); }));

    server.reset();
    check(pumpRunLoopUntil([&] { return !client.isConnected(); }));

    auto again = Heard {};
    server = startServer(again, folder.getSocketPath());
    check(server != nullptr);

    check(pumpRunLoopUntil([&] { return !again.hellos.empty() && hasHeardOnlyLatest(again, pushed); }));
    check(client.isConnected());
};

auto tConnectsOnceAServerAppears =
    test("TransportMac/a client made before any server connects once one appears") = []
{
    auto folder = TemporarySocketFolder {};
    auto client = RudderClient {hello, folder.getSocketPath()};
    check(!client.isConnected());

    auto heard = Heard {};
    auto server = startServer(heard, folder.getSocketPath());
    check(pumpRunLoopUntil([&] { return !heard.hellos.empty(); }));
    check(client.isConnected());
};
