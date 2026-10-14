#pragma once

#include <string>

namespace rudder
{
// The query field's readline/emacs editing, as fzf does it: pure functions on
// the text and a byte cursor that never lands inside a UTF-8 sequence.
struct Line
{
    std::string text;
    int cursor = 0;

    bool operator==(const Line&) const = default;
};

namespace line
{
// ctrl+b / ctrl+f
Line left(Line line);
Line right(Line line);

// ctrl+a / ctrl+e
Line start(Line line);
Line end(Line line);

// alt+b / alt+f: over runs of letters and digits.
Line wordLeft(Line line);
Line wordRight(Line line);

// ctrl+h (backspace) / ctrl+d
Line backspace(Line line);
Line deleteForward(Line line);

// ctrl+w: back to the previous whitespace (unix-word-rubout).
Line deleteWhitespaceWord(Line line);

// alt+backspace: back over one word of letters and digits.
Line deleteWord(Line line);

// ctrl+u / ctrl+k
Line deleteToStart(Line line);
Line deleteToEnd(Line line);
} // namespace line
} // namespace rudder
