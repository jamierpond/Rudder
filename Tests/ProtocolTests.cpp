#include <RudderProtocol.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace rudder::protocol;

auto tHelloRoundTrips = test("Protocol/hello encodes to one line and back") = []
{
    auto line = encode(Hello {"Grazer", "audio.pond.grazer", 42});

    check(line.back() == '\n');
    check(line.find('\n') == line.size() - 1);

    auto message = decode(line.substr(0, line.size() - 1));
    auto* hello = message ? std::get_if<Hello>(&*message) : nullptr;

    check(hello != nullptr);

    if (hello == nullptr)
        return;

    check(hello->app == "Grazer");
    check(hello->bundleId == "audio.pond.grazer");
    check(hello->pid == 42);
};

auto tCanClose = test("Protocol/hello says whether the app closes contexts") = []
{
    auto line = encode(Hello {"CowTerm", "audio.pond.cowterm", 7, true});
    auto message = decode(line);
    auto* hello = message ? std::get_if<Hello>(&*message) : nullptr;

    check(hello != nullptr && hello->canClose);

    // An app built before close existed says nothing, and can't.
    message = decode(R"({"type":"hello","bundleId":"audio.pond.grazer"})");
    hello = message ? std::get_if<Hello>(&*message) : nullptr;

    check(hello != nullptr && !hello->canClose);
    check(!decode(R"({"type":"hello","bundleId":"a","canClose":"yes"})").has_value());
};

auto tCloseRoundTrips = test("Protocol/close carries the context id") = []
{
    auto line = encode(Close {"pane:9"});
    check(line.find(R"("type":"close")") != std::string::npos);

    auto message = decode(line);
    auto* close = message ? std::get_if<Close>(&*message) : nullptr;

    check(close != nullptr && close->contextId == "pane:9");
    check(!decode(R"({"type":"close"})").has_value());
    check(!decode(R"({"type":"close","contextId":""})").has_value());
};

auto tContextsRoundTrip = test("Protocol/contexts keep every field") = []
{
    auto sent = Contexts {{{"3", "GitHub", "https://github.com", "https://github.com"},
                           {"4", "Hacker \"News\"\n", "", ""},
                           {"place:/Users/j/projects/tamber-web", "tamber-web", "", "", false},
                           {"pane:9", "claude", "Tab 3", "", true, "/Users/j/projects/Grazer", true, "main"}}};
    auto line = encode(sent);
    auto message = decode(line.substr(0, line.size() - 1));
    auto* received = message ? std::get_if<Contexts>(&*message) : nullptr;

    check(received != nullptr);

    if (received != nullptr)
        check(received->contexts == sent.contexts);
};

auto tActivateRoundTrips = test("Protocol/activate carries the context id") = []
{
    auto line = encode(Activate {"7"});
    auto message = decode(line);
    auto* activate = message ? std::get_if<Activate>(&*message) : nullptr;

    check(activate != nullptr && activate->contextId == "7");
    check(activate != nullptr && activate->raise);
};

auto tPreviewRoundTrips = test("Protocol/a preview is an activate that doesn't raise") = []
{
    auto line = encode(Activate {"7", false});
    check(line.find(R"("raise":false)") != std::string::npos);

    auto message = decode(line);
    auto* activate = message ? std::get_if<Activate>(&*message) : nullptr;

    check(activate != nullptr && activate->contextId == "7" && !activate->raise);
};

auto tRaiseDefaults = test("Protocol/activate without raise raises") = []
{
    auto message = decode(R"({"type":"activate","contextId":"7"})");
    auto* activate = message ? std::get_if<Activate>(&*message) : nullptr;

    check(activate != nullptr && activate->raise);
    check(!decode(R"({"type":"activate","contextId":"7","raise":"no"})").has_value());
};

auto tOptionalFields = test("Protocol/subtitle and url may be left out") = []
{
    auto message = decode(R"({"type":"contexts","contexts":[{"id":"1","title":"Docs"}]})");
    auto* contexts = message ? std::get_if<Contexts>(&*message) : nullptr;

    check(contexts != nullptr && contexts->contexts.size() == 1);

    if (contexts != nullptr && !contexts->contexts.empty())
    {
        check(contexts->contexts.front().url.empty());
        check(contexts->contexts.front().path.empty());
        check(contexts->contexts.front().branch.empty());
        check(contexts->contexts.front().open);
        check(!contexts->contexts.front().focused);
    }
};

auto tGarbageIgnored = test("Protocol/garbage decodes to nothing") = []
{
    check(!decode("").has_value());
    check(!decode("not json").has_value());
    check(!decode("[1,2,3]").has_value());
    check(!decode(R"({"type":"explode"})").has_value());
    check(!decode(R"({"type":42})").has_value());
    check(!decode(R"({"app":"Grazer"})").has_value());
    check(!decode(R"({"type":"contexts","contexts":"nope"})").has_value());
    check(!decode(R"({"type":"hello","pid":"not a number"})").has_value());
    check(!decode(R"({"type":"contexts","contexts":[{"id":"1","open":"no"}]})").has_value());
    check(!decode(R"({"type":"contexts","contexts":[{"id":"1","path":7}]})").has_value());
    check(!decode(R"({"type":"contexts","contexts":[{"id":"1","focused":1}]})").has_value());
};

auto tLinesSplit = test("Protocol/lines split across chunks reassemble") = []
{
    auto buffer = LineBuffer {};

    check(buffer.append("{\"a\"").empty());

    auto lines = buffer.append(":1}\n{\"b\":2}\n{\"c\"");

    check(lines.size() == 2);
    check(lines[0] == "{\"a\":1}");
    check(lines[1] == "{\"b\":2}");
    check(buffer.append(":3}\n").front() == "{\"c\":3}");
};

auto tLongLinesDropped = test("Protocol/an overlong line is dropped whole") = []
{
    auto buffer = LineBuffer {};
    auto huge = std::string(maxLineBytes + 10, 'x');

    check(buffer.append(huge).empty());

    auto lines = buffer.append("tail of the garbage\nnext\n");

    check(lines.size() == 1);
    check(lines.front() == "next");
};

auto tClosedPlaces = test("Protocol/open: false marks a place that isn't open yet") = []
{
    auto message = decode(R"({"type":"contexts","contexts":[{"id":"place:/p","title":"p","open":false}]})");
    auto* contexts = message ? std::get_if<Contexts>(&*message) : nullptr;

    check(contexts != nullptr && contexts->contexts.size() == 1
          && !contexts->contexts.front().open);
};

auto tPathAndFocused = test("Protocol/path and focused decode, and older apps' lines still do") = []
{
    auto message = decode(
        R"({"type":"contexts","contexts":[{"id":"pane:1","title":"t","path":"/Users/j/projects/Grazer","focused":true}]})");
    auto* contexts = message ? std::get_if<Contexts>(&*message) : nullptr;

    check(contexts != nullptr && contexts->contexts.size() == 1);

    if (contexts != nullptr && !contexts->contexts.empty())
    {
        check(contexts->contexts.front().path == "/Users/j/projects/Grazer");
        check(contexts->contexts.front().focused);
    }

    // What a Rudder built before these fields reads: it looks up only the
    // keys it knows, so the extra ones are simply ignored.
    auto line = encode(Contexts {{{"1", "Docs"}}});
    check(line.find(R"("path":"")") != std::string::npos);
    check(line.find(R"("focused":false)") != std::string::npos);
};

namespace
{
constexpr std::int64_t reportedAtMs = 1'790'000'000'000LL;
constexpr std::int64_t threeDaysMs = 3LL * 24 * 60 * 60 * 1000;
constexpr double decayedUses = 3.25;
constexpr std::int64_t lifetimeVisits = 41;

ContextUsage makeEveryReportedFact()
{
    auto usage = ContextUsage {};
    usage.lastUsedAtMs = reportedAtMs;
    usage.useCount = DecayingUseCount {decayedUses, reportedAtMs, threeDaysMs};
    usage.lastVisitAtMs = reportedAtMs - 1;
    usage.visitCount = lifetimeVisits;
    usage.lastTypedAtMs = reportedAtMs - 2;
    usage.lastFileChangeAtMs = reportedAtMs - 3;
    return usage;
}

const Context* decodeOneContext(const std::optional<Message>& message)
{
    auto* contexts = message ? std::get_if<Contexts>(&*message) : nullptr;

    if (contexts == nullptr || contexts->contexts.size() != 1)
        return nullptr;

    return &contexts->contexts.front();
}
} // namespace

auto tUsageRoundTrips = test("Protocol/usage keeps every reported fact") = []
{
    auto context = Context {"3", "GitHub"};
    context.usage = makeEveryReportedFact();
    auto line = encode(Contexts {{context}});
    auto message = decode(line);
    auto* received = decodeOneContext(message);

    check(received != nullptr && *received == context);
};

auto tUsagePartly = test("Protocol/an app reports only the usage facts it fills in") = []
{
    auto context = Context {"pane:1", "zsh"};
    auto usage = ContextUsage {};
    usage.lastTypedAtMs = reportedAtMs;
    context.usage = usage;
    auto line = encode(Contexts {{context}});

    check(line.find("lastTypedAtMs") != std::string::npos);
    check(line.find("useCount") == std::string::npos);
    check(line.find("visitCount") == std::string::npos);

    auto message = decode(line);
    auto* received = decodeOneContext(message);

    check(received != nullptr && received->usage && received->usage->lastTypedAtMs
          && !received->usage->useCount && !received->usage->lastUsedAtMs);
};

auto tNoUsageFromOlderApps = test("Protocol/a context from an app that reports no usage has none") = []
{
    auto line = encode(Contexts {{{"1", "Docs"}}});

    check(line.find("usage") == std::string::npos);

    auto message = decode(R"({"type":"contexts","contexts":[{"id":"1","title":"Docs","path":"/p"}]})");
    auto* received = decodeOneContext(message);

    check(received != nullptr && !received->usage);

    message = decode(R"({"type":"contexts","contexts":[{"id":"1","usage":null}]})");
    received = decodeOneContext(message);

    check(received != nullptr && !received->usage);

    message = decode(R"({"type":"contexts","contexts":[{"id":"1","usage":{"lastUsedAtMs":null}}]})");
    received = decodeOneContext(message);

    check(received != nullptr && received->usage && !received->usage->lastUsedAtMs);
};

auto tOlderRudderIgnoresUsage = test("Protocol/keys a Rudder doesn't know are ignored, so an older Rudder reads a newer app") = []
{
    auto context = Context {"3", "GitHub"};
    context.usage = makeEveryReportedFact();
    auto line = encode(Contexts {{context}});
    auto withUnknownKey = line;
    withUnknownKey.replace(line.find(R"("usage")"), std::string {R"("usage")"}.size(), R"("usageFromTheFuture")");
    auto message = decode(withUnknownKey);
    auto* received = decodeOneContext(message);

    check(received != nullptr && received->id == "3" && received->title == "GitHub"
          && !received->usage);
};

auto tBadUsage = test("Protocol/usage of the wrong shape is garbage") = []
{
    check(!decode(R"({"type":"contexts","contexts":[{"id":"1","usage":7}]})").has_value());
    check(!decode(R"({"type":"contexts","contexts":[{"id":"1","usage":{"lastUsedAtMs":"now"}}]})")
               .has_value());
    check(!decode(R"({"type":"contexts","contexts":[{"id":"1","usage":{"useCount":{"count":1}}}]})")
               .has_value());
    check(!decode(R"({"type":"contexts","contexts":[{"id":"1","usage":{"lastTypedAtMs":1.5}}]})")
               .has_value());
};
