// Phase 9 探针 2：补齐剩余不确定点
//   1) 类类型 NTTP 的反射属于哪一类（is_value / is_object），constant_of 能否用
//   2) 虚基的 offset_of 是否可求（探针 1 已证明非虚基可用）
//   3) cv 限定特化的 template_arguments_of
//   4) 聚合初始化：substitute(^^std::tuple, 槽位类型表) + 运行期 splat thunk
//   5) 嵌套类型成员的分类（别名 vs 类/联合/枚举）与 parent_of
#include <meta>

#include <array>
#include <cstddef>
#include <print>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "spike_util.hpp"

namespace m = std::meta;

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

template<m::info C, std::size_t I>
consteval auto targ() -> m::info
{
    return m::template_arguments_of(C)[I];
}

// ── 2) 虚基偏移 ──
struct VA
{
    virtual ~VA() = default;
    int a;
};
struct VB : virtual VA
{
    int b;
};

consteval auto virtual_base_offset() -> std::size_t
{
    return static_cast<std::size_t>(m::offset_of(m::bases_of(^^VB, m::access_context::unchecked())[0]).bytes);
}

// ── 4) 聚合 splat ──
struct AggB1
{
    int b1;
};
struct AggB2
{
    int b2;
};
struct Agg : AggB1, AggB2
{
    int x;
    double y;
};

template<m::info T>
consteval auto slot_types() -> std::vector<m::info>
{
    std::vector<m::info> slots;
    for (auto b : m::bases_of(T, m::access_context::unchecked()))
    {
        if (m::is_public(b) && !m::is_virtual(b))
        {
            slots.push_back(m::type_of(b));
        }
    }
    for (auto f : m::nonstatic_data_members_of(T, m::access_context::unchecked()))
    {
        slots.push_back(m::type_of(f));
    }
    return slots;
}

template<m::info T>
consteval auto slot_tuple() -> m::info
{
    return m::substitute(^^std::tuple, slot_types<T>());
}

template<m::info T>
consteval auto slot_count() -> std::size_t
{
    return slot_types<T>().size();
}

template<std::size_t I, class Args>
auto slot_ref(const void* address) -> std::tuple_element_t<I, Args>&
{
    using S = std::tuple_element_t<I, Args>;
    return *static_cast<S*>(const_cast<void*>(address));
}

// 运行期 splat：参数指针按槽位类型解释，全部以左值传入（基类槽位走拷贝初始化）
template<m::info T, std::size_t... I>
consteval auto make_aggregate_new(std::index_sequence<I...>) -> void* (*)(const void* const*)
{
    using Self = typename[:T:];
    using Slots = typename[:slot_tuple<T>():];
    return +[](const void* const* args) -> void*
    {
        return new Self(slot_ref<I, Slots>(args[I])...);
    };
}

// ── 5) 嵌套类型 ──
struct Outer
{
    struct Inner
    {
        int v;
    };
    union U
    {
        int i;
        float f;
    };
    enum class Color
    {
        Red
    };
    using Alias = int;
    using AliasToNested = Inner;
};

consteval std::string dump()
{
    std::string s;

    s += "=== 1) 类类型 NTTP 的反射种类 ===\n";
    constexpr m::info H = ^^Holder<int, 3, Config{ 7, 9 }>;
    s += "  [2] is_value=";
    s += m::is_value(targ<H, 2>()) ? "1" : "0";
    s += " is_object=";
    s += m::is_object(targ<H, 2>()) ? "1" : "0";
    s += " is_type=";
    s += m::is_type(targ<H, 2>()) ? "1" : "0";
    s += " is_template=";
    s += m::is_template(targ<H, 2>()) ? "1" : "0";
    s += " type_of=[";
    s += m::display_string_of(m::type_of(targ<H, 2>()));
    s += "] constant_of_ok=";
    s += []() consteval
    {
        using V = typename[: m::type_of(targ<H, 2>()) :];
        constexpr V v = m::extract<V>(m::constant_of(targ<H, 2>()));
        return v.a == 7;
    }() ? "1" : "0";
    s += "\n";

    s += "=== 2) 虚基偏移 ===\n";
    s += "offset_of(bases_of(VB)[0]).bytes = " + spike::to_dec(static_cast<long long>(virtual_base_offset()));
    s += "\n";

    s += "=== 3) cv 特化的实参 ===\n";
    s += "template_arguments_of(const vector<int>).size() = "
       + spike::to_dec(m::template_arguments_of(^^const std::vector<int>).size());
    s += "\n  template_of(const vector<int>) has_template_arguments = ";
    s += m::has_template_arguments(m::template_of(^^const std::vector<int>)) ? "1" : "0";
    s += "\n";

    s += "=== 4) 聚合槽位 ===\n";
    s += "Agg 槽位 = " + spike::to_dec(slot_count<^^Agg>()) + "  tuple=[";
    s += m::display_string_of(slot_tuple<^^Agg>());
    s += "]\n";

    s += "=== 5) 嵌套类型成员分类 ===\n";
    for (auto member : m::members_of(^^Outer, m::access_context::unchecked()))
    {
        if (!m::is_type(member))
        {
            continue;
        }
        s += "  alias=";
        s += m::is_type_alias(member) ? "1" : "0";
        s += " class=";
        s += m::is_class_type(m::dealias(member)) ? "1" : "0";
        s += " union=";
        s += m::is_union_type(m::dealias(member)) ? "1" : "0";
        s += " enum=";
        s += m::is_enum_type(m::dealias(member)) ? "1" : "0";
        s += " parent_is_class=";
        s += m::is_class_type(m::parent_of(member)) ? "1" : "0";
        s += " display=[";
        s += m::display_string_of(member);
        s += "]\n";
    }

    return s;
}

int main()
{
    static constexpr const char* d = std::define_static_string(dump());
    std::print("{}", d);

    // 运行期 splat thunk 复用
    static constexpr auto make_agg = make_aggregate_new<^^Agg>(std::make_index_sequence<slot_count<^^Agg>()>{});
    AggB1 b1{ 10 };
    AggB2 b2{ 20 };
    int x = 30;
    double y = 40.5;
    const void* args[]{ &b1, &b2, &x, &y };
    Agg* value = static_cast<Agg*>(make_agg(args));
    std::println("runtime aggregate: b1={} b2={} x={} y={}", value->b1, value->b2, value->x, value->y);
    delete value;

    std::println("S9.2 probe ok");
}
