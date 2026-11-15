#pragma once

#include "Items.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rudder
{
// Contexts the user closed from the panel whose app hasn't yet pushed a
// list without them. Their rows leave the list at once, and a push that
// still carries one (sent before the app handled the close) is read as if it
// didn't. An app that hasn't dropped it after closeTimeoutMs refused the
// close, so it shows again.
class ClosingContexts
{
public:
    static constexpr std::int64_t closeTimeoutMs = 5000;

    void markClosing(const std::string& appId, const std::string& contextId, std::int64_t nowMs);

    bool isClosing(const std::string& appId, const std::string& contextId, std::int64_t nowMs) const;

    void removeClosingRows(std::vector<RudderItem>& rows, std::int64_t nowMs) const;

    // `pushed` without the contexts closing in `appId`; a closing context
    // the push no longer has is done closing.
    std::vector<RudderContext> removeClosingContexts(const std::string& appId,
                                                     std::vector<RudderContext> pushed,
                                                     std::int64_t nowMs);

private:
    struct ClosingContext
    {
        std::string appId;
        std::string contextId;
        std::int64_t closedAtMs = 0;
    };

    std::vector<ClosingContext> closing;
};
} // namespace rudder
