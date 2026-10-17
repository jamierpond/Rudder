#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace rudder
{
// `text` as it fits in `width` points: whole if it fits, else the longest
// prefix (cut on a UTF-8 character boundary) that fits with "…" after it.
// `measure` is the font's width of a string.
std::string ellipsized(std::string_view text,
                       float width,
                       const std::function<float(std::string_view)>& measure);
} // namespace rudder
