#include "ContextServer.h"

#import <Foundation/Foundation.h>

#include <cerrno>
#include <cstring>
#include <map>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace rudder::platform
{
struct ContextServer::Native
{
    struct Connection
    {
        dispatch_source_t reader = nil;
        protocol::LineBuffer lines;
    };

    ContextServer* owner = nullptr;
    int listener = -1;
    dispatch_source_t acceptor = nil;
    std::map<int, Connection> connections;

    void accept()
    {
        auto fd = ::accept(listener, nullptr, nullptr);

        if (fd < 0)
            return;

        auto on = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));

        // An app that stops reading must not stall the switcher.
        auto timeout = timeval {0, 200000};
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        auto reader = dispatch_source_create(
            DISPATCH_SOURCE_TYPE_READ, (uintptr_t) fd, 0, dispatch_get_main_queue());

        dispatch_source_set_event_handler(reader, ^{
          receive(fd);
        });
        dispatch_source_set_cancel_handler(reader, ^{
          ::close(fd);
        });

        connections[fd].reader = reader;
        dispatch_resume(reader);
    }

    void receive(int fd)
    {
        auto found = connections.find(fd);

        if (found == connections.end())
            return;

        char buffer[16384];
        auto count = ::read(fd, buffer, sizeof(buffer));

        if (count <= 0)
        {
            if (count < 0 && (errno == EAGAIN || errno == EINTR))
                return;

            close(fd);
            return;
        }

        for (auto& line: found->second.lines.append({buffer, (std::size_t) count}))
        {
            auto message = protocol::decode(line);

            if (!message)
                continue;

            if (auto* hello = std::get_if<protocol::Hello>(&*message))
                owner->onHello(fd, *hello);
            else if (auto* contexts = std::get_if<protocol::Contexts>(&*message))
                owner->onContexts(fd, std::move(contexts->contexts));

            // Activate only ever goes the other way; an app sending one is
            // ignored like any other line Rudder has no use for.

            if (!connections.contains(fd))
                return;
        }
    }

    void close(int fd)
    {
        auto found = connections.find(fd);

        if (found == connections.end())
            return;

        dispatch_source_cancel(found->second.reader);
        connections.erase(found);
        owner->onClosed(fd);
    }
};

ContextServer::ContextServer()
    : native(std::make_unique<Native>())
{
    native->owner = this;
}

ContextServer::~ContextServer()
{
    for (auto& [fd, connection]: native->connections)
        dispatch_source_cancel(connection.reader);

    native->connections.clear();

    if (native->acceptor != nil)
        dispatch_source_cancel(native->acceptor);
}

bool ContextServer::start()
{
    auto path = protocol::socketPath();
    auto address = sockaddr_un {};
    address.sun_family = AF_UNIX;

    if (path.size() >= sizeof(address.sun_path))
        return false;

    auto folder = path.substr(0, path.rfind('/'));
    [NSFileManager.defaultManager
              createDirectoryAtPath:[NSString stringWithUTF8String:folder.c_str()]
        withIntermediateDirectories:YES
                         attributes:nil
                              error:nil];

    // A socket file left by a Rudder that didn't exit cleanly would make bind
    // fail; nothing can be listening on it, as Rudder is the only listener.
    ::unlink(path.c_str());

    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

    auto fd = ::socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0)
        return false;

    if (::bind(fd, (sockaddr*) &address, sizeof(address)) != 0 || ::listen(fd, 16) != 0)
    {
        NSLog(@"Rudder: contexts socket unavailable: %s", std::strerror(errno));
        ::close(fd);
        return false;
    }

    // This user's apps only.
    ::chmod(path.c_str(), 0600);

    native->listener = fd;
    native->acceptor = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_READ, (uintptr_t) fd, 0, dispatch_get_main_queue());

    auto* server = native.get();
    dispatch_source_set_event_handler(native->acceptor, ^{
      server->accept();
    });
    dispatch_source_set_cancel_handler(native->acceptor, ^{
      ::close(fd);
    });
    dispatch_resume(native->acceptor);
    return true;
}

void ContextServer::sendActivate(int connection, const std::string& contextId, bool raise)
{
    if (!native->connections.contains(connection))
        return;

    auto line = protocol::encode(protocol::Activate {contextId, raise});
    auto* data = line.data();
    auto left = line.size();

    while (left > 0)
    {
        auto written = ::write(connection, data, left);

        if (written < 0 && errno == EINTR)
            continue;

        if (written <= 0)
        {
            native->close(connection);
            return;
        }

        data += written;
        left -= (std::size_t) written;
    }
}
} // namespace rudder::platform
