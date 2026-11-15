#pragma once

#include <VimEditor.h>

#include <string>

namespace searchkeys
{
enum class SpecialKey
{
    none,
    escape,
    enter,
    upArrow,
    downArrow,
    leftArrow,
    rightArrow,
    backspace,
    forwardDelete,
    home,
    end
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
    // The app handles the key itself: its control and command chords, Tab.
    passThrough,
    // The box took the key; the app shows the box's new text, caret and mode.
    none,
    moveHighlightDown,
    moveHighlightUp,
    moveHighlightToFirst,
    moveHighlightToLast,
    openHighlighted,
    openHighlightedInNewTab,
    starHighlighted,
    closeSearch
};

// A one-line vim search box over a list of rows. Text editing is the vim
// editor's; this adds the list's keys on top: Enter opens, arrows and j/k
// move the highlight, gg/G jump to the ends, s stars, Esc in normal closes,
// ctrl+w in insert deletes the word behind the caret.
class SearchBox
{
public:
    // Insert mode, empty text.
    SearchBox();

    Action handleKey(const Key& key);

    // For the app's own edits (emacs chords, completion, a click); the mode stays.
    void setTextAndCaret(const std::string& text, int caret);

    std::string getText() const;

    // A byte offset into the text; on the last character at most in normal mode.
    int getCaret() const;

    bool isNormalMode() const;

private:
    Action handleChord(const Key& key);
    Action handleKeyInInsert(const Key& key);
    Action handleKeyInNormal(const Key& key);
    Action handleTypedInNormal(const std::string& typed);
    void feedEditorKeepingOneLine(const std::string& editorKey);
    void feedEditor(const Key& key);

    vim::Editor editor;
    bool isWaitingForSecondG = false;
};
} // namespace searchkeys
