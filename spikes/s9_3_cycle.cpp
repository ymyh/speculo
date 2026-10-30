// Phase 9 探针 3：bundle 之间的**地址互换**是否会在常量求值期触发「使用未初始化变量」。
// 场景：Outer 的嵌套类型表要指向 bundle<Outer::Inner>.cls，
//       而 Outer::Inner 的 enclosing 要指向 bundle<Outer>.desc —— 互相引用，两个都是取地址。
#include <meta>

#include <array>
#include <print>
#include <string>
#include <utility>

#include "spike_util.hpp"

namespace m = std::meta;

struct Outer
{
    struct Inner
    {
        int v;
    };
    enum class Color
    {
        Red
    };
};

template<m::info C>
consteval auto nested_count() -> std::size_t
{
    if constexpr (m::is_class_type(C) || m::is_union_type(C))
    {
        std::size_t count = 0;
        for (auto member : m::members_of(C, m::access_context::unchecked()))
        {
            if (m::is_type(member) && !m::is_type_alias(member))
            {
                ++count;
            }
        }
        return count;
    }
    else
    {
        return 0;
    }
}

template<m::info C, std::size_t I>
consteval auto nested_member() -> m::info
{
    std::size_t index = 0;
    for (auto member : m::members_of(C, m::access_context::unchecked()))
    {
        if (m::is_type(member) && !m::is_type_alias(member))
        {
            if (index == I)
            {
                return member;
            }
            ++index;
        }
    }
    return m::info{};
}

template<class T>
struct TypeBundle
{
    const void* enclosing = nullptr;
    std::array<const void*, nested_count<^^T>()> nested{};
};

template<class T>
inline constexpr TypeBundle<T> bundle = []() consteval -> TypeBundle<T>
{
    TypeBundle<T> b{};
    constexpr m::info C = ^^T;
    if constexpr (m::has_parent(C) && m::is_type(m::parent_of(C))
                  && (m::is_class_type(m::parent_of(C)) || m::is_union_type(m::parent_of(C))))
    {
        b.enclosing = std::addressof(bundle<typename[: m::parent_of(C) :]>.enclosing);
    }
    [&b]<std::size_t... I>(std::index_sequence<I...>)
    {
        ((b.nested[I] = static_cast<const void*>(std::addressof(bundle<typename[: nested_member<C, I>() :]>))), ...);
    }(std::make_index_sequence<nested_count<C>()>{});
    return b;
}();

int main()
{
    std::println("bundle<Outer>           = {}", static_cast<const void*>(std::addressof(bundle<Outer>)));
    std::println("bundle<Outer>.nested[1] = {}", bundle<Outer>.nested[1]);
    std::println("bundle<Inner>           = {}", static_cast<const void*>(std::addressof(bundle<Outer::Inner>)));
    std::println("bundle<Inner>.enclosing = {}", bundle<Outer::Inner>.enclosing);
    std::println("bundle<Color>.enclosing = {}", bundle<Outer::Color>.enclosing);
    std::println("S9.3 probe ok");
}
