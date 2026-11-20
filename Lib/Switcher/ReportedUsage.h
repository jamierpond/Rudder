#pragma once

#include "Items.h"
#include "Ranking.h"

#include <FrecencySearch/LearnedPicks.h>
#include <RudderProtocol.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rudder
{
// A use an app reports (a tab shown, a pane typed in) counts like a focus
// Rudder only watched: a quarter of a pick made in Rudder. The app saw the
// user there, but the user didn't ask Rudder for it.
constexpr double appReportedUseWeight = 0.25;

// A time an app reports (last used, last visited, last typed, last file
// change) counts as one use at that time, decaying as Rudder's own picks do.
constexpr double usesPerReportedTime = 1.0;
constexpr std::int64_t reportedTimeHalfLifeMs = frecencysearch::usageHalfLifeMs;

// `count` from `fromMs` decayed to `toMs`, halving every `halfLifeMs`; 0 for
// a half-life that isn't positive. A stamp from the future doesn't grow it.
double calculateDecayedCount(double count,
                             std::int64_t fromMs,
                             std::int64_t toMs,
                             std::int64_t halfLifeMs);

// The strongest single sign of use in what the app reports, decayed to
// `nowMs`: its decaying use count, re-decayed with the half-life it came with,
// or one use at its latest reported time, whichever is more. 0 when it
// reports nothing Rudder weighs (a visit count alone is a lifetime total).
double calculateStrongestReportedUse(const protocol::ContextUsage& usage, std::int64_t nowMs);

// Rudder's frecency for one row: its own (picks in the panel and the focus it
// watched, FrecencyStore) plus appReportedUseWeight times the strongest use
// reported by any of the row's contexts. With nothing reported it is
// Rudder's own, unchanged.
double calculateRowFrecency(double rudderOwnFrecency,
                            const std::vector<protocol::ContextUsage>& reportedUsages,
                            std::int64_t nowMs);

// Each reported usage, by the contextKey of the context that carried it.
using ReportedUsageByKey = std::map<std::string, std::vector<protocol::ContextUsage>>;

// Files the usage each of `appId`'s contexts reports under its contextKey.
void collectReportedUsage(ReportedUsageByKey& byKey,
                          const std::string& appId,
                          const std::vector<RudderContext>& contexts);

// Each row's frecency by its frecencyKey, as calculateRowFrecency folds it:
// `rudderOwnFrecency` and `reported`, both at `nowMs`.
FrecencyFn makeRowFrecencyLookup(FrecencyFn rudderOwnFrecency,
                                 ReportedUsageByKey reported,
                                 std::int64_t nowMs);

// The row reported usage lifts most above Rudder's own frecency, for the log.
struct MostLiftedRow
{
    std::string key;
    double rudderOwnFrecency = 0;
    double frecency = 0;
};

std::optional<MostLiftedRow> findMostLiftedRow(const ReportedUsageByKey& reported,
                                               const FrecencyFn& rudderOwnFrecency,
                                               std::int64_t nowMs);
} // namespace rudder
