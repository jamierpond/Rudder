#include <BackStack.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace rudder;

namespace
{
using Ids = std::vector<std::string>;

BackStack visited(const Ids& ids)
{
    auto stack = BackStack {};

    for (auto& id: ids)
        stack.record(id);

    return stack;
}
} // namespace

auto tMostRecentFirst = test("BackStack/entries are most recent first") = []
{ check(visited({"a", "b", "c"}).entries() == Ids {"c", "b", "a"}); };

auto tDuplicatesCollapse = test("BackStack/revisits collapse to one entry") = []
{ check(visited({"a", "b", "a", "b", "a", "c"}).entries() == Ids {"c", "a", "b"}); };

auto tBackWalks = test("BackStack/back keeps walking further back") = []
{
    auto stack = visited({"a", "b", "c", "d"});

    check(stack.back() == "c");
    stack.record("c");
    check(stack.back() == "b");
    stack.record("b");
    check(stack.back() == "a");
    stack.record("a");
    check(!stack.back().has_value());
};

auto tWalkDoesNotReorder = test("BackStack/the walk's own activations keep the order")
    = []
{
    auto stack = visited({"a", "b", "c"});

    stack.back();
    stack.record("b");

    check(stack.walking());
    check(stack.entries() == Ids {"c", "b", "a"});
};

auto tLateActivations = test("BackStack/late walk activations are still expected") = []
{
    auto stack = visited({"a", "b", "c", "d"});

    // Two quick presses before either activation is reported.
    check(stack.back() == "c");
    check(stack.back() == "b");
    stack.record("c");
    stack.record("b");

    check(stack.walking());
    check(stack.back() == "a");
};

auto tOtherActivationCommits = test("BackStack/a new activation ends the walk") = []
{
    auto stack = visited({"a", "b", "c", "d"});

    stack.back();
    stack.record("c");
    stack.back();
    stack.record("b");

    // Landed on b, then the user switched to e: b is where they were.
    stack.record("e");

    check(!stack.walking());
    check(stack.entries() == Ids {"e", "b", "d", "c", "a"});
    check(stack.back() == "b");
};

auto tReturnToStartCommits = test("BackStack/going back to the start ends the walk") = []
{
    auto stack = visited({"a", "b", "c"});

    stack.back();
    stack.record("b");
    stack.record("c");

    check(!stack.walking());
    check(stack.entries() == Ids {"c", "b", "a"});
};

auto tForgetQuitApp = test("BackStack/forgetting an app drops it") = []
{
    auto stack = visited({"a", "b", "c"});
    stack.forget("b");

    check(stack.entries() == Ids {"c", "a"});
    check(stack.back() == "a");
};

auto tForgetLandingMidWalk = test("BackStack/the walk survives its landing quitting")
    = []
{
    auto stack = visited({"a", "b", "c", "d"});

    stack.back();
    stack.record("c");
    stack.forget("c");

    check(stack.entries() == Ids {"d", "b", "a"});
    check(stack.back() == "b");
};

auto tEmptyStack = test("BackStack/an empty or single history has nowhere to go") = []
{
    auto stack = BackStack {};
    check(!stack.back().has_value());

    stack.record("a");
    check(!stack.back().has_value());
};

auto tBounded = test("BackStack/history is bounded") = []
{
    auto stack = BackStack {};

    for (std::size_t i = 0; i < BackStack::maxEntries + 20; ++i)
        stack.record("app-" + std::to_string(i));

    check(stack.entries().size() == BackStack::maxEntries);
    check(stack.entries().front() == "app-" + std::to_string(BackStack::maxEntries + 19));
};
