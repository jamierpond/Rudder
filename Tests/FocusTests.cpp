#include <Focus.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace rudder;

namespace
{
constexpr auto cowTerm = "com.eacp.cowterm";

std::vector<RudderContext> focusedOn(const std::string& branch)
{
    auto a = RudderContext {"pane:a", "claude", "", ""};
    a.branch = "fix-login";
    auto b = RudderContext {"pane:b", "zsh", "", ""};
    b.branch = "rename";

    (branch == a.branch ? a : b).focused = true;
    return {a, b};
}
} // namespace

auto tFocusChangeStamps = test("Focus/a focus change in the front app is stamped by its key")
    = []
{
    auto focus = FocusTracker {};

    check(focus.pushed(cowTerm, focusedOn("fix-login"), true, 1000)
          == std::optional<std::string> {"com.eacp.cowterm/branch:fix-login"});

    // The same focus pushed again (a title changed): nothing new.
    check(!focus.pushed(cowTerm, focusedOn("fix-login"), true, 2000));

    check(focus.pushed(cowTerm, focusedOn("rename"), true, 3000)
          == std::optional<std::string> {"com.eacp.cowterm/branch:rename"});
};

auto tBackgroundFocusNotStamped
    = test("Focus/a focus change behind another app isn't stamped until it's in front") = []
{
    auto focus = FocusTracker {};

    check(!focus.pushed(cowTerm, focusedOn("rename"), false, 1000));
    check(focus.activated(cowTerm, 5000)
          == std::optional<std::string> {"com.eacp.cowterm/branch:rename"});
    check(!focus.activated("com.apple.Safari", 5000));
};

auto tDwellCreditsOnce = test("Focus/a focus that holds earns frecency once, a flick none")
    = []
{
    auto focus = FocusTracker {};
    auto dwell = FocusTracker::dwellMs;

    focus.pushed(cowTerm, focusedOn("fix-login"), true, 0);
    focus.pushed(cowTerm, focusedOn("rename"), true, 500);

    // Flicked past fix-login; rename hasn't held long enough yet.
    check(!focus.settle(cowTerm, dwell));

    check(focus.settle(cowTerm, 500 + dwell)
          == std::optional<std::string> {"com.eacp.cowterm/branch:rename"});
    check(!focus.settle(cowTerm, 500 + 10 * dwell));

    // Only the front app's focus counts.
    check(!focus.settle("com.apple.Safari", 500 + 10 * dwell));

    // Coming back to it is a new spell.
    focus.activated(cowTerm, 100000);
    check(!focus.settle(cowTerm, 100000 + dwell - 1));
    check(focus.settle(cowTerm, 100000 + dwell).has_value());

    check(FocusTracker::focusWeight < 1.0);
};

auto tForgetDrops = test("Focus/an app that quits is forgotten") = []
{
    auto focus = FocusTracker {};

    focus.pushed(cowTerm, focusedOn("rename"), true, 0);
    focus.forget(cowTerm);

    check(!focus.activated(cowTerm, 1000));
    check(!focus.settle(cowTerm, 1000000));
};

auto tForgetKeyEndsSpell = test("Focus/a gone context's spell ends: no stamp, no credit") = []
{
    auto focus = FocusTracker {};
    auto key = std::string {"com.eacp.cowterm/branch:fix-login"};

    check(focus.pushed(cowTerm, focusedOn("fix-login"), true, 0) == std::optional {key});

    focus.forgetKey(key);

    check(!focus.activated(cowTerm, 1000));
    check(!focus.settle(cowTerm, 1000 + FocusTracker::dwellMs));

    // The next push that focuses something starts a spell again.
    check(focus.pushed(cowTerm, focusedOn("rename"), true, 2000)
          == std::optional<std::string> {"com.eacp.cowterm/branch:rename"});
};
