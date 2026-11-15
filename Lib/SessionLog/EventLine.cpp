#include "EventLine.h"

namespace sessionlog
{
namespace
{
constexpr auto charactersThatNeedQuotes = std::string_view {" \t\n\"=\\"};

bool isQuotingNeeded(std::string_view value)
{
    return value.empty()
           || value.find_first_of(charactersThatNeedQuotes) != std::string_view::npos;
}

std::string quoteValue(std::string_view value)
{
    auto quoted = std::string {"\""};

    for (auto character: value)
    {
        if (character == '"' || character == '\\')
            quoted += '\\';

        if (character == '\n')
            quoted += "\\n";
        else
            quoted += character;
    }

    return quoted + "\"";
}
} // namespace

std::string formatEventLine(std::string_view event, std::initializer_list<Field> fields)
{
    auto line = std::string {event};

    for (auto& field: fields)
    {
        line += ' ' + field.key + '=';

        if (isQuotingNeeded(field.value))
            line += quoteValue(field.value);
        else
            line += field.value;
    }

    return line;
}
} // namespace sessionlog
