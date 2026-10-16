// S4a —— 验证：模板中做「成员访问拼接」 obj.[:M:] 是否仍然 ICE。
//        期望：GCC 16.2 内部错误（这就是 dynref 改用 offset_of + launder 的原因）。
#include <meta>

#include <utility>

namespace m = std::meta;

struct S
{
    int x = 1;
    int y = 2;
};

template <m::info M>
consteval auto make_getter()
{
    using C = typename[: m::parent_of(M) :];
    return +[](const C* p) -> int
    { return p->[:M:]; };
}

int main()
{
    S s;
    auto g = make_getter<m::nonstatic_data_members_of(^^S, m::access_context::unchecked())[0]>();
    return g(&s) == 1 ? 0 : 1;
}
