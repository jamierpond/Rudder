#include <FrecencySearch/LearnedPicks.h>
#include <FrecencySearch/MatchLevel.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace frecencysearch;

namespace
{
constexpr std::int64_t nowMs = 1'790'000'000'000LL;
constexpr std::int64_t dayMs = 24LL * 60 * 60 * 1000;
} // namespace

auto tNameLevels = test("FrecencySearch/name match levels, best first") = []
{
    check(findNameMatchLevel("graz", "Grazer") == MatchLevel::nameStartsWithQuery);
    check(findNameMatchLevel("term", "CowTerm") == MatchLevel::nameWordStartsWithQuery);
    check(findNameMatchLevel("main", "Wikipedia — Main Page") == MatchLevel::nameWordStartsWithQuery);
    check(findNameMatchLevel("raze", "Grazer") == MatchLevel::nameContainsQueryOrDetailWordStartsWithQuery);
    check(findNameMatchLevel("gzr", "Grazer") == MatchLevel::nameHasQueryLettersInOrder);
    check(!findNameMatchLevel("zg", "Grazer"));
};

auto tDetailsOnlyByWordStart = test("FrecencySearch/details match only where a word starts") = []
{
    auto texts = SearchableTexts {{"claude"}, {"/Users/j/projects/Grazer"}};

    check(findMatchLevel("grazer", texts) == MatchLevel::nameContainsQueryOrDetailWordStartsWithQuery);
    check(!findMatchLevel("ujpd", texts));
    check(!findMatchLevel("razer", texts));
};

auto tSchemeNeverMatches = test("FrecencySearch/a url's scheme never matches") = []
{
    auto texts = SearchableTexts {{"Home"}, {removeUrlScheme("grazer://home")}};

    check(!findMatchLevel("grazer", texts));
    check(findMatchLevel("home", texts) == MatchLevel::nameStartsWithQuery);
};

auto tWordsSplit = test("FrecencySearch/words split at symbols, CamelCase and digits") = []
{
    check(splitIntoWords("GitHub-PRs2") == std::vector<std::string> {"git", "hub", "prs", "2"});
    check(normaliseQuery("  Grazer   GitHub ") == "grazer github");
};

auto tExactThenLongestPrefix = test("FrecencySearch/learned picks: exact text, else the longest learned prefix") = []
{
    auto learned = std::vector<LearnedPick> {addPickTo({"g", "a"}, nowMs), addPickTo({"gr", "b"}, nowMs)};

    check(findLearnedPicksForQuery(learned, "gr", nowMs).front().destinationKey == "b");
    check(findLearnedPicksForQuery(learned, "gra", nowMs).front().destinationKey == "b");
    check(findLearnedPicksForQuery(learned, "g", nowMs).front().destinationKey == "a");
    check(findLearnedPicksForQuery(learned, "x", nowMs).empty());
};

auto tLearnedPicksDecay = test("FrecencySearch/learned picks halve every three days") = []
{
    auto twice = addPickTo(addPickTo({"cow", "x"}, nowMs), nowMs);
    auto found = findLearnedPicksForQuery({twice}, "cow", nowMs + 3 * dayMs);

    check(twice.count == 2.0);
    check(found.front().count == 1.0);
};

auto tOldPicksForgotten = test("FrecencySearch/picks unused for four weeks are forgotten") = []
{
    auto fresh = addPickTo({"cow", "x"}, nowMs);
    auto stale = addPickTo({"cow", "y"}, nowMs - 29 * dayMs);
    auto forgotten = findLearnedPicksToForget({fresh, stale}, nowMs);

    check(forgotten.size() == 1);
    check(forgotten.front().destinationKey == "y");
};
