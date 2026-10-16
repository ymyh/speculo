// probe —— 限制确认 2：构造函数能否 extract 出成员函数指针。
#include <meta>

#include <utility>

namespace m = std::meta;

struct S
{
    S(int) {}
};

consteval m::info find_ctor()
{
    for (auto mm : m::members_of(^^S, m::access_context::unchecked()))
    {
        if (m::is_constructor(mm))
        {
            return mm;
        }
    }
    return m::info{};
}

template <m::info M>
consteval auto make_ctor_ptr()
{
    return m::extract<void (S::*)(int)>(M);
}

int main()
{
    (void)make_ctor_ptr<find_ctor()>();
}
