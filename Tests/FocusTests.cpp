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
    a.repo = "/p";
    auto b = RudderContext {"pane:b", "zsh", "", ""};
    b.branch = "rename";
    b.repo = "/p";

    (branch == a.branch ? a : b).focused = true;
    return {a, b};
}
} // namespace

auto tFocusChangeStamps = test("Focus/a focus change in the front app is stamped by its key")
    = []
{
    auto focus = FocusTracker {};

    check(focus.pushed(cowTerm, focusedOn("fix-login"), true, 1000)
          == std::optional<std::string> {"com.eacp.cowterm/repo:/p#branch:fix-login"});

    // The same focus pushed again (a title changed): nothing new.
    check(!focus.pushed(cowTerm, focusedOn("fix-login"), true, 2000));

    check(focus.pushed(cowTerm, focusedOn("rename"), true, 3000)
          == std::optional<std::string> {"com.eacp.cowterm/repo:/p#branch:rename"});
};

auto tBackgroundFocusNotStamped
    = test("Focus/a focus change behind another app isn't stamped until it's in front") = []
{
    auto focus = FocusTracker {};

    check(!focus.pushed(cowTerm, focusedOn("rename"), false, 1000));
    check(focus.activated(cowTerm, 5000)
          == std::optional<std::string> {"com.eacp.cowterm/repo:/p#branch:rename"});
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
          == std::optional<std::string> {"com.eacp.cowterm/repo:/p#branch:rename"});
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
    auto key = std::string {"com.eacp.cowterm/repo:/p#branch:fix-login"};

    check(focus.pushed(cowTerm, focusedOn("fix-login"), true, 0) == std::optional {key});

    focus.forgetKey(key);

    check(!focus.activated(cowTerm, 1000));
    check(!focus.settle(cowTerm, 1000 + FocusTracker::dwellMs));

    // The next push that focuses something starts a spell again.
    check(focus.pushed(cowTerm, focusedOn("rename"), true, 2000)
          == std::optional<std::string> {"com.eacp.cowterm/repo:/p#branch:rename"});
};

// The live store had a pick count +1 and then, five seconds later, the same
// decision again as a held focus: Rudder records the pick's activation, the
// OS reports it again, and each started a fresh, uncredited spell.
auto tPickNotCreditedTwice
    = test("Focus/a picked context isn't credited again for the focus the pick causes") = []
{
    auto focus = FocusTracker {};
    auto dwell = FocusTracker::dwellMs;
    auto fix = std::string {"com.eacp.cowterm/repo:/p#branch:fix-login"};

    focus.picked(cowTerm, fix, 1000);

    // Rudder's own recordActivation, then the OS's activation event.
    check(focus.activated(cowTerm, 1000) == std::optional {fix});
    check(focus.activated(cowTerm, 1300) == std::optional {fix});

    // The push catching up with the pane switch: nothing new.
    check(!focus.pushed(cowTerm, focusedOn("fix-login"), true, 1400));

    check(!focus.settle(cowTerm, 1000 + 10 * dwell));

    // Coming back to it later is a new spell, and that one counts.
    check(focus.activated(cowTerm, 100000) == std::optional {fix});
    check(focus.settle(cowTerm, 100000 + dwell) == std::optional {fix});
};

// Pushes are ignored while the panel is up, so without picked() the tracker
// still held the pane the user left: the pick's activation stamped it and
// its dwell credited it.
auto tPickReplacesStaleSpell
    = test("Focus/after a pick the pane left behind is neither stamped nor credited") = []
{
    auto focus = FocusTracker {};
    auto rename = std::string {"com.eacp.cowterm/repo:/p#branch:rename"};

    focus.pushed(cowTerm, focusedOn("fix-login"), true, 0);
    focus.picked(cowTerm, rename, 60000);

    check(focus.activated(cowTerm, 60000) == std::optional {rename});
    check(!focus.settle(cowTerm, 60000 + 10 * FocusTracker::dwellMs));
};
