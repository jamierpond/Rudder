#pragma once

#include <string>

namespace searchkeys
{
// Vim-style modes for a search box over a list of rows. Pure: the app hands
// in its key and the box's text and caret, applies the state it gets back
// and turns the action into its own effect.

enum class Mode
{
    insert,
    normal
};

// The first key of a two-key command (dd, gg, cc), waiting for the second.
enum class PendingOperator
{
    none,
    deleteRow,
    goToFirst,
    changeText
};

struct State
{
    Mode mode = Mode::insert;
    PendingOperator pending = PendingOperator::none;
    std::string text;
    // A byte offset into `text`.
    int caret = 0;

    bool operator==(const State&) const = default;
};

enum class SpecialKey
{
    none,
    escape,
    enter,
    upArrow,
    downArrow
};

struct Key
{
    SpecialKey special = SpecialKey::none;
    // What the key types, UTF-8; empty for a key that types nothing.
    std::string characters;
    bool command = false;
    bool control = false;
    bool alt = false;
};

enum class Action
{
    // The app handles the key as it did before modes existed: typing,
    // editing keys, ctrl-n/p, its own chords.
    passThrough,
    none,
    moveHighlightDown,
    moveHighlightUp,
    moveHighlightToFirst,
    moveHighlightToLast,
    openHighlighted,
    openHighlightedInNewTab,
    closeHighlighted,
    starHighlighted,
    closeSearch
};

struct Result
{
    State state;
    Action action = Action::none;

    bool operator==(const Result&) const = default;
};

// A fresh search box: insert mode, empty text.
State getOpeningState();

Result handleKey(const State& state, const Key& key);

bool isNormalMode(const State& state);
} // namespace searchkeys
