// S5 —— 枚举：enumerators_of / underlying_type / 枚举项名字与底层值。
//
// 结论：GCC 16.2 提供 enumerators_of、underlying_type、is_scoped_enum_type。
// 取枚举项数值要用 extract<枚举类型>(项)，再转底层类型；
// extract<底层类型>(项) 与 extract<底层类型>(constant_of(项)) 都会抛
// "value cannot be extracted"。
#include <meta>

#include <cstddef>
#include <print>
#include <string>

#include "spike_util.hpp"

namespace m = std::meta;

enum Plain
{
    A,
    B = 5,
    C
};

enum class Scoped : unsigned char
{
    X = 1,
    Y = 200
};

template <m::info E>
consteval std::string dump_enumerators()
{
    using V = typename[: E :];
    using U = typename[: m::underlying_type(E) :];
    std::string out;
    out += "underlying=" + std::string(m::display_string_of(m::underlying_type(E))) +
           " is_scoped=" + (m::is_scoped_enum_type(E) ? "true" : "false") +
           " size=" + spike::to_dec(m::size_of(E)) + "\n";
    for (auto e : m::enumerators_of(E))
    {
        const U v = static_cast<U>(m::extract<V>(e));
        out += "  " + std::string(m::identifier_of(e)) +
               " = " + spike::to_dec(static_cast<long long>(v)) +
               "  is_enumerator=" + (m::is_enumerator(e) ? "true" : "false") +
               "  parent=" + std::string(m::display_string_of(m::parent_of(e))) + "\n";
    }
    return out;
}

int main()
{
    static_assert(m::is_enum_type(^^Plain));
    static_assert(!m::is_scoped_enum_type(^^Plain));
    static_assert(m::is_enum_type(^^Scoped));
    static_assert(m::is_scoped_enum_type(^^Scoped));
    static_assert(m::is_enumerable_type(^^Plain));
    static_assert(m::is_same_type(m::underlying_type(^^Plain), ^^unsigned int));
    static_assert(m::is_same_type(m::underlying_type(^^Scoped), ^^unsigned char));

    static_assert(m::enumerators_of(^^Plain).size() == 3);
    static_assert(m::enumerators_of(^^Scoped).size() == 2);

    // 名字 <-> 值
    static_assert(m::identifier_of(m::enumerators_of(^^Plain)[1]) == "B");
    static_assert(m::extract<Plain>(m::enumerators_of(^^Plain)[1]) == Plain::B);
    static_assert(static_cast<int>(m::extract<Plain>(m::enumerators_of(^^Plain)[2])) == 6);
    static_assert(m::extract<Scoped>(m::enumerators_of(^^Scoped)[1]) == Scoped::Y);

    static constexpr const char* d1 = std::define_static_string(dump_enumerators<^^Plain>());
    static constexpr const char* d2 = std::define_static_string(dump_enumerators<^^Scoped>());
    std::println("Plain:\n{}", d1);
    std::println("Scoped:\n{}", d2);
    std::println("S5 ok");
}
