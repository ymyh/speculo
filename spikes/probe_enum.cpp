// probe: 枚举项取值到底怎么 extract
#include <meta>

#include <print>
#include <string>

namespace m = std::meta;

enum Plain
{
    A,
    B = 5,
    C
};
enum class Scoped : unsigned char
{
    X = 1,
    Y = 200
};

int main()
{
    // 变体 1：extract 成枚举类型本身
    static_assert(m::extract<Plain>(m::enumerators_of(^^Plain)[1]) == Plain::B);
    static_assert(m::extract<Scoped>(m::enumerators_of(^^Scoped)[1]) == Scoped::Y);

    // 变体 2：extract 成底层类型（通过 constant_of）
    // static_assert(m::extract<int>(m::constant_of(m::enumerators_of(^^Plain)[1])) == 5);

    constexpr auto up = m::display_string_of(m::underlying_type(^^Plain));
    constexpr auto us = m::display_string_of(m::underlying_type(^^Scoped));
    std::println("Plain underlying={}  Scoped underlying={}", up, us);
}
