// S11 探针（模块侧）：导出带 template for / splice 的函数模板，
// 由用户 TU 用本地类型实例化。
module;

#include <array>
#include <meta>
#include <string_view>

export module s11;

namespace s11::detail
{

// 非导出的内部收集器：导出模板在用户 TU 实例化时能否调用它？
template<std::meta::info C>
consteval auto collect_public_fields() -> std::array<std::meta::info, 2>
{
    const auto nonstatic = std::meta::nonstatic_data_members_of(C, std::meta::access_context::current());
    return { nonstatic[0], nonstatic[1] };
}

} // namespace s11::detail

export namespace s11
{

template<typename T>
consteval std::size_t count_first_two()
{
    return s11::detail::collect_public_fields<^^T>().size();
}


template<typename T, typename F>
constexpr void each_field(T& obj, F&& f)
{
    template for (constexpr auto member : std::define_static_array(
                      std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current())))
    {
        if constexpr (std::meta::is_bit_field(member))
        {
            using FieldType = typename[: std::meta::type_of(member) :];
            f(std::string_view(std::meta::identifier_of(member)), static_cast<FieldType>(obj.[:member:]));
        }
        else
        {
            f(std::string_view(std::meta::identifier_of(member)), obj.[:member:]);
        }
    }
}

template<typename T, typename F>
constexpr void each_field_with_static(T& obj, F&& f)
{
    template for (constexpr auto member : std::define_static_array(
                      std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current())))
    {
        if constexpr (std::meta::is_bit_field(member))
        {
            using FieldType = typename[: std::meta::type_of(member) :];
            f(std::string_view(std::meta::identifier_of(member)), static_cast<FieldType>(obj.[:member:]));
        }
        else
        {
            f(std::string_view(std::meta::identifier_of(member)), obj.[:member:]);
        }
    }
    template for (constexpr auto member : std::define_static_array(
                      std::meta::static_data_members_of(^^T, std::meta::access_context::current())))
    {
        f(std::string_view(std::meta::identifier_of(member)), obj.[:member:]);
    }
}

template<typename T>
consteval std::size_t count_public_fields()
{
    return std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()).size()
         + std::meta::static_data_members_of(^^T, std::meta::access_context::current()).size();
}

} // namespace s11
