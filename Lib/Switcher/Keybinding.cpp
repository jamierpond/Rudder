#include "Keybinding.h"

#include <eacp/Core/Platform/Platform.h>

#include <algorithm>
#include <iterator>
#include <cctype>
#include <utility>
#include <vector>

namespace rudder
{
namespace
{
struct NamedKey
{
    const char* name;
    std::uint16_t code;
    const char* label;
};

// macOS virtual key codes (Carbon kVK_*), the values eacp::Graphics::KeyCode
// spells out for the subset it names.
constexpr NamedKey keys[] = {
    {"a", 0x00, "A"},       {"s", 0x01, "S"},      {"d", 0x02, "D"},
    {"f", 0x03, "F"},       {"h", 0x04, "H"},      {"g", 0x05, "G"},
    {"z", 0x06, "Z"},       {"x", 0x07, "X"},      {"c", 0x08, "C"},
    {"v", 0x09, "V"},       {"b", 0x0B, "B"},      {"q", 0x0C, "Q"},
    {"w", 0x0D, "W"},       {"e", 0x0E, "E"},      {"r", 0x0F, "R"},
    {"y", 0x10, "Y"},       {"t", 0x11, "T"},      {"o", 0x1F, "O"},
    {"u", 0x20, "U"},       {"i", 0x22, "I"},      {"p", 0x23, "P"},
    {"l", 0x25, "L"},       {"j", 0x26, "J"},      {"k", 0x28, "K"},
    {"n", 0x2D, "N"},       {"m", 0x2E, "M"},      {"0", 0x1D, "0"},
    {"1", 0x12, "1"},       {"2", 0x13, "2"},      {"3", 0x14, "3"},
    {"4", 0x15, "4"},       {"5", 0x17, "5"},      {"6", 0x16, "6"},
    {"7", 0x1A, "7"},       {"8", 0x1C, "8"},      {"9", 0x19, "9"},
    {"space", 0x31, "Space"}, {"`", 0x32, "`"},    {"-", 0x1B, "-"},
    {"=", 0x18, "="},       {"[", 0x21, "["},      {"]", 0x1E, "]"},
    {"\\", 0x2A, "\\"},     {";", 0x29, ";"},      {"'", 0x27, "'"},
    {",", 0x2B, ","},       {".", 0x2F, "."},      {"/", 0x2C, "/"},
    {"tab", 0x30, "Tab"},   {"return", 0x24, "↩"}, {"escape", 0x35, "Esc"},
    {"left", 0x7B, "←"},    {"right", 0x7C, "→"},  {"down", 0x7D, "↓"},
    {"up", 0x7E, "↑"},      {"f1", 0x7A, "F1"},    {"f2", 0x78, "F2"},
    {"f3", 0x63, "F3"},     {"f4", 0x76, "F4"},    {"f5", 0x60, "F5"},
    {"f6", 0x61, "F6"},     {"f7", 0x62, "F7"},    {"f8", 0x64, "F8"},
    {"f9", 0x65, "F9"},     {"f10", 0x6D, "F10"},  {"f11", 0x67, "F11"},
    {"f12", 0x6F, "F12"},
};

const NamedKey* findKey(const std::string& name)
{
    auto found = std::find_if(std::begin(keys),
                              std::end(keys),
                              [&](const NamedKey& key) { return name == key.name; });

    return found != std::end(keys) ? found : nullptr;
}

std::string lowered(std::string text)
{
    for (auto& c: text)
        c = (char) std::tolower((unsigned char) c);

    return text;
}

// "+" separates parts, so a chord can't bind the "+" key itself -- it is
// shift+= on the layouts that have one anyway.
std::vector<std::string> splitParts(const std::string& text)
{
    auto parts = std::vector<std::string> {};
    auto current = std::string {};

    for (auto c: text)
    {
        if (c == '+')
        {
            parts.push_back(current);
            current.clear();
        }
        else if (c != ' ')
            current += c;
    }

    parts.push_back(current);
    return parts;
}

bool isFunctionKey(const std::string& key)
{
    return key.size() >= 2 && key[0] == 'f' && std::isdigit((unsigned char) key[1]);
}
} // namespace

std::optional<Chord> parseChord(const std::string& text)
{
    auto parts = splitParts(lowered(text));
    auto chord = Chord {};

    for (std::size_t i = 0; i + 1 < parts.size(); ++i)
    {
        auto& part = parts[i];

        if (part == "control" || part == "ctrl")
            chord.control = true;
        else if (part == "option" || part == "opt" || part == "alt")
            chord.option = true;
        else if (part == "shift")
            chord.shift = true;
        else if (part == "command" || part == "cmd" || part == "win")
            chord.command = true;
        else
            return std::nullopt;
    }

    chord.key = parts.back();

    if (findKey(chord.key) == nullptr)
        return std::nullopt;

    auto modified = chord.control || chord.option || chord.shift || chord.command;

    if (!modified && !isFunctionKey(chord.key))
        return std::nullopt;

    return chord;
}

std::string serialiseChord(const Chord& chord)
{
    auto text = std::string {};

    if (chord.control)
        text += "control+";
    if (chord.option)
        text += "option+";
    if (chord.shift)
        text += "shift+";
    if (chord.command)
        text += "command+";

    return text + chord.key;
}

std::string displayChord(const Chord& chord)
{
    auto* key = findKey(chord.key);
    auto label = std::string {key != nullptr ? key->label : chord.key};

    // Windows menus spell chords out, and its command key is the Windows key.
    if constexpr (eacp::Platform::isWindows())
    {
        auto text = std::string {};

        if (chord.control)
            text += "Ctrl+";
        if (chord.option)
            text += "Alt+";
        if (chord.shift)
            text += "Shift+";
        if (chord.command)
            text += "Win+";

        return text + label;
    }

    auto text = std::string {};

    if (chord.control)
        text += "⌃";
    if (chord.option)
        text += "⌥";
    if (chord.shift)
        text += "⇧";
    if (chord.command)
        text += "⌘";

    return text + label;
}

std::optional<std::uint16_t> keyCodeFor(const std::string& key)
{
    if (auto* found = findKey(key))
        return found->code;

    return std::nullopt;
}
bool matchesChord(const Chord& chord,
                  std::uint16_t keyCode,
                  bool control,
                  bool option,
                  bool shift,
                  bool command)
{
    return keyCodeFor(chord.key) == keyCode && chord.control == control
           && chord.option == option && chord.shift == shift
           && chord.command == command;
}

std::optional<std::string> keyNameFor(std::uint16_t keyCode)
{
    for (auto& key: keys)
        if (key.code == keyCode)
            return key.name;

    return std::nullopt;
}
} // namespace rudder
