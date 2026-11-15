#include "MatchLevel.h"

#include <algorithm>
#include <cctype>

namespace frecencysearch
{
namespace
{
char toLowerCharacter(char character)
{
    auto unsignedCharacter = (unsigned char) character;
    return (char) std::tolower(unsignedCharacter);
}

bool isLetterOrDigit(char character)
{
    auto unsignedCharacter = (unsigned char) character;
    return std::isalnum(unsignedCharacter) != 0 || unsignedCharacter >= 0x80;
}

bool isUpperCase(char character)
{
    auto unsignedCharacter = (unsigned char) character;
    return std::isupper(unsignedCharacter) != 0;
}

bool isDigit(char character)
{
    auto unsignedCharacter = (unsigned char) character;
    return std::isdigit(unsignedCharacter) != 0;
}

bool startsNewWord(std::string_view text, std::size_t index)
{
    if (index == 0)
        return true;

    auto previous = text[index - 1];
    auto current = text[index];

    if (isUpperCase(current) && !isUpperCase(previous))
        return true;

    return isDigit(current) != isDigit(previous);
}

std::string toLowerText(std::string_view text)
{
    auto lower = std::string {};

    for (auto character: text)
        lower.push_back(toLowerCharacter(character));

    return lower;
}

std::string joinWords(const std::vector<std::string>& words)
{
    auto joined = std::string {};

    for (auto& word: words)
    {
        if (!joined.empty())
            joined.push_back(' ');

        joined += word;
    }

    return joined;
}

// Where in the joined words the query starts on a word: 0 for the first
// word, the word's index otherwise.
std::optional<std::size_t> findWordStartingWith(std::string_view text,
                                                std::string_view normalisedQuery)
{
    auto words = splitIntoWords(text);
    auto joined = joinWords(words);
    auto offset = std::size_t {0};

    for (std::size_t wordIndex = 0; wordIndex < words.size(); ++wordIndex)
    {
        if (joined.compare(offset, normalisedQuery.size(), normalisedQuery) == 0)
            return wordIndex;

        offset += words[wordIndex].size() + 1;
    }

    return std::nullopt;
}
} // namespace

std::string normaliseQuery(std::string_view query)
{
    auto normalised = std::string {};

    for (auto character: query)
    {
        auto isLeadingOrRepeatedSpace = character == ' ' && (normalised.empty() || normalised.back() == ' ');

        if (!isLeadingOrRepeatedSpace)
            normalised.push_back(toLowerCharacter(character));
    }

    if (!normalised.empty() && normalised.back() == ' ')
        normalised.pop_back();

    return normalised;
}

std::vector<std::string> splitIntoWords(std::string_view text)
{
    auto words = std::vector<std::string> {};

    for (std::size_t index = 0; index < text.size(); ++index)
    {
        if (!isLetterOrDigit(text[index]))
            continue;

        auto followsSeparator = index == 0 || !isLetterOrDigit(text[index - 1]);

        if (followsSeparator || startsNewWord(text, index))
            words.emplace_back();

        words.back().push_back(toLowerCharacter(text[index]));
    }

    return words;
}

std::string removeUrlScheme(std::string_view url)
{
    auto schemeEnd = url.find("://");

    if (schemeEnd == std::string_view::npos)
        return std::string {url};

    return std::string {url.substr(schemeEnd + 3)};
}

bool hasWordStartingWith(std::string_view text, std::string_view query)
{
    return findWordStartingWith(text, normaliseQuery(query)).has_value();
}

bool hasLettersInOrder(std::string_view query, std::string_view text)
{
    auto normalisedQuery = normaliseQuery(query);
    auto textIndex = std::size_t {0};

    for (auto letter: normalisedQuery)
    {
        if (letter == ' ')
            continue;

        while (textIndex < text.size() && toLowerCharacter(text[textIndex]) != letter)
            ++textIndex;

        if (textIndex == text.size())
            return false;

        ++textIndex;
    }

    return true;
}

std::optional<MatchLevel> findNameMatchLevel(std::string_view query, std::string_view name)
{
    auto normalisedQuery = normaliseQuery(query);

    if (normalisedQuery.empty() || name.empty())
        return std::nullopt;

    auto lowerName = toLowerText(name);

    if (lowerName.starts_with(normalisedQuery))
        return MatchLevel::nameStartsWithQuery;

    if (auto wordIndex = findWordStartingWith(name, normalisedQuery))
    {
        if (*wordIndex == 0)
            return MatchLevel::nameStartsWithQuery;

        return MatchLevel::nameWordStartsWithQuery;
    }

    if (lowerName.find(normalisedQuery) != std::string::npos)
        return MatchLevel::nameContainsQueryOrDetailWordStartsWithQuery;

    if (hasLettersInOrder(normalisedQuery, name))
        return MatchLevel::nameHasQueryLettersInOrder;

    return std::nullopt;
}

bool isBetterMatchLevel(std::optional<MatchLevel> candidate,
                        std::optional<MatchLevel> current)
{
    if (!candidate)
        return false;

    return !current || *candidate < *current;
}

namespace
{
std::optional<MatchLevel> findBestNameMatchLevel(std::string_view normalisedQuery,
                                                 const std::vector<std::string>& names)
{
    auto best = std::optional<MatchLevel> {};

    for (auto& name: names)
    {
        auto level = findNameMatchLevel(normalisedQuery, name);

        if (isBetterMatchLevel(level, best))
            best = level;
    }

    return best;
}

std::optional<MatchLevel> findWholeQueryMatchLevel(std::string_view normalisedQuery,
                                                   const SearchableTexts& texts)
{
    auto best = findBestNameMatchLevel(normalisedQuery, texts.names);

    auto detailLevel = MatchLevel::nameContainsQueryOrDetailWordStartsWithQuery;

    if (isBetterMatchLevel(detailLevel, best))
        for (auto& detail: texts.details)
            if (hasWordStartingWith(detail, normalisedQuery))
                return detailLevel;

    return best;
}

std::vector<std::string> splitQueryAtSpaces(std::string_view normalisedQuery)
{
    auto words = std::vector<std::string> {};
    auto wordStart = std::size_t {0};

    while (wordStart <= normalisedQuery.size())
    {
        auto wordEnd = std::min(normalisedQuery.find(' ', wordStart), normalisedQuery.size());
        words.emplace_back(normalisedQuery.substr(wordStart, wordEnd - wordStart));
        wordStart = wordEnd + 1;
    }

    return words;
}

// Each word of a multi-word query found on its own, anywhere among the
// texts: as good as its worst word.
std::optional<MatchLevel> findEveryWordMatchLevel(std::string_view normalisedQuery,
                                                  const SearchableTexts& texts)
{
    auto worst = std::optional<MatchLevel> {};
    auto words = splitQueryAtSpaces(normalisedQuery);

    if (words.size() < 2)
        return std::nullopt;

    for (auto& word: words)
    {
        auto level = findWholeQueryMatchLevel(word, texts);

        if (!level)
            return std::nullopt;

        if (!worst || *level > *worst)
            worst = level;
    }

    return worst;
}
} // namespace

std::optional<MatchLevel> findMatchLevel(std::string_view query, const SearchableTexts& texts)
{
    auto normalisedQuery = normaliseQuery(query);
    auto level = findWholeQueryMatchLevel(normalisedQuery, texts);
    auto everyWordLevel = findEveryWordMatchLevel(normalisedQuery, texts);

    if (isBetterMatchLevel(everyWordLevel, level))
        return everyWordLevel;

    return level;
}
} // namespace frecencysearch
