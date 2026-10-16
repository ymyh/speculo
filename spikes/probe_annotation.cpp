// probe —— GCC 16.2 是否支持 P2996 注解（[[= expr]]）+ annotations_of 检测。
#include <meta>

#include <print>
#include <string>

namespace m = std::meta;

struct reflect_impl_tag
{
};

struct S
{
    [[= reflect_impl_tag{}]] int annotated() const { return 1; }
    int plain() const { return 2; }
};

template<m::info M>
consteval auto ann_count() -> std::size_t
{
    return m::annotations_of(M).size();
}

template<m::info M>
consteval auto ann0_is_annotation() -> bool
{
    auto v = m::annotations_of(M);
    return !v.empty() && m::is_annotation(v[0]);
}

template<m::info M>
consteval auto ann0_type() -> std::string_view
{
    auto v = m::annotations_of(M);
    return v.empty() ? std::string_view{} : std::define_static_string(m::display_string_of(m::type_of(v[0])));
}

template<m::info M>
consteval auto ann0_matches() -> bool
{
    for (auto a : m::annotations_of(M))
    {
        if (m::is_annotation(a) && m::type_of(a) == ^^reflect_impl_tag)
        {
            return true;
        }
    }
    return false;
}

int main()
{
    std::println("annotated: count={} is_ann={} type='{}' matches={}",
                 ann_count<^^S::annotated>(), ann0_is_annotation<^^S::annotated>(),
                 ann0_type<^^S::annotated>(), ann0_matches<^^S::annotated>());
    std::println("plain: count={} matches={}",
                 ann_count<^^S::plain>(), ann0_matches<^^S::plain>());
}
