// S8 —— 物化形态验证：能否把反射结果物化成「constexpr 静态描述表」，
//       且表中保存 std::string_view 名字（需 std::define_static_string 提升到静态存储）。
//       这是 Phase 3 的核心，也是 "constexpr std::string 不能做变量 / std::to_string
//       不是 constexpr" 两个坑的正面解法。
#include <meta>

#include <array>
#include <cstddef>
#include <print>
#include <string_view>
#include <utility>

namespace m = std::meta;

struct Point
{
    int x = 0;
    double y = 0;
};

struct FieldDesc
{
    std::string_view name;
    std::string_view type_name;
    std::size_t offset;
    std::size_t size;
    bool is_bitfield;
};

template <m::info R, std::size_t I>
consteval FieldDesc make_field_desc()
{
    constexpr m::info M = m::nonstatic_data_members_of(R, m::access_context::unchecked())[I];
    return FieldDesc{
        .name = std::define_static_string(m::identifier_of(M)),
        .type_name = std::define_static_string(m::display_string_of(m::type_of(M))),
        .offset = static_cast<std::size_t>(m::offset_of(M).bytes),
        .size = m::size_of(m::type_of(M)),
        .is_bitfield = m::is_bit_field(M),
    };
}

template <m::info R, std::size_t... I>
consteval std::array<FieldDesc, sizeof...(I)> make_table_impl(std::index_sequence<I...>)
{
    return std::array<FieldDesc, sizeof...(I)>{make_field_desc<R, I>()...};
}

template <m::info R>
consteval auto make_table()
{
    constexpr std::size_t n = m::nonstatic_data_members_of(R, m::access_context::unchecked()).size();
    return make_table_impl<R>(std::make_index_sequence<n>{});
}

// 静态描述表：编译期生成，运行期只读
static constexpr auto k_point_fields = make_table<^^Point>();

int main()
{
    static_assert(k_point_fields.size() == 2);
    static_assert(k_point_fields[0].name == "x");
    static_assert(k_point_fields[0].offset == 0);
    static_assert(k_point_fields[1].name == "y");
    static_assert(k_point_fields[1].offset == 8);
    static_assert(k_point_fields[0].type_name == "int");
    static_assert(k_point_fields[1].type_name == "double");

    for (const auto& f : k_point_fields)
    {
        std::println("  {} : {} @ {} (size={})", f.name, f.type_name, f.offset, f.size);
    }
    std::println("S8 ok: constexpr 静态描述表可用（string_view 经 define_static_string 提升）");
}
