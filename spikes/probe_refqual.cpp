// probe —— 限制确认 1：ref-qualified 成员函数能否 extract 出成员函数指针。
#include <meta>

#include <utility>

namespace m = std::meta;

struct S
{
    void f() & {}
    void g() && {}
};

consteval m::info find(std::string_view nm)
{
    for (auto mm : m::members_of(^^S, m::access_context::unchecked()))
    {
        if (m::is_function(mm) && m::has_identifier(mm) && m::identifier_of(mm) == nm)
        {
            return mm;
        }
    }
    return m::info{};
}

template <m::info M>
consteval auto make_lref_invoker()
{
    using C = typename[: m::parent_of(M) :];
    return +[](C* p)
    {
        constexpr void (C::*fp)() & = m::extract<void (C::*)() &>(M);
        (p->*fp)();
    };
}

int main()
{
    S s;
    auto inv = make_lref_invoker<find("f")>();
    inv(&s);
}
