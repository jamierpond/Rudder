#include "RowOrder.h"

#include "LearnedPicks.h"

namespace frecencysearch
{
bool isLearnedForQuery(const RowRankingFacts& facts)
{
    return facts.learnedPicks >= learnedPicksToLeadTheList;
}

bool shouldRankBefore(const RowRankingFacts& first, const RowRankingFacts& second)
{
    auto firstIsLearned = isLearnedForQuery(first);
    auto secondIsLearned = isLearnedForQuery(second);

    if (firstIsLearned != secondIsLearned)
        return firstIsLearned;

    if (firstIsLearned && first.learnedAtMs != second.learnedAtMs)
        return first.learnedAtMs > second.learnedAtMs;

    if (first.level != second.level)
        return first.level < second.level;

    if (first.usage != second.usage)
        return first.usage > second.usage;

    return first.isOpen && !second.isOpen;
}
} // namespace frecencysearch
