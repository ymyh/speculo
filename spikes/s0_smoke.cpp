// S0 —— 基本机制冒烟测试：确认 <meta> 可用、^^ 与拼接语法、members_of 下标访问、
//        offset_of / size_of 返回类型。
#include <meta>

#include <print>
#include <string_view>
#include <utility>

namespace m = std::meta;

struct Point
{
    int x;
    int y;
};

template <std::size_t I>
consteval m::info member_at()
{
    return m::members_of(^^Point, m::access_context::current())[I];
}

int main()
{
    static_assert(m::is_nonstatic_data_member(member_at<0>()));
    static_assert(m::identifier_of(member_at<0>()) == "x");
    static_assert(m::identifier_of(member_at<1>()) == "y");

    // offset_of 返回 member_offset{ bytes, bits }
    static_assert(m::offset_of(member_at<0>()).bytes == 0);
    static_assert(m::offset_of(member_at<1>()).bytes == 4);
    static_assert(m::offset_of(member_at<1>()).bits == 0);
    static_assert(m::size_of(^^Point) == 8);
    static_assert(m::alignment_of(^^Point) == 4);

    constexpr std::size_t member_count = m::members_of(^^Point, m::access_context::current()).size();
    constexpr std::size_t size = m::size_of(^^Point);
    constexpr std::size_t align = m::alignment_of(^^Point);
    std::println("S0 ok: members={} size={} align={}", member_count, size, align);
}
