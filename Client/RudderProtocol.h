#pragma once

#include <Miro/Reflect.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The contexts protocol between Rudder and the apps that publish "contexts"
// (Grazer tabs, CowTerm sessions): newline-delimited JSON over a local socket,
// one object per line with a "type" field.
//
//   app -> Rudder   {"type":"hello","app":"Grazer","bundleId":"audio.pond.grazer","pid":42,
//                    "canClose":true}
//   app -> Rudder   {"type":"contexts","contexts":[{"id":"3","title":"…","url":"…"}]}
//                   (each context may also carry "subtitle", "path", "repo",
//                   "branch", "open": false, "focused": true and "usage")
//   Rudder -> app   {"type":"activate","contextId":"3","raise":true}
//   Rudder -> app   {"type":"close","contextId":"3"}
//
// activate with "raise": false is a preview: the app only switches to the
// context (its tab, its pane) and must not activate itself or raise a window
// -- Rudder brings the app forward itself, and drops it again when the
// selection moves on. "raise" left out means true, so an app built before it
// existed still sees a plain activate; Enter is always a raising one.
//
// close goes only to an app whose hello said "canClose": true, and only for
// one of its open contexts: the app closes it (the tab, the pane) and pushes
// its list without it. There is no separate "closed" reply -- a context
// missing from a push is gone, however it was closed.
//
// "usage" is how the user has used the context in the app, as the app recorded
// it: times in epoch milliseconds, counts, and a decaying count with the time
// it is valid as of and the half-life it decays by. Every field in it, and
// "usage" itself, may be left out, which means the app doesn't report it.
// Rudder alone turns these facts into its order; an app never sends a rank:
//
//   "usage": {"lastUsedAtMs": 1763600000000,
//             "useCount": {"count": 3.2, "asOfMs": 1763600000000,
//                          "halfLifeMs": 259200000},
//             "lastVisitAtMs": 1763600000000, "visitCount": 41,
//             "lastTypedAtMs": 1763600000000, "lastFileChangeAtMs": 1763600000000}
//
// A Rudder built before "usage" looks up only the keys it knows and ignores
// it; an app built before it sends none, and Rudder orders its rows by its
// own picks and watched focus alone, as it did.
//
// hello comes first; each contexts push replaces the app's whole list; the
// connection closing means the app quit. Rudder announces "rudder.started"
// when it starts, so apps reconnect: a distributed notification on macOS, a
// named event it keeps set while it runs on Windows.
namespace rudder::protocol
{
struct Hello
{
    std::string app;
    std::string bundleId;
    std::int64_t pid = 0;

    // The app carries out close.
    bool canClose = false;

    MIRO_REFLECT(app, bundleId, pid, canClose)
};

// A use count the app keeps decayed: `count` as of `asOfMs`, halving every
// `halfLifeMs`.
struct DecayingUseCount
{
    double count = 0;
    std::int64_t asOfMs = 0;
    std::int64_t halfLifeMs = 0;

    MIRO_REFLECT(count, asOfMs, halfLifeMs)

    bool operator==(const DecayingUseCount&) const = default;
};

// How the user has used one context in the app, as the app recorded it. A
// field left out is one the app doesn't report.
struct ContextUsage
{
    // The last time the user went to it in the app: a tab shown, a session
    // switched to.
    Miro::Omittable<std::int64_t> lastUsedAtMs;

    // The app's own decaying count of those uses.
    Miro::Omittable<DecayingUseCount> useCount;

    // A browser's history: the last visit to the page and how many visits
    // it has had, not decayed.
    Miro::Omittable<std::int64_t> lastVisitAtMs;
    Miro::Omittable<std::int64_t> visitCount;

    // A terminal pane's last keystroke from the user.
    Miro::Omittable<std::int64_t> lastTypedAtMs;

    // The last time a file changed in the context's project folder.
    Miro::Omittable<std::int64_t> lastFileChangeAtMs;

    MIRO_REFLECT(lastUsedAtMs, useCount, lastVisitAtMs, visitCount, lastTypedAtMs, lastFileChangeAtMs)

    bool operator==(const ContextUsage&) const = default;
};

struct Context
{
    std::string id;
    std::string title;
    std::string subtitle;
    std::string url;

    // False for a place the app can open on activate (a project folder that
    // becomes a new session) rather than one already open. Rudder only ever
    // activates those on Enter, never to preview them.
    bool open = true;

    // The absolute directory the context is about, if it has one: a terminal
    // pane's cwd, a project folder. Rudder matches on it, below the title.
    std::string path;

    // The one context the app is showing now (its front tab, its focused
    // pane). Rudder switches the app back to it when Esc undoes a preview.
    bool focused = false;

    // The git branch checked out at `path`, if it is in a repo and HEAD isn't
    // detached. A context's most telling name: Rudder matches on it first.
    std::string branch;

    // The root of the git repository `path` is in (a worktree's own root),
    // empty outside a repo. With `branch`, it names the context for Rudder.
    std::string repo;

    // Left out when the app reports no usage for it.
    Miro::Omittable<ContextUsage> usage;

    MIRO_REFLECT(id, title, subtitle, url, open, path, focused, branch, repo, usage)

    bool operator==(const Context&) const = default;
};

struct Contexts
{
    std::vector<Context> contexts;

    MIRO_REFLECT(contexts)
};

struct Activate
{
    std::string contextId;

    // False for a preview: switch to the context, nothing more.
    bool raise = true;

    MIRO_REFLECT(contextId, raise)
};

struct Close
{
    std::string contextId;

    MIRO_REFLECT(contextId)
};

using Message = std::variant<Hello, Contexts, Activate, Close>;

// One line, newline included.
std::string encode(const Message& message);

// Nullopt for anything that isn't a well-formed message of a known type --
// the socket is local, but a bad line must never take either side down.
std::optional<Message> decode(std::string_view line);

// A line longer than this is garbage: dropped, along with the rest of it.
constexpr std::size_t maxLineBytes = 1 << 20;

// Splits a byte stream into lines as it arrives in arbitrary chunks.
class LineBuffer
{
public:
    // The complete lines `bytes` finished, without their newlines.
    std::vector<std::string> append(std::string_view bytes);

private:
    std::string pending;
    bool discarding = false;
};

// Where Rudder listens: the socket file
// ~/Library/Application Support/pond/Rudder/rudder.sock on macOS, the name of an
// eacp IPC channel (a named pipe only this user can open) on Windows.
std::string endpoint();

constexpr auto startedNotification = "rudder.started";
} // namespace rudder::protocol
