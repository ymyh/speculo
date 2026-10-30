// S8.6 聚合构造：能否在 consteval 里做 splat 构造（extract + 参数包展开），
//      以及「public 基类在前」的初始化顺序能否用 bases_of 推出来。
#include <meta>

#include <print>
#include <string>
#include <tuple>
#include <utility>

#include "spike_util.hpp"

namespace m = std::meta;

struct B1
{
    int b1;
};
struct B2
{
    int b2;
};

struct Agg : B1, B2
{
    int x;
    double y;
};

struct WithPrivate : B1
{
private:
    int hidden;
};

struct NotAggregate
{
    int a;
    NotAggregate() = default;
    NotAggregate(int v) : a(v) {}
};

struct VirtualBase : virtual B1
{
    int z;
};

// 初始化清单 = public 非虚基类（声明序）+ 本类非静态数据成员（声明序）
template <m::info T>
consteval std::vector<m::info> initializer_types()
{
    std::vector<m::info> tys;
    for (auto b : m::bases_of(T, m::access_context::unchecked()))
    {
        if (m::is_public(b) && !m::is_virtual(b))
        {
            tys.push_back(m::type_of(b));
        }
    }
    for (auto f : m::nonstatic_data_members_of(T, m::access_context::unchecked()))
    {
        tys.push_back(m::type_of(f));
    }
    return tys;
}

template <m::info T>
consteval m::info initializer_tuple_type()
{
    return m::substitute(^^std::tuple, initializer_types<T>());
}

template <m::info T>
struct AggInfo
{
    static constexpr m::info kTuple = initializer_tuple_type<T>();
    using Args = typename[:kTuple:];
    static constexpr std::size_t kN = std::tuple_size_v<Args>;
    static constexpr std::size_t kBases = std::size(m::bases_of(T, m::access_context::unchecked()));
};

// splat 构造 thunk：Args = (B1, B2, int, double) —— 基类槽位放的是**基类对象**，不是它的成员
template <m::info T, std::size_t... I>
consteval auto make_agg_thunk(std::index_sequence<I...>)
{
    using Ty = typename[:T:];
    using Args = typename[:initializer_tuple_type<T>():];
    return +[](const Args& t) -> Ty
    {
        return Ty{std::get<I>(t)...};
    };
}

template <m::info T>
consteval auto aggregate_ctor()
{
    return make_agg_thunk<T>(std::make_index_sequence<AggInfo<T>::kN>{});
}

// ---- 编译期结论 ----
static_assert(m::is_aggregate_type(^^Agg));
// C++20 起「有 private/protected 非静态数据成员」的类不是聚合，所以 is_aggregate 已经挡掉一类
static_assert(!m::is_aggregate_type(^^WithPrivate));
static_assert(!m::is_aggregate_type(^^NotAggregate));
static_assert(AggInfo<^^Agg>::kN == 4); // B1 + B2 + int + double
static_assert(m::has_inaccessible_nonstatic_data_members(^^WithPrivate, m::access_context::unprivileged()));
static_assert(!m::has_inaccessible_nonstatic_data_members(^^Agg, m::access_context::unprivileged()));

consteval std::string dump()
{
    std::string s;
    s += "Agg 的初始化清单（" + spike::to_dec(AggInfo<^^Agg>::kN) + " 项）:\n";
    for (auto t : initializer_types<^^Agg>())
    {
        s += "  [";
        s += m::display_string_of(t);
        s += "]\n";
    }
    s += "substitute -> tuple 类型 = [";
    s += m::display_string_of(AggInfo<^^Agg>::kTuple);
    s += "]\n";
    s += "bases_of(Agg) = " + spike::to_dec(AggInfo<^^Agg>::kBases);
    s += "（含非 public 基；使用时先按 is_public && !is_virtual 过滤）\n";
    s += "WithPrivate 有不可访问成员 = ";
    s += m::has_inaccessible_nonstatic_data_members(^^WithPrivate, m::access_context::unprivileged()) ? "1" : "0";
    s += "  => new_aggregate 必须拒绝\n";

    // consteval 里直接 splat 构造
    constexpr auto ctor = aggregate_ctor<^^Agg>();
    Agg a = ctor(std::tuple<B1, B2, int, double>{B1{1}, B2{2}, 3, 4.5});
    s += "consteval splat: b1=" + spike::to_dec(static_cast<long long>(a.b1)) + " b2=" + spike::to_dec(static_cast<long long>(a.b2)) + " x=" + spike::to_dec(static_cast<long long>(a.x));
    s += " y_ok=";
    s += (a.y == 4.5) ? "1" : "0";
    s += "\n";
    return s;
}

int main()
{
    static constexpr const char* d = std::define_static_string(dump());
    std::print("{}", d);

    // 运行期同一 thunk 复用（Phase 9 new_aggregate 的形态）
    static constexpr auto ctor = aggregate_ctor<^^Agg>();
    Agg v = ctor(std::tuple<B1, B2, int, double>{B1{10}, B2{20}, 30, 40.5});
    std::println("runtime thunk: b1={} b2={} x={} y={}", v.b1, v.b2, v.x, v.y);
    std::println("S8.6 ok");
}
