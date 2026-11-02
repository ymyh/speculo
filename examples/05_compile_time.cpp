// Compile-time half demo: one traversal generates serialization, hashing and a
// column (SoA) view with zero runtime reflection lookups.
#include <cstring>

import std;
import speculo;

namespace
{

struct Point
{
    int x;
    int y;
    int z;
};

template<typename T>
auto to_json(const T& obj) -> std::string
{
    std::string out = "{";
    bool first = true;
    speculo::for_each_field(obj, [&](std::string_view name, const auto& value)
    {
        out += first ? "" : ",";
        first = false;
        out += std::format("\"{}\":{}", name, value);
    });
    out += "}";
    return out;
}

template<typename T>
auto hash_fields(const T& obj) -> std::size_t
{
    std::size_t seed = 0;
    speculo::for_each_field(obj, [&](std::string_view, const auto& value)
    {
        const std::size_t piece = std::hash<std::string>{}(std::format("{}", value));
        seed ^= piece + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    });
    return seed;
}

auto as_column(const Point& point) -> std::array<int, speculo::field_count<Point>()>
{
    constexpr auto offsets = speculo::field_table<Point>;
    const auto* bytes = reinterpret_cast<const char*>(&point);
    std::array<int, offsets.size()> column{};
    for (std::size_t i = 0; i < offsets.size(); ++i)
    {
        std::memcpy(&column[i], bytes + offsets[i].offset, sizeof(int));
    }
    return column;
}

auto kind_name(speculo::MemberKind kind) -> std::string_view
{
    switch (kind)
    {
    case speculo::MemberKind::Field: return "field";
    case speculo::MemberKind::StaticField: return "static field";
    case speculo::MemberKind::Method: return "method";
    case speculo::MemberKind::Destructor: return "destructor";
    }
    return "?";
}

} // namespace

int main()
{
    const Point point{ 1, 2, 3 };

    std::println("json      = {}", to_json(point));
    std::println("hash      = {}", hash_fields(point));

    constexpr auto offsets = speculo::field_table<Point>;
    static_assert(offsets.size() == 3);
    static_assert(offsets[0].name == "x");
    static_assert(speculo::field_count<Point>() == 3);

    for (const auto& info : offsets)
    {
        std::println("field {:<4} offset={} size={} align={}", info.name, info.offset, info.size, info.alignment);
    }

    const auto column = as_column(point);
    std::println("soa       = [{}, {}, {}]", column[0], column[1], column[2]);

    std::println("members of Point:");
    speculo::for_each_member<Point>([&](std::string_view name, speculo::MemberKind kind)
    {
        std::println("  {:<8} {}", name, kind_name(kind));
    });

    return 0;
}
