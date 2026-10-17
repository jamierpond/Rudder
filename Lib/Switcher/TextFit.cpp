#include "TextFit.h"

#include <vector>

namespace rudder
{
namespace
{
constexpr std::string_view ellipsis = "…";

bool continuationByte(char c)
{
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}
} // namespace

std::string ellipsized(std::string_view text,
                       float width,
                       const std::function<float(std::string_view)>& measure)
{
    if (measure(text) <= width)
        return std::string {text};

    // Every place a cut may land, shortest first.
    auto cuts = std::vector<std::size_t> {0};

    for (std::size_t i = 1; i < text.size(); ++i)
        if (!continuationByte(text[i]))
            cuts.push_back(i);

    auto with = [&](std::size_t cut)
    {
        auto shown = std::string {text.substr(0, cut)};

        while (!shown.empty() && shown.back() == ' ')
            shown.pop_back();

        return shown + std::string {ellipsis};
    };

    // The longest cut that fits; widths grow with the prefix.
    auto low = std::size_t {0};
    auto high = cuts.size();

    while (high - low > 1)
    {
        auto mid = (low + high) / 2;

        if (measure(with(cuts[mid])) <= width)
            low = mid;
        else
            high = mid;
    }

    return with(cuts[low]);
}
} // namespace rudder
