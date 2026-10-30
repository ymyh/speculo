// S8.7 值类别：能否区分 & / && 限定的成员函数；std::move 转发到 (obj->*fp) 是否保留右值语义。
#include <meta>

#include <print>
#include <string>
#include <utility>

#include "spike_util.hpp"

namespace m = std::meta;

struct RQ
{
    std::string s = "hello";
    int lref_calls = 0;

    int lref() &
    {
        return ++lref_calls;
    }

    std::string take() &&
    {
        return std::move(s);
    }

    int cref() const &
    {
        return 7;
    }
};

template <m::info M>
consteval m::info find_member(std::string_view name)
{
    for (auto mm : m::members_of(M, m::access_context::unchecked()))
    {
        if (m::is_function(mm) && m::has_identifier(mm) && m::identifier_of(mm) == name)
        {
            return mm;
        }
    }
    return m::info{};
}

// & 限定：左值视图
template <m::info M>
consteval auto make_lref_invoker()
{
    using C = typename[:m::parent_of(M):];
    return +[](C* p) -> int
    {
        constexpr int (C::*fp)() & = m::extract<int (C::*)() &>(M);
        return (p->*fp)();
    };
}

// && 限定：右值视图（std::move 是否保留右值语义，看 take() 能否真的搬走 s）
template <m::info M>
consteval auto make_rref_invoker()
{
    using C = typename[:m::parent_of(M):];
    return +[](C* p) -> std::string
    {
        constexpr std::string (C::*fp)() && = m::extract<std::string (C::*)() &&>(M);
        return (std::move(*p).*fp)();
    };
}

template <m::info M>
consteval auto make_cref_invoker()
{
    using C = typename[:m::parent_of(M):];
    return +[](const C* p) -> int
    {
        constexpr int (C::*fp)() const & = m::extract<int (C::*)() const &>(M);
        return (p->*fp)();
    };
}

static_assert(m::is_lvalue_reference_qualified(find_member<^^RQ>("lref")));
static_assert(!m::is_rvalue_reference_qualified(find_member<^^RQ>("lref")));
static_assert(m::is_rvalue_reference_qualified(find_member<^^RQ>("take")));
static_assert(!m::is_lvalue_reference_qualified(find_member<^^RQ>("take")));
static_assert(m::is_const(find_member<^^RQ>("cref")));

int main()
{
    constexpr m::info kLref = find_member<^^RQ>("lref");
    constexpr m::info kTake = find_member<^^RQ>("take");
    constexpr m::info kCref = find_member<^^RQ>("cref");

    std::println("lref: lref_qualified={} rref_qualified={}", m::is_lvalue_reference_qualified(kLref), m::is_rvalue_reference_qualified(kLref));
    std::println("take: lref_qualified={} rref_qualified={}", m::is_lvalue_reference_qualified(kTake), m::is_rvalue_reference_qualified(kTake));
    std::println("type_of(lref)=[{}]", m::display_string_of(m::type_of(kLref)));
    std::println("type_of(take)=[{}]", m::display_string_of(m::type_of(kTake)));

    static constexpr auto lref_inv = make_lref_invoker<kLref>();
    static constexpr auto rref_inv = make_rref_invoker<kTake>();
    static constexpr auto cref_inv = make_cref_invoker<kCref>();

    RQ a;
    std::println("lref() call #1 = {}", lref_inv(&a));
    std::println("lref() call #2 = {}", lref_inv(&a));
    std::println("cref() = {}", cref_inv(&a));

    RQ b;
    std::string moved = rref_inv(&b);
    std::println("take() -> [{}], source s after rvalue call = [{}{}]", moved, b.s.empty() ? "<empty>" : b.s, "");
    if (!b.s.empty())
    {
        std::println("S8.7 FAILED: 右值语义未保留");
        return 1;
    }
    std::println("S8.7 ok: & / && 限定可区分，右值转发保留语义");
}
