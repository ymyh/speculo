#include <cstddef>
#include <string_view>
#include <print>

namespace demo
{
template<std::size_t N>
struct FixedString
{
    char data[N]{};
    constexpr FixedString(const char (&s)[N])
    {
        for (std::size_t i = 0; i < N; ++i) { data[i] = s[i]; }
    }
    [[nodiscard]] constexpr operator std::string_view() const { return std::string_view(data, N - 1); }
};

template<std::size_t N>
FixedString(const char (&)[N]) -> FixedString<N>;

template<FixedString Name, typename... Params>
std::string_view name_of() { return Name; }
} // namespace demo

int main()
{
    std::println("name=[{}]", demo::name_of<"scale", int>());
    return 0;
}
