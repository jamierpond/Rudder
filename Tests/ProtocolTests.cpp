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
    auto line = encode(Hello {"CowTerm", "com.eacp.cowterm", 7, true});
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
