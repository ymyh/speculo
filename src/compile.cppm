// speculo:compile —— 编译期半层（api_design §10）。
//
// 纯 constexpr / consteval：不持有运行期状态、不物化描述表、不注册。只 import
// std 与 :meta；成员收集规则由 :meta 的单一收集器提供，与运行期 get_fields() /
// get_methods() 完全一致。
module;

#include <meta>

export module speculo:compile;

import std;
import :meta;

export namespace speculo
{

/**
 * @brief One field in a compile-time field table (api_design §10.3)
 *
 * `offset` is 0 for static data members; `size` / `alignment` describe the
 * declared field type (`std::remove_cvref_t` of the member type), matching
 * `Field::get_size()` / `Field::get_alignment()` on the runtime side.
 */
struct field_info
{
    std::string_view name;
    std::size_t      offset;
    std::size_t      size;
    std::size_t      alignment;
    bool             is_bitfield;
    bool             is_static;
};

/// Category reported by for_each_member (api_design §10.3)
enum class MemberKind
{
    Field,       ///< non-static data member
    StaticField, ///< static data member
    Method,      ///< member function (including conversion operators)
    Destructor,  ///< user-declared destructor
};

namespace detail
{

namespace meta = std::meta;

template<meta::info M>
consteval auto make_field_info() -> field_info
{
    using FieldType = std::remove_cvref_t<typename[: meta::type_of(M) :]>;

    field_info info{};
    info.name = std::define_static_string(member_name(M));
    info.size = sizeof(FieldType);
    info.alignment = alignof(FieldType);
    info.is_bitfield = meta::is_bit_field(M);
    info.is_static = meta::is_static_member(M);
    if constexpr (!meta::is_static_member(M))
    {
        info.offset = static_cast<std::size_t>(meta::offset_of(M).bytes);
    }
    return info;
}

template<meta::info C>
consteval auto make_field_info_table() -> std::array<field_info, public_field_count<C>()>
{
    return [&]<std::size_t... I>(std::index_sequence<I...>) -> std::array<field_info, sizeof...(I)>
    {
        return { make_field_info<public_field_member<C, I>()>()... };
    }(std::make_index_sequence<public_field_count<C>()>{});
}

template<typename T>
consteval auto make_field_table() -> std::array<field_info, collected_field_count<T>>
{
    return make_field_info_table<info_of<std::remove_cvref_t<T>>()>();
}

} // namespace detail

/// Number of public data members of T, as reported by Class::get_fields().
template<typename T>
consteval auto field_count() -> std::size_t
{
    return detail::collected_field_count<std::remove_cvref_t<T>>;
}

/// Number of public members of T (fields + methods, including static members).
template<typename T>
consteval auto member_count() -> std::size_t
{
    return detail::collected_field_count<std::remove_cvref_t<T>>
         + detail::collected_method_count<std::remove_cvref_t<T>>;
}

/**
 * @brief Visits every public data member of obj, in Class::get_fields() order
 *
 * Order: non-static members (declaration order), then static members
 * (declaration order) -- exactly the order of the runtime field table. Base
 * classes are not expanded (api_design §10.4).
 *
 * The callback is invoked as `f(name, value)`. Normal members are passed as
 * lvalues, so a callback taking `T&` can write them; bit-fields are passed as
 * the read-out value (no reference exists). Any callback shape that accepts
 * both -- `const auto&`, `auto&&`, `auto` -- works for every field.
 *
 * @param obj The object to visit (const objects yield const field views)
 * @param f Callback with signature `f(std::string_view, value)`
 */
template<typename T, typename F>
constexpr void for_each_field(T& obj, F&& f)
{
    static constexpr auto members = detail::collect_fields<std::remove_cvref_t<T>>();
    template for (constexpr auto member : members)
    {
        constexpr std::string_view name = std::define_static_string(detail::member_name(member));
        if constexpr (std::meta::is_bit_field(member))
        {
            using FieldType = std::remove_cv_t<typename[: std::meta::type_of(member) :]>;
            std::invoke(f, name, static_cast<FieldType>(obj.[:member:]));
        }
        else
        {
            std::invoke(f, name, obj.[:member:]);
        }
    }
}

/**
 * @brief Visits every public member of T: fields first, then methods
 *
 * Only the name and the category are exposed; the reflection handle itself never
 * leaves the module. The order matches `get_fields()` followed by
 * `get_methods()` (api_design §10.4).
 *
 * @tparam T The type to visit (cv / reference qualifiers are normalized)
 * @param f Callback with signature `f(std::string_view, MemberKind)`
 */
template<typename T, typename F>
constexpr void for_each_member(F&& f)
{
    using Visited = std::remove_cvref_t<T>;

    static constexpr auto fields = detail::collect_fields<Visited>();
    template for (constexpr auto member : fields)
    {
        constexpr std::string_view name = std::define_static_string(detail::member_name(member));
        std::invoke(f, name, std::meta::is_static_member(member) ? MemberKind::StaticField : MemberKind::Field);
    }

    static constexpr auto methods = detail::collect_methods<Visited>();
    template for (constexpr auto member : methods)
    {
        constexpr std::string_view name = std::define_static_string(detail::member_name(member));
        std::invoke(f, name, std::meta::is_destructor(member) ? MemberKind::Destructor : MemberKind::Method);
    }
}

/**
 * @brief Compile-time field table of T (api_design §10.3)
 *
 * Usable as a constant offset / layout index:
 *
 *     constexpr auto offsets = speculo::field_table<Point>;
 *     static_assert(offsets[0].offset == 0);
 *
 * Empty for non-class types and for standard-library types (which are
 * materialized shallowly).
 */
template<typename T>
inline constexpr std::array<field_info, field_count<T>()> field_table = detail::make_field_table<T>();

} // namespace speculo
