#pragma once

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace grazer
{
// fzf-style case-insensitive subsequence match. Consecutive matches and
// word-boundary hits score higher; skipped text costs a little. Spaces in the
// query are ignored so "wiki main" can match "Wikipedia — Main Page".
// Scores the best alignment over every way the query fits the text, not the
// first greedy one, so a later consecutive run beats an earlier stray letter.
inline std::optional<int> fuzzyScore(const std::string& query,
                                     const std::string& text)
{
    constexpr auto consecutiveMatchScore = 8;
    constexpr auto scatteredMatchScore = 1;
    constexpr auto wordBoundaryBonus = 4;
    constexpr auto maximumGapCost = 3;
    constexpr auto unreachable = std::numeric_limits<int>::min() / 2;

    auto lower = [](char c) { return (char) std::tolower((unsigned char) c); };

    auto isBoundary = [&](size_t index)
    {
        if (index == 0)
            return true;

        auto prev = (unsigned char) text[index - 1];
        return !std::isalnum(prev);
    };

    auto calculateGapCost = [&](size_t skipped)
    { return (int) std::min<size_t>(skipped, (size_t) maximumGapCost); };

    // Row[j]: best score with the latest query letter matched at text[j].
    thread_local std::vector<int> previousRow;
    thread_local std::vector<int> currentRow;
    previousRow.assign(text.size(), unreachable);
    currentRow.assign(text.size(), unreachable);

    auto isFirstQueryLetter = true;

    for (auto queryChar: query)
    {
        if (queryChar == ' ')
            continue;

        auto target = lower(queryChar);
        auto bestBeyondGapCap = unreachable;
        auto anyReachable = false;

        for (size_t j = 0; j < text.size(); ++j)
        {
            if (!isFirstQueryLetter && j >= (size_t) maximumGapCost + 1)
                bestBeyondGapCap = std::max(
                    bestBeyondGapCap, previousRow[j - maximumGapCost - 1]);

            currentRow[j] = unreachable;

            if (lower(text[j]) != target)
                continue;

            auto best = unreachable;

            if (isFirstQueryLetter)
            {
                best = scatteredMatchScore - calculateGapCost(j);
            }
            else
            {
                if (j >= 1 && previousRow[j - 1] != unreachable)
                    best = previousRow[j - 1] + consecutiveMatchScore;

                for (size_t skipped = 1;
                     skipped < (size_t) maximumGapCost && skipped + 1 <= j;
                     ++skipped)
                {
                    auto from = previousRow[j - skipped - 1];

                    if (from != unreachable)
                        best = std::max(best,
                                        from + scatteredMatchScore
                                            - calculateGapCost(skipped));
                }

                if (bestBeyondGapCap != unreachable)
                    best = std::max(best,
                                    bestBeyondGapCap + scatteredMatchScore
                                        - maximumGapCost);
            }

            if (best == unreachable)
                continue;

            if (isBoundary(j))
                best += wordBoundaryBonus;

            currentRow[j] = best;
            anyReachable = true;
        }

        if (!anyReachable)
            return std::nullopt;

        std::swap(previousRow, currentRow);
        isFirstQueryLetter = false;
    }

    if (isFirstQueryLetter)
        return 0;

    return *std::max_element(previousRow.begin(), previousRow.end());
}
} // namespace grazer
