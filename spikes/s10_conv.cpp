// S10 probe: the return type of a conversion operator.
#include <meta>

#include <cstddef>
#include <print>
#include <string>

namespace m = std::meta;

struct Conv
{
    int n = 0;
    operator int() const { return n; }
    explicit operator std::string() const { return "x"; }
};

template<m::info C, std::size_t I>
consteval auto member_at() -> m::info
{
    return m::members_of(C, m::access_context::unchecked())[I];
}

template<m::info C>
consteval auto member_count() -> std::size_t
{
    return m::members_of(C, m::access_context::unchecked()).size();
}

int main()
{
    constexpr std::size_t count = member_count<^^Conv>();
    [&]<std::size_t... I>(std::index_sequence<I...>)
    {
        (([&]()
        {
            constexpr m::info M = member_at<^^Conv, I>();
            if constexpr (m::is_conversion_function(M))
            {
                constexpr const char* display = std::define_static_string(m::display_string_of(M));
                constexpr const char* ret = std::define_static_string(m::display_string_of(m::return_type_of(M)));
                std::println("conversion display=[{}] return=[{}]", display, ret);
            }
        }()), ...);
    }(std::make_index_sequence<count>{});

    return 0;
}
