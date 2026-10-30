// Phase 9 实现前的最小探针：
//   1) 以 meta::info 为键的 constexpr 变量模板（主模板的声明级描述）能否自引用，
//      并能否从「以类型为键」的 bundle 里用依赖拼写引用到同一份实例；
//   2) NTTP 的装箱：constant_of + extract<T>（含类类型 NTTP）能否成功；
//   3) 函数模板特化的 template_arguments_of / template_of；
//   4) meta::offset_of 对「基类」反射是否可用（决定 BaseClass::get_offset 的实现方式）。
#include <meta>

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <string>
#include <type_traits>
#include <vector>

#include "spike_util.hpp"

namespace m = std::meta;

// ── 1) meta::info 键的变量模板，自引用 + 被类型键的 bundle 依赖引用 ──

template<m::info K>
struct TplInfo
{
    const char* name = nullptr;
    const void* self = nullptr; // 自引用（相当于 TypeBundle 里的 cls）
};

template<m::info K>
inline constexpr TplInfo<K> tpl_info = []() consteval -> TplInfo<K>
{
    TplInfo<K> t{};
    t.name = std::define_static_string(m::identifier_of(K));
    t.self = std::addressof(tpl_info<K>); // 变量模板自引用
    return t;
}();

template<class T>
struct TypeInfo
{
    const void* self = nullptr;
    const void* primary = nullptr;  // 指向 meta::info 键的实例
    std::size_t primary_args = 0;
};

template<class T>
inline constexpr TypeInfo<T> type_info = []() consteval -> TypeInfo<T>
{
    constexpr m::info C = ^^T;
    TypeInfo<T> t{};
    t.self = std::addressof(type_info<T>);
    if constexpr (m::has_template_arguments(C))
    {
        // 依赖拼写（C 是模板参数上下文里的 constexpr 值）
        t.primary = std::addressof(tpl_info<m::template_of(C)>);
        t.primary_args = m::template_arguments_of(C).size();
    }
    return t;
}();

// ── 2) NTTP 装箱 ──

struct Config
{
    int a;
    int b;
    constexpr bool operator==(const Config&) const = default;
};

template<class T, int N, Config C>
struct Holder
{
};

// ── 3) 函数模板特化 ──

namespace probe
{
template<class T>
T id(T v)
{
    return v;
}
} // namespace probe

// ── 4) 基类偏移 ──

struct B1
{
    int b1;
};
struct B2
{
    int b2;
};
struct E : B1, B2
{
    int e;
};
struct Empty
{
};
struct WithEmpty : Empty, B1
{
    int v;
};

consteval bool offset_of_base_works()
{
    // GCC 16.2: offset_of 的实参必须是成员？
    return m::offset_of(m::bases_of(^^E, m::access_context::unchecked())[1]).bytes == 4;
}

template<m::info C, std::size_t I>
consteval auto targ() -> m::info
{
    return m::template_arguments_of(C)[I];
}

template<m::info A>
consteval bool value_roundtrips()
{
    using V = typename[: m::type_of(A) :];
    constexpr V value = m::extract<V>(m::constant_of(A));
    return value == V{ value };
}

consteval std::string dump()
{
    std::string s;

    s += "=== 1) info-keyed 变量模板 ===\n";
    s += "tpl_info<vector>.name = [";
    s += tpl_info<m::template_of(^^std::vector<int>)>.name;
    s += "]\n";
    s += "type_info<vector<int>>.primary == type_info<vector<double>>.primary : ";
    s += type_info<std::vector<int>>.primary == type_info<std::vector<double>>.primary ? "1" : "0";
    s += "\n  args(v<int>)=" + spike::to_dec(type_info<std::vector<int>>.primary_args);
    s += "  args(v<double>)=" + spike::to_dec(type_info<std::vector<double>>.primary_args);
    s += "\n  self-stable across specializations: ";
    const void* vector_int_primary = tpl_info<m::template_of(^^std::vector<int>)>.self;
    const void* vector_double_primary = tpl_info<m::template_of(^^std::vector<double>)>.self;
    s += vector_int_primary == vector_double_primary ? "1" : "0";
    s += "\n";

    s += "=== 2) NTTP 装箱 ===\n";
    constexpr m::info H = ^^Holder<int, 3, Config{ 7, 9 }>;
    s += "Holder<int,3,Config{7,9}> 实参数 = " + spike::to_dec(m::template_arguments_of(H).size()) + "\n";
    s += "  [0] is_type=";
    s += m::is_type(targ<H, 0>()) ? "1" : "0";
    s += "  [1] is_value=";
    s += m::is_value(targ<H, 1>()) ? "1" : "0";
    s += " type=[";
    s += m::display_string_of(m::type_of(targ<H, 1>()));
    s += "] value=[";
    s += m::display_string_of(targ<H, 1>());
    s += "] roundtrip=";
    s += value_roundtrips<targ<H, 1>()>() ? "1" : "0";
    s += "\n  [2] is_value=";
    s += m::is_value(targ<H, 2>()) ? "1" : "0";
    s += " type=[";
    s += m::display_string_of(m::type_of(targ<H, 2>()));
    s += "] value=[";
    s += m::display_string_of(targ<H, 2>());
    s += "] roundtrip=";
    s += value_roundtrips<targ<H, 2>()>() ? "1" : "0";
    s += "\n";

    s += "=== 3) 函数模板特化 ===\n";
    constexpr m::info F = ^^probe::id<int>;
    s += "is_function=";
    s += m::is_function(F) ? "1" : "0";
    s += " has_template_arguments=";
    s += m::has_template_arguments(F) ? "1" : "0";
    s += " template_of_ident=[";
    s += m::identifier_of(m::template_of(F));
    s += "] args=";
    if constexpr (m::has_template_arguments(F))
    {
        s += spike::to_dec(m::template_arguments_of(F).size());
        s += " [0]=[";
        s += m::display_string_of(targ<F, 0>());
        s += "]";
    }
    s += "\n  display=[";
    s += m::display_string_of(F);
    s += "]\n";

    s += "=== 4) offset_of(基类) ===\n";
    s += "offset_of(bases_of(E)[1]).bytes = "
       + spike::to_dec(static_cast<long long>(m::offset_of(m::bases_of(^^E, m::access_context::unchecked())[1]).bytes));
    s += "  (期望 4)\n";
    s += "offset_of(bases_of(WithEmpty)[1]).bytes = "
       + spike::to_dec(static_cast<long long>(
           m::offset_of(m::bases_of(^^WithEmpty, m::access_context::unchecked())[1]).bytes));
    s += "\n";
    s += "=== 5) cv 类型上的模板实参 ===\n";
    s += "has_template_arguments(const vector<int>) = ";
    s += m::has_template_arguments(^^const std::vector<int>) ? "1" : "0";
    s += "  is_const_type = ";
    s += m::is_const_type(^^const std::vector<int>) ? "1" : "0";
    s += "\n";
    return s;
}

// 4') 若 offset_of 不接受基类，把下面这行注释掉即可看到编译错误
static_assert(offset_of_base_works(), "offset_of(base) does not work in GCC 16.2");

// 另测：指针没有模板实参
static_assert(!m::has_template_arguments(^^std::vector<int>*));

int main()
{
    static constexpr const char* d = std::define_static_string(dump());
    std::print("{}", d);
    std::println("S9.1 probe ok");
}
