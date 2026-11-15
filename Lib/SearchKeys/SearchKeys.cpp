#include "SearchKeys.h"

#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace searchkeys
{
namespace
{
struct SpecialKeyName
{
    SpecialKey special;
    std::string_view editorKey;
};

constexpr auto editorKeyNames = std::array {
    SpecialKeyName {SpecialKey::escape, "Esc"},
    SpecialKeyName {SpecialKey::leftArrow, "Left"},
    SpecialKeyName {SpecialKey::rightArrow, "Right"},
    SpecialKeyName {SpecialKey::backspace, "Backspace"},
    SpecialKeyName {SpecialKey::forwardDelete, "Delete"},
    SpecialKeyName {SpecialKey::home, "Home"},
    SpecialKeyName {SpecialKey::end, "End"},
};

std::optional<std::string> getEditorKeyName(SpecialKey special)
{
    for (auto& entry: editorKeyNames)
        if (entry.special == special)
            return std::string(entry.editorKey);

    return std::nullopt;
}

bool isContinuationByte(char c)
{
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// The editor takes one character per key.
std::vector<std::string> splitIntoCharacters(const std::string& text)
{
    auto characters = std::vector<std::string> {};

    for (auto c: text)
    {
        if (isContinuationByte(c) && !characters.empty())
            characters.back() += c;
        else
            characters.emplace_back(1, c);
    }

    return characters;
}

std::optional<Action> getListActionForArrowOrEnter(const Key& key)
{
    switch (key.special)
    {
        case SpecialKey::enter:
            if (key.command)
                return Action::openHighlightedInNewTab;

            return Action::openHighlighted;
        case SpecialKey::downArrow: return Action::moveHighlightDown;
        case SpecialKey::upArrow: return Action::moveHighlightUp;
        default: return std::nullopt;
    }
}

struct NormalListBinding
{
    std::string_view typed;
    Action action;
};

constexpr auto normalListBindings = std::array {
    NormalListBinding {"j", Action::moveHighlightDown},
    NormalListBinding {"k", Action::moveHighlightUp},
    NormalListBinding {"G", Action::moveHighlightToLast},
    NormalListBinding {"s", Action::starHighlighted},
};

bool isDeleteWordKey(const Key& key)
{
    return key.control && !key.command && !key.alt && key.characters == "w";
}

// Prompts the one-line box has no room to show.
bool isPromptKey(const std::string& typed)
{
    return typed == ":" || typed == "?";
}
} // namespace

SearchBox::SearchBox()
{
    editor.feed("i");
}

Action SearchBox::handleKey(const Key& key)
{
    if (auto listAction = getListActionForArrowOrEnter(key))
        return *listAction;

    if (key.command || key.control)
        return handleChord(key);

    if (isNormalMode())
        return handleKeyInNormal(key);

    return handleKeyInInsert(key);
}

Action SearchBox::handleKeyInInsert(const Key& key)
{
    if (key.special == SpecialKey::escape && getText().empty())
        return Action::closeSearch;

    if (key.special == SpecialKey::none && key.characters.empty())
        return Action::passThrough;

    feedEditor(key);
    return Action::none;
}

// Chords other than ctrl+w in insert mode stay the app's (ctrl-n/p, ⌘D, its
// quit and rename bindings).
Action SearchBox::handleChord(const Key& key)
{
    if (!isDeleteWordKey(key) || isNormalMode())
        return Action::passThrough;

    feedEditorKeepingOneLine("C-w");
    return Action::none;
}

Action SearchBox::handleKeyInNormal(const Key& key)
{
    auto isEscape = key.special == SpecialKey::escape;

    if (isWaitingForSecondG)
    {
        isWaitingForSecondG = false;

        if (key.characters == "g")
            return Action::moveHighlightToFirst;

        if (isEscape)
            return Action::none;

        feedEditorKeepingOneLine("g");
        feedEditor(key);
        return Action::none;
    }

    if (editor.hasPendingKeys())
    {
        feedEditor(key);
        return Action::none;
    }

    if (isEscape)
        return Action::closeSearch;

    if (key.special != SpecialKey::none)
    {
        feedEditor(key);
        return Action::none;
    }

    return handleTypedInNormal(key.characters);
}

Action SearchBox::handleTypedInNormal(const std::string& typed)
{
    for (auto& binding: normalListBindings)
        if (binding.typed == typed)
            return binding.action;

    if (typed == "g")
    {
        isWaitingForSecondG = true;
        return Action::none;
    }

    if (typed == "/")
    {
        feedEditorKeepingOneLine("i");
        return Action::none;
    }

    if (!isPromptKey(typed))
        feedEditor(Key {.characters = typed});

    return Action::none;
}

void SearchBox::feedEditor(const Key& key)
{
    if (auto name = getEditorKeyName(key.special))
    {
        feedEditorKeepingOneLine(*name);
        return;
    }

    for (auto& character: splitIntoCharacters(key.characters))
        feedEditorKeepingOneLine(character);
}

// A key that would make a second line (o, p after yy, Enter) changes nothing.
void SearchBox::feedEditorKeepingOneLine(const std::string& editorKey)
{
    auto textBefore = editor.text();
    auto caretBefore = getCaret();
    auto wasNormal = isNormalMode();

    editor.feed(editorKey);

    if (editor.lines().size() <= 1)
        return;

    editor.setText(textBefore);

    if (!wasNormal)
        editor.feed("i");

    editor.setCursor(0, caretBefore);
}

void SearchBox::setTextAndCaret(const std::string& text, int caret)
{
    if (text != editor.text())
    {
        auto wasNormal = isNormalMode();
        editor.setText(text);

        if (!wasNormal)
            editor.feed("i");
    }

    editor.setCursor(0, caret);
}

std::string SearchBox::getText() const
{
    return editor.text();
}

int SearchBox::getCaret() const
{
    return editor.column();
}

bool SearchBox::isNormalMode() const
{
    return editor.mode() != vim::Mode::insert;
}
} // namespace searchkeys
