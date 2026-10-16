// Isolate extract / display_string_of for a string-literal annotation.
#include <meta>

#include <print>
#include <string>
#include <string_view>

namespace m = std::meta;

struct Holder
{
    [[= "literal"]]
    int decayed;
};

consteval auto shown() -> std::string_view
{
    return std::define_static_string(std::string(m::display_string_of(m::annotations_of(^^Holder::decayed)[0])));
}

int main()
{
    std::println("display: {}", shown());
}
