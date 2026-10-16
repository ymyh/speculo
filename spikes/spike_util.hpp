// spike 通用小工具。
#pragma once

#include <meta>

#include <cstddef>
#include <string>
#include <string_view>

namespace spike
{

// 编译期十进制转字符串。libstdc++ 16.2 的 std::to_string 不是 constexpr，
// 所以在 consteval 里必须手写（库实现同理，见 review.md）。
consteval std::string to_dec(unsigned long long v)
{
    if (v == 0)
    {
        return "0";
    }
    std::string s;
    while (v != 0)
    {
        s.insert(s.begin(), static_cast<char>('0' + v % 10));
        v /= 10;
    }
    return s;
}

consteval std::string to_dec(long long v)
{
    if (v < 0)
    {
        return "-" + to_dec(static_cast<unsigned long long>(-(v + 1)) + 1ULL);
    }
    return to_dec(static_cast<unsigned long long>(v));
}

} // namespace spike
