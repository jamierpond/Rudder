#pragma once

#include "MatchLevel.h"

#include <cstdint>

namespace frecencysearch
{
// What decides where a matched row sorts.
struct RowRankingFacts
{
    MatchLevel level = MatchLevel::nameHasQueryLettersInOrder;

    // Decayed usage: 1 per pick, less for a use only watched.
    double usage = 0;

    // Decayed picks of this row for the typed text, and when last made.
    double learnedPicks = 0;
    std::int64_t learnedAtMs = 0;

    // A running app, an open tab or pane; not a closed app or unopened place.
    bool isOpen = true;
};

bool isLearnedForQuery(const RowRankingFacts& facts);

// Learned rows first, the latest pick first; then by level, usage, and
// open before closed.
bool shouldRankBefore(const RowRankingFacts& first, const RowRankingFacts& second);
} // namespace frecencysearch
