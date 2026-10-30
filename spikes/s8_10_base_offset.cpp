// S8.10 基类偏移：cast thunk 的地址差能否在优化下化为立即数；虚基是否必须走运行期。
#include <meta>

#include <cstddef>
#include <print>

#include "spike_util.hpp"

namespace m = std::meta;

struct P1
{
    int p1;
};
struct P2
{
    int p2;
};
struct P3
{
    int p3;
};

// 非虚多继承
struct E : P1, P2, P3
{
    int e;
};

// 空基优化
struct EmptyBase
{
};
struct WithEmpty : EmptyBase, P1
{
    int v;
};

// 虚继承：同一个虚基在不同完整对象里偏移不同 → 无静态偏移
struct VA
{
    virtual ~VA() = default;
    int a;
};
struct VB : virtual VA
{
    int b;
};
struct VC : virtual VA
{
    int c;
};
struct VD : VB, VC
{
    int d;
};

// cast thunk：void* -> void*（Phase 9 的 BaseDesc::offset_fn 形态）
template <m::info D, m::info B>
consteval auto make_cast_thunk()
{
    using DT = typename[:D:];
    using BT = typename[:B:];
    return +[](void* p) -> void*
    {
        return static_cast<BT*>(static_cast<DT*>(p));
    };
}

// GCC 16.2 的实参顺序是 (base, derived)（与 std::is_virtual_base_of_v<Base, Derived> 一致），
// 与直觉相反，Phase 9 接线时必须按这条验证（见 review.md）。
static_assert(m::is_virtual_base_of_type(^^VA, ^^VB));
static_assert(!m::is_virtual_base_of_type(^^P2, ^^E));

int main()
{
    // 非虚多继承：直接量出偏移
    E e{};
    std::byte* base = reinterpret_cast<std::byte*>(&e);

    static constexpr auto cast_p1 = make_cast_thunk<^^E, ^^P1>();
    static constexpr auto cast_p2 = make_cast_thunk<^^E, ^^P2>();
    static constexpr auto cast_p3 = make_cast_thunk<^^E, ^^P3>();

    const auto off1 = static_cast<std::byte*>(cast_p1(&e)) - base;
    const auto off2 = static_cast<std::byte*>(cast_p2(&e)) - base;
    const auto off3 = static_cast<std::byte*>(cast_p3(&e)) - base;
    std::println("E: offset(P1)={} offset(P2)={} offset(P3)={} sizeof(E)={}", static_cast<long long>(off1), static_cast<long long>(off2), static_cast<long long>(off3), sizeof(E));

    // 空基
    WithEmpty we{};
    static constexpr auto cast_empty = make_cast_thunk<^^WithEmpty, ^^EmptyBase>();
    static constexpr auto cast_p1b = make_cast_thunk<^^WithEmpty, ^^P1>();
    const auto off_empty = static_cast<std::byte*>(cast_empty(&we)) - reinterpret_cast<std::byte*>(&we);
    const auto off_p1b = static_cast<std::byte*>(cast_p1b(&we)) - reinterpret_cast<std::byte*>(&we);
    std::println("WithEmpty: offset(EmptyBase)={} offset(P1)={} size={}", static_cast<long long>(off_empty), static_cast<long long>(off_p1b), sizeof(WithEmpty));

    // 虚基：同一虚基在不同完整对象里偏移不同 → 不可能有静态偏移
    VB vb{};
    VD vd{};
    static constexpr auto cast_va_vb = make_cast_thunk<^^VB, ^^VA>();
    static constexpr auto cast_va_vd = make_cast_thunk<^^VD, ^^VA>();
    const auto va_in_vb = static_cast<std::byte*>(cast_va_vb(&vb)) - reinterpret_cast<std::byte*>(&vb);
    const auto va_in_vd = static_cast<std::byte*>(cast_va_vd(&vd)) - reinterpret_cast<std::byte*>(&vd);
    std::println("VA offset in VB={} in VD={}  (两者不同 => 虚基只能运行期)", static_cast<long long>(va_in_vb), static_cast<long long>(va_in_vd));

    std::println("S8.10 ok");
}
