#include "SearchKeys.h"

#include <array>
#include <string_view>

namespace searchkeys
{
namespace
{
Result stay(State state, Action action)
{
    state.pending = PendingOperator::none;
    return {state, action};
}

Result enterInsert(State state, int caret)
{
    state.mode = Mode::insert;
    state.pending = PendingOperator::none;
    state.caret = caret;
    return {state, Action::none};
}

Result clearAndEnterInsert(State state)
{
    state.text.clear();
    return enterInsert(state, 0);
}

Result waitFor(State state, PendingOperator pending)
{
    state.pending = pending;
    return {state, Action::none};
}

// The second key of dd, gg or cc; anything else drops the operator.
Result finishOperator(const State& state, const std::string& typed)
{
    auto pending = state.pending;

    if (pending == PendingOperator::deleteRow && typed == "d")
        return stay(state, Action::closeHighlighted);

    if (pending == PendingOperator::goToFirst && typed == "g")
        return stay(state, Action::moveHighlightToFirst);

    if (pending == PendingOperator::changeText && typed == "c")
        return clearAndEnterInsert(state);

    return stay(state, Action::none);
}

Result handleSpecialInNormal(const State& state, const Key& key)
{
    switch (key.special)
    {
        case SpecialKey::escape:
            if (state.pending != PendingOperator::none)
                return stay(state, Action::none);

            return stay(state, Action::closeSearch);
        case SpecialKey::enter:
            if (key.command)
                return stay(state, Action::openHighlightedInNewTab);

            return stay(state, Action::openHighlighted);
        case SpecialKey::downArrow: return stay(state, Action::moveHighlightDown);
        case SpecialKey::upArrow: return stay(state, Action::moveHighlightUp);
        case SpecialKey::none: break;
    }

    return stay(state, Action::none);
}

using NormalCommand = Result (*)(const State&);

struct NormalBinding
{
    std::string_view typed;
    NormalCommand command;
};

constexpr auto normalBindings = std::array {
    NormalBinding {"j", [](const State& s) { return stay(s, Action::moveHighlightDown); }},
    NormalBinding {"k", [](const State& s) { return stay(s, Action::moveHighlightUp); }},
    NormalBinding {"G", [](const State& s) { return stay(s, Action::moveHighlightToLast); }},
    NormalBinding {"x", [](const State& s) { return stay(s, Action::closeHighlighted); }},
    NormalBinding {"s", [](const State& s) { return stay(s, Action::starHighlighted); }},
    NormalBinding {"i", [](const State& s) { return enterInsert(s, s.caret); }},
    NormalBinding {"a", [](const State& s) { return enterInsert(s, s.caret); }},
    NormalBinding {"/", [](const State& s) { return enterInsert(s, s.caret); }},
    NormalBinding {"A", [](const State& s) { return enterInsert(s, (int) s.text.size()); }},
    NormalBinding {"I", [](const State& s) { return enterInsert(s, 0); }},
    NormalBinding {"S", [](const State& s) { return clearAndEnterInsert(s); }},
    NormalBinding {"d", [](const State& s) { return waitFor(s, PendingOperator::deleteRow); }},
    NormalBinding {"g", [](const State& s) { return waitFor(s, PendingOperator::goToFirst); }},
    NormalBinding {"c", [](const State& s) { return waitFor(s, PendingOperator::changeText); }},
};

Result handleTypedInNormal(const State& state, const std::string& typed)
{
    for (auto& binding: normalBindings)
        if (binding.typed == typed)
            return binding.command(state);

    return stay(state, Action::none);
}

bool hasModifier(const Key& key)
{
    return key.command || key.control || key.alt;
}

Result handleKeyInNormal(const State& state, const Key& key)
{
    if (key.special != SpecialKey::none)
        return handleSpecialInNormal(state, key);

    // Chords stay the app's (ctrl-n/p, ⌘D, its quit and rename bindings).
    if (key.command || key.control)
        return {state, Action::passThrough};

    if (key.alt)
        return stay(state, Action::none);

    if (state.pending != PendingOperator::none)
        return finishOperator(state, key.characters);

    return handleTypedInNormal(state, key.characters);
}
} // namespace

State getOpeningState()
{
    return {};
}

bool isNormalMode(const State& state)
{
    return state.mode == Mode::normal;
}

Result handleKey(const State& state, const Key& key)
{
    if (isNormalMode(state))
        return handleKeyInNormal(state, key);

    if (key.special == SpecialKey::escape && !hasModifier(key))
    {
        auto normal = state;
        normal.mode = Mode::normal;
        normal.pending = PendingOperator::none;
        return {normal, Action::none};
    }

    return {state, Action::passThrough};
}
} // namespace searchkeys
