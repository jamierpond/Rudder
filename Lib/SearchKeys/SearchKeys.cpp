#include "SearchKeys.h"

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

Result handleTypedInNormal(const State& state, const std::string& typed)
{
    auto end = (int) state.text.size();

    if (typed == "j")
        return stay(state, Action::moveHighlightDown);
    if (typed == "k")
        return stay(state, Action::moveHighlightUp);
    if (typed == "G")
        return stay(state, Action::moveHighlightToLast);
    if (typed == "x")
        return stay(state, Action::closeHighlighted);
    if (typed == "s")
        return stay(state, Action::starHighlighted);
    if (typed == "i" || typed == "a" || typed == "/")
        return enterInsert(state, state.caret);
    if (typed == "A")
        return enterInsert(state, end);
    if (typed == "I")
        return enterInsert(state, 0);
    if (typed == "S")
        return clearAndEnterInsert(state);
    if (typed == "d")
        return waitFor(state, PendingOperator::deleteRow);
    if (typed == "g")
        return waitFor(state, PendingOperator::goToFirst);
    if (typed == "c")
        return waitFor(state, PendingOperator::changeText);

    return stay(state, Action::none);
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

    if (key.special == SpecialKey::escape && !key.command && !key.control && !key.alt)
    {
        auto normal = state;
        normal.mode = Mode::normal;
        normal.pending = PendingOperator::none;
        return {normal, Action::none};
    }

    return {state, Action::passThrough};
}
} // namespace searchkeys
