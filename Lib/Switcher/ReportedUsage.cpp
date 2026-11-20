#include "ReportedUsage.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rudder
{
double calculateDecayedCount(double count,
                             std::int64_t fromMs,
                             std::int64_t toMs,
                             std::int64_t halfLifeMs)
{
    if (halfLifeMs <= 0)
        return 0;

    auto elapsedMs = std::max<std::int64_t>(0, toMs - fromMs);
    auto halfLives = (double) elapsedMs / (double) halfLifeMs;
    return count * std::exp2(-halfLives);
}

double calculateStrongestReportedUse(const protocol::ContextUsage& usage, std::int64_t nowMs)
{
    auto strongest = 0.0;

    if (usage.useCount)
        strongest = calculateDecayedCount(
            usage.useCount->count, usage.useCount->asOfMs, nowMs, usage.useCount->halfLifeMs);

    for (auto* reportedTime: {&usage.lastUsedAtMs,
                              &usage.lastVisitAtMs,
                              &usage.lastTypedAtMs,
                              &usage.lastFileChangeAtMs})
        if (*reportedTime)
            strongest = std::max(strongest,
                                 calculateDecayedCount(usesPerReportedTime,
                                                       **reportedTime,
                                                       nowMs,
                                                       reportedTimeHalfLifeMs));

    return strongest;
}

double calculateRowFrecency(double rudderOwnFrecency,
                            const std::vector<protocol::ContextUsage>& reportedUsages,
                            std::int64_t nowMs)
{
    auto strongest = 0.0;

    for (auto& usage: reportedUsages)
        strongest = std::max(strongest, calculateStrongestReportedUse(usage, nowMs));

    return rudderOwnFrecency + appReportedUseWeight * strongest;
}

void collectReportedUsage(ReportedUsageByKey& byKey,
                          const std::string& appId,
                          const std::vector<RudderContext>& contexts)
{
    for (auto& context: contexts)
        if (context.reportedUsage)
            byKey[contextKey(appId, context)].push_back(*context.reportedUsage);
}

std::optional<MostLiftedRow> findMostLiftedRow(const ReportedUsageByKey& reported,
                                               const FrecencyFn& rudderOwnFrecency,
                                               std::int64_t nowMs)
{
    auto mostLifted = std::optional<MostLiftedRow> {};

    for (auto& [key, usages]: reported)
    {
        auto row = MostLiftedRow {key, rudderOwnFrecency(key), 0};
        row.frecency = calculateRowFrecency(row.rudderOwnFrecency, usages, nowMs);

        auto lift = row.frecency - row.rudderOwnFrecency;

        if (!mostLifted || lift > mostLifted->frecency - mostLifted->rudderOwnFrecency)
            mostLifted = row;
    }

    return mostLifted;
}

FrecencyFn makeRowFrecencyLookup(FrecencyFn rudderOwnFrecency,
                                 ReportedUsageByKey reported,
                                 std::int64_t nowMs)
{
    return [rudderOwnFrecency = std::move(rudderOwnFrecency),
            reported = std::move(reported),
            nowMs](const std::string& key)
    {
        auto own = rudderOwnFrecency(key);
        auto found = reported.find(key);

        if (found == reported.end())
            return own;

        return calculateRowFrecency(own, found->second, nowMs);
    };
}
} // namespace rudder
