#include "RudderClient.h"

#import <Foundation/Foundation.h>

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace rudder
{
// The connection state lives in a shared_ptr the dispatch blocks hold weakly,
// so a block arriving after the client is gone finds nothing to touch.
struct RudderClient::Native
{
    struct State
    {
        RudderClient* owner = nullptr;
        protocol::Hello hello;
        std::optional<protocol::Contexts> latest;
        protocol::LineBuffer lines;
        int socket = -1;
        dispatch_source_t reader = nil;
        id startedObserver = nil;
    };

    std::shared_ptr<State> state = std::make_shared<State>();

    static void connect(const std::shared_ptr<State>& state)
    {
        if (state->socket >= 0)
            return;

        auto path = protocol::socketPath();
        auto address = sockaddr_un {};
        address.sun_family = AF_UNIX;

        if (path.size() >= sizeof(address.sun_path))
            return;

        std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

        auto fd = ::socket(AF_UNIX, SOCK_STREAM, 0);

        if (fd < 0)
            return;

        auto on = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));

        // A wedged Rudder must not wedge the app: a push gives up after a
        // moment and the connection is dropped.
        auto timeout = timeval {0, 200000};
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        // No Rudder listening is the normal case: stay quiet until it says
        // it has started.
        if (::connect(fd, (sockaddr*) &address, sizeof(address)) != 0)
        {
            ::close(fd);
            return;
        }

        state->socket = fd;
        state->lines = {};

        auto weak = std::weak_ptr<State> {state};
        state->reader =
            dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, (uintptr_t) fd, 0, dispatch_get_main_queue());

        dispatch_source_set_event_handler(state->reader, ^{
          if (auto alive = weak.lock())
              receive(alive);
        });

        dispatch_source_set_cancel_handler(state->reader, ^{
          ::close(fd);
        });

        dispatch_resume(state->reader);

        send(state, state->hello);

        if (state->latest)
            send(state, *state->latest);
    }

    static void disconnect(const std::shared_ptr<State>& state)
    {
        if (state->reader != nil)
        {
            dispatch_source_cancel(state->reader);
            state->reader = nil;
        }

        state->socket = -1;
    }

    static void receive(const std::shared_ptr<State>& state)
    {
        char buffer[16384];
        auto count = ::read(state->socket, buffer, sizeof(buffer));

        if (count <= 0)
        {
            if (count < 0 && (errno == EAGAIN || errno == EINTR))
                return;

            disconnect(state);
            return;
        }

        for (auto& line: state->lines.append({buffer, (std::size_t) count}))
        {
            auto message = protocol::decode(line);

            if (!message)
                continue;

            if (auto* activate = std::get_if<protocol::Activate>(&*message))
                if (state->owner != nullptr)
                    state->owner->onActivate(activate->contextId);
        }
    }

    static void send(const std::shared_ptr<State>& state, const protocol::Message& message)
    {
        if (state->socket < 0)
            return;

        auto line = protocol::encode(message);
        auto* data = line.data();
        auto left = line.size();

        while (left > 0)
        {
            auto written = ::write(state->socket, data, left);

            if (written < 0 && errno == EINTR)
                continue;

            if (written <= 0)
            {
                disconnect(state);
                return;
            }

            data += written;
            left -= (std::size_t) written;
        }
    }
};

RudderClient::RudderClient(protocol::Hello hello)
    : native(std::make_unique<Native>())
{
    auto& state = native->state;
    state->owner = this;
    state->hello = std::move(hello);

    if (state->hello.pid == 0)
        state->hello.pid = getpid();

    auto weak = std::weak_ptr<Native::State> {state};
    auto* name = [NSString stringWithUTF8String:protocol::startedNotification];

    state->startedObserver = [NSDistributedNotificationCenter.defaultCenter
        addObserverForName:name
                    object:nil
                     queue:NSOperationQueue.mainQueue
                usingBlock:^(NSNotification*) {
                  if (auto alive = weak.lock())
                  {
                      // A restarted Rudder: the old connection is dead even
                      // if its close hasn't been read yet.
                      Native::disconnect(alive);
                      Native::connect(alive);
                  }
                }];

    Native::connect(state);
}

RudderClient::~RudderClient()
{
    auto& state = native->state;
    state->owner = nullptr;

    if (state->startedObserver != nil)
        [NSDistributedNotificationCenter.defaultCenter removeObserver:state->startedObserver];

    Native::disconnect(state);
}

void RudderClient::pushContexts(std::vector<protocol::Context> contexts)
{
    auto& state = native->state;
    state->latest = protocol::Contexts {std::move(contexts)};
    Native::send(state, *state->latest);
}

bool RudderClient::isConnected() const
{
    return native->state->socket >= 0;
}
} // namespace rudder
