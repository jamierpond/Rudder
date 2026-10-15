#include "LineEdit.h"

#include <algorithm>
#include <cctype>

namespace rudder::line
{
namespace
{
bool isContinuation(char c)
{
    return ((unsigned char) c & 0xC0) == 0x80;
}

int clampCursor(const Line& line)
{
    return std::clamp(line.cursor, 0, (int) line.text.size());
}

int previous(const std::string& text, int from)
{
    auto at = from - 1;

    while (at > 0 && isContinuation(text[(std::size_t) at]))
        --at;

    return std::max(at, 0);
}

int next(const std::string& text, int from)
{
    auto at = from + 1;

    while (at < (int) text.size() && isContinuation(text[(std::size_t) at]))
        ++at;

    return std::min(at, (int) text.size());
}

// Non-ASCII bytes count as word characters, so accented names move as words.
bool isWordByte(char c)
{
    return std::isalnum((unsigned char) c) || ((unsigned char) c & 0x80) != 0;
}

bool isSpace(char c)
{
    return std::isspace((unsigned char) c) != 0;
}

int wordStartBefore(const std::string& text, int from)
{
    auto at = from;

    while (at > 0 && !isWordByte(text[(std::size_t) at - 1]))
        --at;

    while (at > 0 && isWordByte(text[(std::size_t) at - 1]))
        --at;

    return at;
}

int wordEndAfter(const std::string& text, int from)
{
    auto at = from;
    auto size = (int) text.size();

    while (at < size && !isWordByte(text[(std::size_t) at]))
        ++at;

    while (at < size && isWordByte(text[(std::size_t) at]))
        ++at;

    return at;
}

Line erase(Line line, int from, int to)
{
    line.text.erase((std::size_t) from, (std::size_t) (to - from));
    line.cursor = from;
    return line;
}
} // namespace

Line left(Line line)
{
    line.cursor = previous(line.text, clampCursor(line));
    return line;
}

Line right(Line line)
{
    auto at = clampCursor(line);
    line.cursor = at < (int) line.text.size() ? next(line.text, at) : at;
    return line;
}

Line start(Line line)
{
    line.cursor = 0;
    return line;
}

Line end(Line line)
{
    line.cursor = (int) line.text.size();
    return line;
}

Line wordLeft(Line line)
{
    line.cursor = wordStartBefore(line.text, clampCursor(line));
    return line;
}

Line wordRight(Line line)
{
    line.cursor = wordEndAfter(line.text, clampCursor(line));
    return line;
}

Line backspace(Line line)
{
    auto at = clampCursor(line);

    if (at == 0)
        return line;

    auto from = previous(line.text, at);
    return erase(std::move(line), from, at);
}

Line deleteForward(Line line)
{
    auto at = clampCursor(line);

    if (at >= (int) line.text.size())
        return line;

    auto to = next(line.text, at);
    return erase(std::move(line), at, to);
}

Line deleteWhitespaceWord(Line line)
{
    auto at = clampCursor(line);
    auto from = at;

    while (from > 0 && isSpace(line.text[(std::size_t) from - 1]))
        --from;

    while (from > 0 && !isSpace(line.text[(std::size_t) from - 1]))
        --from;

    return erase(std::move(line), from, at);
}

Line deleteWord(Line line)
{
    auto at = clampCursor(line);
    auto from = wordStartBefore(line.text, at);
    return erase(std::move(line), from, at);
}

Line deleteToStart(Line line)
{
    auto at = clampCursor(line);
    return erase(std::move(line), 0, at);
}

Line deleteToEnd(Line line)
{
    auto at = clampCursor(line);
    auto size = (int) line.text.size();
    return erase(std::move(line), at, size);
}

Line insert(Line line, const std::string& text)
{
    auto at = clampCursor(line);
    line.text.insert((std::size_t) at, text);
    line.cursor = at + (int) text.size();
    return line;
}

float scrollToCaret(float caret, float textWidth, float visible, float scroll)
{
    if (caret - scroll > visible)
        scroll = caret - visible;

    if (caret < scroll)
        scroll = caret;

    return std::clamp(scroll, 0.f, std::max(0.f, textWidth - visible));
}
} // namespace rudder::line
