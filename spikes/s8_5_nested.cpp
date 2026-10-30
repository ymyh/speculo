// S8.5 嵌套类型：members_of 里如何区分类/联合/枚举/别名；parent_of 能否给出外层类；
//      嵌套在**类模板实例**里的类型如何定位。
#include <meta>

#include <print>
#include <string>

#include "spike_util.hpp"

namespace m = std::meta;

struct Outer
{
    struct Inner
    {
        int v;
    };
    class InnerClass
    {
    };
    union U
    {
        int i;
        float f;
    };
    enum class Color
    {
        Red,
        Green
    };
    enum Plain
    {
        A,
        B
    };
    using Alias = int;
    using AliasToNested = Inner;
    struct Deeper
    {
        struct Deepest
        {
        };
    };
    int direct = 0;
    void f();
};

template <class T>
struct Wrapper
{
    struct InTemplate
    {
        T t;
    };
    enum class E
    {
        X
    };
    using A = T;
    T value;
};

// ---- 编译期结论 ----
static_assert(m::is_same_type(m::parent_of(^^Outer::Inner), ^^Outer));
static_assert(m::has_parent(^^Outer::Inner));
static_assert(m::is_same_type(m::parent_of(^^Outer::U), ^^Outer));
static_assert(m::is_same_type(m::parent_of(^^Outer::Color), ^^Outer));
static_assert(m::is_same_type(m::parent_of(^^Outer::Alias), ^^Outer));
static_assert(m::is_same_type(m::parent_of(^^Outer::Deeper::Deepest), ^^Outer::Deeper));

// 类模板实例里的嵌套类型：parent 是实例化本身，且该嵌套类型本身不是模板特化
static_assert(m::is_same_type(m::parent_of(^^Wrapper<int>::InTemplate), ^^Wrapper<int>));
static_assert(!m::has_template_arguments(^^Wrapper<int>::InTemplate));

consteval const char* kind_of(m::info t)
{
    if (m::is_class_type(t))
    {
        return "class";
    }
    if (m::is_union_type(t))
    {
        return "union";
    }
    if (m::is_enum_type(t))
    {
        return "enum";
    }
    if (m::is_type_alias(t))
    {
        return "alias";
    }
    return "other";
}

consteval std::string dump()
{
    std::string s;
    s += "=== Outer 的 type 成员 ===\n";
    for (auto mm : m::members_of(^^Outer, m::access_context::unchecked()))
    {
        if (!m::is_type(mm))
        {
            continue;
        }
        s += "  kind=";
        s += kind_of(mm);
        s += "  dealias=";
        s += kind_of(m::dealias(mm));
        s += "  display=[";
        s += m::display_string_of(mm);
        s += "]  parent=[";
        s += m::display_string_of(m::parent_of(mm));
        s += "]  dealias_of=[";
        s += m::display_string_of(m::dealias(mm));
        s += "]\n";
    }

    s += "=== Wrapper<int> 的 type 成员 ===\n";
    for (auto mm : m::members_of(^^Wrapper<int>, m::access_context::unchecked()))
    {
        if (!m::is_type(mm))
        {
            continue;
        }
        s += "  kind=";
        s += kind_of(mm);
        s += "  display=[";
        s += m::display_string_of(mm);
        s += "]  parent=[";
        s += m::display_string_of(m::parent_of(mm));
        s += "]  parent_has_targs=";
        s += m::has_template_arguments(m::parent_of(mm)) ? "1" : "0";
        s += "  self_has_targs=";
        s += m::has_template_arguments(mm) ? "1" : "0";
        s += "  self_is_alias=";
        s += m::is_type_alias(mm) ? "1" : "0";
        s += "\n";
    }

    s += "=== 从 primary 走：substitute(Wrapper, {double}) 的成员 ===\n";
    constexpr m::info WD = m::substitute(^^Wrapper, {^^double});
    for (auto mm : m::members_of(WD, m::access_context::unchecked()))
    {
        if (!m::is_type(mm))
        {
            continue;
        }
        s += "  [";
        s += m::display_string_of(mm);
        s += "] is_same_as_source=";
        s += m::is_same_type(mm, ^^Wrapper<double>::InTemplate) ? "1" : "0";
        s += "\n";
    }

    s += "=== 嵌套类型的完整名 ===\n";
    s += "  identifier_of(Inner)=[";
    s += m::identifier_of(^^Outer::Inner);
    s += "]  parent identifier=[";
    s += m::identifier_of(m::parent_of(^^Outer::Inner));
    s += "]  => Class::get_name 需自行拼接「外层::内层」\n";
    return s;
}

int main()
{
    static constexpr const char* d = std::define_static_string(dump());
    std::print("{}", d);
    std::println("S8.5 ok");
}
