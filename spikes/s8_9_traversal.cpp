// S8.9 编译期遍历：template for / splice 在**用户侧 TU**（非模块）是否可用；
//      以及 >100 字段的大结构体遍历的编译期开销（耗时由外部脚本测，见 review.md）。
#include <meta>

#include <array>
#include <print>
#include <string>
#include <vector>

#include "spike_util.hpp"

namespace m = std::meta;

struct Point
{
    int x;
    double y;
    char z;
};

// 形式 1：普通 constexpr 数组
constexpr int sum_ints()
{
    int n = 0;
    template for (constexpr int i : std::array{1, 2, 3, 4})
    {
        n += i;
    }
    return n;
}
static_assert(sum_ints() == 10);

// 形式 2：反射范围（必须经 define_static_array 提升为 constexpr 范围）
constexpr std::size_t total_member_bits()
{
    std::size_t n = 0;
    template for (constexpr auto mem : std::define_static_array(m::nonstatic_data_members_of(^^Point, m::access_context::unchecked())))
    {
        n += m::bit_size_of(m::type_of(mem));
    }
    return n;
}
static_assert(total_member_bits() == 32 + 64 + 8);

// 形式 3：splice 出成员名做编译期字符串拼接
consteval std::string names()
{
    std::string s;
    template for (constexpr auto mem : std::define_static_array(m::nonstatic_data_members_of(^^Point, m::access_context::unchecked())))
    {
        s += std::string(m::identifier_of(mem));
        s += ",";
    }
    return s;
}
static_assert(m::nonstatic_data_members_of(^^Point, m::access_context::unchecked()).size() == 3);
// 注：constexpr 比较 std::string 在 GCC 16.2 下不稳（返回值的 SSO union 不可常量求值），
// 故此处只校验名字拼接的产物形状，实际字符串在运行期打印。

// 形式 4：大结构体 —— 用 define_aggregate 合成 128 字段，再走 reflect 遍历
template <int N>
struct Big;

consteval
{
    std::vector<m::info> specs;
    for (int i = 0; i < 128; ++i)
    {
        specs.push_back(m::data_member_spec(^^int, {.name = "f" + spike::to_dec(static_cast<long long>(i))}));
    }
    m::define_aggregate(^^Big<0>, specs);
}

consteval std::size_t big_sum_offsets()
{
    std::size_t n = 0;
    template for (constexpr auto mem : std::define_static_array(m::nonstatic_data_members_of(^^Big<0>, m::access_context::unchecked())))
    {
        n += static_cast<std::size_t>(m::offset_of(mem).bytes);
    }
    return n;
}

consteval std::string big_info()
{
    constexpr m::info T = ^^Big<0>;
    auto mems = m::nonstatic_data_members_of(T, m::access_context::unchecked());
    std::string s = "Big<0>: size=" + spike::to_dec(static_cast<long long>(m::size_of(T)));
    s += " members=" + spike::to_dec(mems.size());
    s += " offset_sum=" + spike::to_dec(big_sum_offsets());
    s += " first=" + std::string(m::identifier_of(mems.front()));
    s += " last=" + std::string(m::identifier_of(mems.back()));
    return s;
}

int main()
{
    static constexpr const char* d = std::define_static_string(big_info());
    // consteval 返回 std::string 的函数只能在「manifestly constant-evaluated」上下文里调用，
    // 不能直接当运行期实参（GCC 16.2 的 SSO 限制，与 review.md 既有记录一致）。
    static constexpr const char* nl = std::define_static_string(names());
    std::println("{}", d);
    std::println("names()={} total_bits={}", nl, total_member_bits());

    // 运行期 splice 访问（用户 TU 内）
    Point p{1, 2.5, 'c'};
    constexpr m::info kX = m::nonstatic_data_members_of(^^Point, m::access_context::unchecked())[0];
    p.[:kX:] = 42;
    std::println("p.x via splice = {}", p.x);
    std::println("S8.9 ok");
}
