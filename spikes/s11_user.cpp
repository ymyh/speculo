// S11 探针（用户侧）：验证模块导出的模板在用户 TU 实例化时
// 能否遍历本地类型，以及位域 / 静态成员 / const 对象的行为。
#include <array>
#include <meta>
#include <print>
#include <string_view>

import s11;

struct Point
{
    int x;
    double y;
    unsigned bits : 3;
    static int counter;
    const char* name;
};

int Point::counter = 7;

struct OnlyStatic
{
    static int value;
};

int OnlyStatic::value = 11;

int main()
{
    Point p{ 1, 2.5, 5, "p" };

    int seen = 0;
    s11::each_field(p, [&](std::string_view name, auto&& value)
    {
        ++seen;
        std::println("field {} = {}", name, value);
    });
    std::println("seen={} expected=4 (nonstatic only)", seen);

    seen = 0;
    s11::each_field_with_static(p, [&](std::string_view name, auto&& value)
    {
        ++seen;
        std::println("field+static {} = {}", name, value);
    });
    std::println("seen={} expected=5", seen);

    const Point cp{ 3, 4.5, 1, "cp" };
    seen = 0;
    s11::each_field(cp, [&](std::string_view, const auto&) { ++seen; });
    std::println("const seen={} expected=4", seen);

    OnlyStatic os{};
    s11::each_field_with_static(os, [&](std::string_view name, auto&& value)
    {
        std::println("only-static {} = {}", name, value);
    });

    static_assert(s11::count_public_fields<Point>() == 5);
    static_assert(s11::count_first_two<Point>() == 2);
    std::println("S11 ok");
}
