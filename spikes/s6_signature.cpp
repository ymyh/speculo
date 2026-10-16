// S6 —— 函数签名：parameters_of / return_type_of / cv-ref-noexcept 提取，
//       以及 is_explicit / is_deleted / is_defaulted / is_override 等谓词。
//       另外验证 substitute 把"一串形参 info"变回 std::tuple 类型。
#include <meta>

#include <cstddef>
#include <print>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "spike_util.hpp"

namespace m = std::meta;

struct Base
{
    virtual void f(int) {}
    virtual int g(double, char) const = 0;
    virtual ~Base() = default;
};

struct Fn : Base
{
    Fn() = default;
    explicit Fn(int) {}
    Fn(const Fn&) = delete;
    Fn(Fn&&) noexcept = default;

    int m(int a, double b) const& noexcept { return a; }
    static void s(double) {}
    void f(int) override {}
    int g(double, char) const override { return 0; }
    void del() = delete;
    int variadic(int, ...) { return 0; }
    int defarg(int a, int b = 3) { return a + b; }
    operator int() const { return 0; }
};

consteval m::info find_named(m::info R, std::string_view nm, int arg_count = -1)
{
    for (auto mm : m::members_of(R, m::access_context::unchecked()))
    {
        if (m::is_function(mm) && m::has_identifier(mm) && m::identifier_of(mm) == nm)
        {
            if (arg_count < 0 || static_cast<int>(m::parameters_of(mm).size()) == arg_count)
            {
                return mm;
            }
        }
    }
    return m::info{};
}

consteval m::info find_ctor_param(m::info R, m::info T)
{
    for (auto mm : m::members_of(R, m::access_context::unchecked()))
    {
        if (m::is_constructor(mm) && m::parameters_of(mm).size() == 1 &&
            m::is_same_type(m::type_of(m::parameters_of(mm)[0]), T))
        {
            return mm;
        }
    }
    return m::info{};
}

consteval m::info find_default_ctor(m::info R)
{
    for (auto mm : m::members_of(R, m::access_context::unchecked()))
    {
        if (m::is_constructor(mm) && m::parameters_of(mm).size() == 0)
        {
            return mm;
        }
    }
    return m::info{};
}

consteval m::info find_copy_ctor(m::info R)
{
    for (auto mm : m::members_of(R, m::access_context::unchecked()))
    {
        if (m::is_constructor(mm) && m::is_copy_constructor(mm))
        {
            return mm;
        }
    }
    return m::info{};
}

consteval m::info find_conversion(m::info R)
{
    for (auto mm : m::members_of(R, m::access_context::unchecked()))
    {
        if (m::is_conversion_function(mm))
        {
            return mm;
        }
    }
    return m::info{};
}

// 把形参类型 info 串成 tuple 类型
consteval std::vector<m::info> param_type_infos(m::info fn)
{
    std::vector<m::info> out;
    for (auto p : m::parameters_of(fn))
    {
        out.push_back(m::type_of(p));
    }
    return out;
}

template <m::info M>
using ParamsTuple = typename[: m::substitute(^^std::tuple, param_type_infos(M)) :];

consteval std::string dump_fn(m::info fn)
{
    std::string out;
    out += "  " + std::string(m::display_string_of(fn));
    out += "  [";
    if (!m::is_constructor(fn) && !m::is_destructor(fn))
    {
        out += "ret=" + std::string(m::display_string_of(m::return_type_of(fn)));
    }
    out += " params=" + spike::to_dec(m::parameters_of(fn).size());
    if (m::is_static_member(fn))
    {
        out += " static";
    }
    if (m::is_const(fn))
    {
        out += " const";
    }
    if (m::is_noexcept(fn))
    {
        out += " noexcept";
    }
    if (m::is_lvalue_reference_qualified(fn))
    {
        out += " &";
    }
    if (m::is_rvalue_reference_qualified(fn))
    {
        out += " &&";
    }
    if (m::is_virtual(fn))
    {
        out += " virtual";
    }
    if (m::is_pure_virtual(fn))
    {
        out += " pure";
    }
    if (m::is_override(fn))
    {
        out += " override";
    }
    if (m::is_deleted(fn))
    {
        out += " deleted";
    }
    if (m::is_defaulted(fn))
    {
        out += " defaulted";
    }
    if (m::is_explicit(fn))
    {
        out += " explicit";
    }
    if (m::is_vararg_function(fn))
    {
        out += " vararg";
    }
    if (m::is_conversion_function(fn))
    {
        out += " conversion";
    }
    out += "]\n";
    for (auto p : m::parameters_of(fn))
    {
        out += "      param '" +
               (m::has_identifier(p) ? std::string(m::identifier_of(p)) : std::string("?")) +
               "' : " + std::string(m::display_string_of(m::type_of(p)));
        if (m::has_default_argument(p))
        {
            out += " (has-default)";
        }
        out += "\n";
    }
    return out;
}

consteval std::string dump_all()
{
    std::string out;
    for (auto mm : m::members_of(^^Fn, m::access_context::unchecked()))
    {
        if (m::is_function(mm))
        {
            out += dump_fn(mm);
        }
    }
    return out;
}

int main()
{
    // 返回类型与形参
    static_assert(m::is_same_type(m::return_type_of(find_named(^^Fn, "m", 2)), ^^int));
    static_assert(m::parameters_of(find_named(^^Fn, "m", 2)).size() == 2);
    static_assert(m::is_same_type(m::type_of(m::parameters_of(find_named(^^Fn, "m", 2))[0]), ^^int));
    static_assert(m::is_same_type(m::type_of(m::parameters_of(find_named(^^Fn, "m", 2))[1]), ^^double));

    // cv / ref / noexcept
    static_assert(m::is_const(find_named(^^Fn, "m", 2)));
    static_assert(m::is_noexcept(find_named(^^Fn, "m", 2)));
    static_assert(m::is_lvalue_reference_qualified(find_named(^^Fn, "m", 2)));

    // static
    static_assert(m::is_static_member(find_named(^^Fn, "s", 1)));

    // virtual / override / pure
    static_assert(m::is_virtual(find_named(^^Fn, "g", 2)));
    static_assert(m::is_override(find_named(^^Fn, "g", 2)));
    static_assert(m::is_virtual(find_named(^^Base, "g", 2)));
    static_assert(m::is_pure_virtual(find_named(^^Base, "g", 2)));

    // explicit / deleted / defaulted
    static_assert(m::is_explicit(find_ctor_param(^^Fn, ^^int)));
    static_assert(m::is_deleted(find_copy_ctor(^^Fn)));
    static_assert(m::is_defaulted(find_default_ctor(^^Fn)));

    // 变参 / 默认实参
    static_assert(m::is_vararg_function(find_named(^^Fn, "variadic", 1)));
    static_assert(m::has_default_argument(m::parameters_of(find_named(^^Fn, "defarg", 2))[1]));

    // 转换函数
    static_assert(m::is_conversion_function(find_conversion(^^Fn)));
    static_assert(m::is_same_type(m::return_type_of(find_conversion(^^Fn)), ^^int));

    // substitute：形参 -> std::tuple
    static_assert(std::is_same_v<ParamsTuple<find_named(^^Fn, "m", 2)>, std::tuple<int, double>>);
    static_assert(std::is_same_v<ParamsTuple<find_named(^^Fn, "defarg", 2)>, std::tuple<int, int>>);

    static constexpr const char* d = std::define_static_string(dump_all());
    std::println("{}", d);
    std::println("S6 ok");
}
