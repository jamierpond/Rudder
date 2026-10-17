#include <TextFit.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace rudder;

namespace
{
// One point per character, UTF-8 continuation bytes taking none.
float monospace(std::string_view text)
{
    auto width = 0.f;

    for (auto c: text)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
            width += 1.f;

    return width;
}
} // namespace

auto tFitWhole = test("TextFit/text that fits is untouched") = []
{
    check(ellipsized("Rudder", 6.f, monospace) == "Rudder");
    check(ellipsized("", 0.f, monospace).empty());
};

auto tFitCut = test("TextFit/too long ends in an ellipsis inside the width") = []
{
    check(ellipsized("~/projects/Rudder", 8.f, monospace) == "~/proje…");
    check(monospace(ellipsized("~/projects/Rudder", 8.f, monospace)) <= 8.f);
};

auto tFitSpace = test("TextFit/no space before the ellipsis") = []
{
    check(ellipsized("Rudder · CowTerm", 9.f, monospace) == "Rudder ·…");
    check(ellipsized("ab cd", 4.f, monospace) == "ab…");
};

auto tFitUtf8 = test("TextFit/cuts land between characters") = []
{
    // "·" is two bytes: never split.
    check(ellipsized("a·b·c·d", 4.f, monospace) == "a·b…");
};

auto tFitTiny = test("TextFit/narrower than the ellipsis is just the ellipsis") = []
{
    check(ellipsized("Rudder", 0.f, monospace) == "…");
};
