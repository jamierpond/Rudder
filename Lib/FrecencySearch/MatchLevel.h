#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace frecencysearch
{
// How a query finds a row, best first. Rows sort by level, then by usage.
enum class MatchLevel
{
    nameStartsWithQuery = 1,
    nameWordStartsWithQuery,
    nameContainsQueryOrDetailWordStartsWithQuery,
    nameHasQueryLettersInOrder
};

// The texts a row is found by. Names are what the row is called (a title, a
// branch, a pane's directory name); details are what it is about (a
// subtitle, a path, a url) and match only where one of their words starts.
struct SearchableTexts
{
    std::vector<std::string> names;
    std::vector<std::string> details;
};

// Lower case, spaces trimmed at both ends and runs of spaces made one.
std::string normaliseQuery(std::string_view query);

// Lower-case words, split at anything not a letter or digit and where
// CamelCase or a digit starts a new word: "GitHub-PRs2" is git, hub, prs, 2.
std::vector<std::string> splitIntoWords(std::string_view text);

// `url` without its scheme, so grazer:// or https:// never match.
std::string removeUrlScheme(std::string_view url);

bool hasWordStartingWith(std::string_view text, std::string_view normalisedQuery);

// Every letter of the query (spaces skipped) appears in `text` in order.
bool hasLettersInOrder(std::string_view normalisedQuery, std::string_view text);

std::optional<MatchLevel> findNameMatchLevel(std::string_view normalisedQuery,
                                             std::string_view name);

std::optional<MatchLevel> findMatchLevel(std::string_view normalisedQuery,
                                         const SearchableTexts& texts);

bool isBetterMatchLevel(std::optional<MatchLevel> candidate,
                        std::optional<MatchLevel> current);
} // namespace frecencysearch
