// S8.10 补充：把 cast thunk 放进 volatile 函数指针，阻止内联，
//   以便在 -O2 汇编里核对「非虚基 → 立即数偏移」「虚基 → 必须读 vtable」。
//   编译：g++ -std=c++26 -freflection -O2 -S s8_10b_offset_asm.cpp
#include <meta>

#include <cstddef>
#include <print>

namespace m = std::meta;

struct P1
{
    int p1;
};
struct P2
{
    int p2;
};
struct E : P1, P2
{
    int e;
};

struct VA
{
    virtual ~VA() = default;
    int a;
};
struct VB : virtual VA
{
    int b;
};

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

// volatile 函数指针：调用不可内联，thunk 必须独立生成
void* (*volatile g_cast_p2)(void*) = make_cast_thunk<^^E, ^^P2>();
void* (*volatile g_cast_va)(void*) = make_cast_thunk<^^VB, ^^VA>();

// 同样形态，但用普通模板写一遍，作为代码生成的对照
template <class D, class B>
void* plain_cast_thunk(void* p)
{
    return static_cast<B*>(static_cast<D*>(p));
}
void* (*volatile g_plain_p2)(void*) = &plain_cast_thunk<E, P2>;

int main()
{
    E e{};
    VB vb{};
    auto off_p2 = static_cast<std::byte*>(g_cast_p2(&e)) - reinterpret_cast<std::byte*>(&e);
    auto off_va = static_cast<std::byte*>(g_cast_va(&vb)) - reinterpret_cast<std::byte*>(&vb);
    auto off_plain = static_cast<std::byte*>(g_plain_p2(&e)) - reinterpret_cast<std::byte*>(&e);
    std::println("offset(P2 in E) via meta thunk   = {}", static_cast<long long>(off_p2));
    std::println("offset(P2 in E) via plain thunk  = {}", static_cast<long long>(off_plain));
    std::println("offset(VA in VB) via meta thunk  = {}", static_cast<long long>(off_va));
    std::println("S8.10b ok");
}
