#include <FuzzyMatch.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace grazer;

auto tSubsequenceMatches = test("FuzzyMatch/subsequences match") = []
{
    check(fuzzyScore("ghub", "GitHub").has_value());
    check(fuzzyScore("wiki", "Wikipedia — Main Page").has_value());
};

auto tMissingCharsDontMatch = test("FuzzyMatch/missing characters fail") = []
{ check(!fuzzyScore("gz", "GitHub").has_value()); };

auto tCaseInsensitive = test("FuzzyMatch/matching ignores case") = []
{ check(fuzzyScore("GITHUB", "github.com").has_value()); };

auto tSpacesIgnored = test("FuzzyMatch/spaces in the query are skipped") = []
{ check(fuzzyScore("wiki main", "Wikipedia — Main Page").has_value()); };

auto tExactBeatsScattered =
    test("FuzzyMatch/consecutive hits outrank scattered ones") = []
{
    auto exact = fuzzyScore("git", "github.com");
    auto scattered = fuzzyScore("git", "grand illusion tour");

    check(exact.has_value() && scattered.has_value());
    check(*exact > *scattered);
};

auto tEmptyQueryMatchesAll = test("FuzzyMatch/the empty query matches anything") = []
{ check(fuzzyScore("", "anything").has_value()); };

auto tLaterRunBeatsEarlierStrayLetter =
    test("FuzzyMatch/a later consecutive run beats an earlier stray letter") = []
{
    auto marketing = fuzzyScore(
        "pulls",
        "Tamber Marketing Pulls mail email inbox "
        "https://github.com/tamber-inc/tamber-mktg/pulls");
    auto web = fuzzyScore("pulls",
                          "Tamber Web GitHub PRs coding "
                          "https://github.com/tamber-inc/tamber-web/pulls");

    check(marketing == 34);
    check(web == 34);
    check(fuzzyScore("pulls", "Tamber Web GitHub PRs pulls") == 34);
};

auto tBareUrlFindsTheRun = test("FuzzyMatch/a bare URL scores its path run") = []
{ check(fuzzyScore("pulls", "https://github.com/tamber-inc/tamber-web/pulls") == 34); };

auto tScoringRulesUnchanged = test("FuzzyMatch/scoring rules are unchanged") = []
{
    constexpr auto boundaryThenThreeConsecutive = 5 + 8 + 8;
    constexpr auto midWordStartGapOne = 1 - 1;
    constexpr auto twoBoundaryHitsGapCapped = 5 + (1 + 4 - 3);

    check(fuzzyScore("git", "github") == boundaryThenThreeConsecutive);
    check(fuzzyScore("i", "xi") == midWordStartGapOne);
    check(fuzzyScore("ab", "a----b") == twoBoundaryHitsGapCapped);
    check(fuzzyScore("a b", "ab") == fuzzyScore("ab", "ab"));
    check(!fuzzyScore("abc", "acb").has_value());
};
