#include "BackStack.h"

#include <algorithm>

namespace rudder
{
void BackStack::record(const std::string& appId)
{
    if (walking())
    {
        // Our own walk landing, possibly late: two quick presses can each be
        // answered by an activation after the cursor has already moved on.
        for (std::size_t i = 1; i <= cursor; ++i)
            if (stack[i] == appId)
                return;

        commitWalk();
    }

    std::erase(stack, appId);
    stack.insert(stack.begin(), appId);

    if (stack.size() > maxEntries)
        stack.resize(maxEntries);
}

std::optional<std::string> BackStack::back()
{
    if (cursor + 1 >= stack.size())
        return std::nullopt;

    ++cursor;
    return stack[cursor];
}

void BackStack::forget(const std::string& appId)
{
    auto found = std::find(stack.begin(), stack.end(), appId);

    if (found == stack.end())
        return;

    auto index = (std::size_t) (found - stack.begin());
    stack.erase(found);

    // Anything at or before the walk's landing shifts it one closer; a
    // vanished landing leaves the walk on the app before it.
    if (cursor > 0 && index <= cursor)
        --cursor;
}

void BackStack::commitWalk()
{
    auto landing = stack[cursor];
    stack.erase(stack.begin() + (std::ptrdiff_t) cursor);
    stack.insert(stack.begin(), landing);
    cursor = 0;
}
} // namespace rudder
