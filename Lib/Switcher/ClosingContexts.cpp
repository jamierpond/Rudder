#include "ClosingContexts.h"

#include <algorithm>

namespace rudder
{
void ClosingContexts::markClosing(const std::string& appId,
                                  const std::string& contextId,
                                  std::int64_t nowMs)
{
    closing.push_back({appId, contextId, nowMs});
}

bool ClosingContexts::isClosing(const std::string& appId,
                                const std::string& contextId,
                                std::int64_t nowMs) const
{
    return std::any_of(closing.begin(),
                       closing.end(),
                       [&](const ClosingContext& context)
                       {
                           auto isSameContext = context.appId == appId && context.contextId == contextId;
                           return isSameContext && nowMs - context.closedAtMs <= closeTimeoutMs;
                       });
}

void ClosingContexts::removeClosingRows(std::vector<RudderItem>& rows, std::int64_t nowMs) const
{
    std::erase_if(rows,
                  [&](const RudderItem& row)
                  { return !row.contextId.empty() && isClosing(row.appId, row.contextId, nowMs); });
}

std::vector<RudderContext> ClosingContexts::removeClosingContexts(const std::string& appId,
                                                                  std::vector<RudderContext> pushed,
                                                                  std::int64_t nowMs)
{
    std::erase_if(closing,
                  [&](const ClosingContext& context)
                  {
                      auto isTimedOut = nowMs - context.closedAtMs > closeTimeoutMs;
                      auto isStillPushed = std::any_of(pushed.begin(),
                                                       pushed.end(),
                                                       [&](const RudderContext& other)
                                                       { return other.id == context.contextId; });
                      return isTimedOut || (context.appId == appId && !isStillPushed);
                  });

    std::erase_if(pushed,
                  [&](const RudderContext& context) { return isClosing(appId, context.id, nowMs); });
    return pushed;
}
} // namespace rudder
