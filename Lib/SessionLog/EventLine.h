#pragma once

#include <concepts>
#include <initializer_list>
#include <string>
#include <string_view>

namespace sessionlog
{
struct Field
{
    Field(std::string_view keyToUse, std::string_view valueToUse)
        : key(keyToUse)
        , value(valueToUse)
    {
    }

    template <std::integral Number>
    Field(std::string_view keyToUse, Number number)
        : key(keyToUse)
        , value(std::to_string(number))
    {
    }

    std::string key;
    std::string value;
};

// One greppable log line: `<event> key=value ...`, events dotted
// (`palette.open via=cmd-l`). A value holding a space, quote, '=' or newline,
// or an empty one, is written in double quotes with \" \\ \n escapes.
std::string formatEventLine(std::string_view event, std::initializer_list<Field> fields = {});
} // namespace sessionlog
