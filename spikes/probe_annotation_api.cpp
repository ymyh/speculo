// Probe: what GCC 16.2 actually reflects for C++26 annotations (P3394).
#include <meta>

#include <array>
#include <print>
#include <string>
#include <string_view>

namespace m = std::meta;

struct Tag
{
};

struct Rename
{
    const char* name;
    int rank;
};

struct Label
{
    int rank;
};

enum class Color
{
    Red [[= 1]],
    Green [[= Tag{}]] [[= 2]],
};

struct Base
{
};

struct [[= Tag{}]]
[[= Rename{ "Type", 7 }]]
Widget : [[= Tag{}]] Base
{
    [[= Rename{ "first name", 1 }]]
    std::string first;

    [[= Label{ 4 }]]
    int labeled;

    [[= 42, = 43]]
    int count;

    [[= "literal"]]
    int text;

    [[= std::array<char, 8>{ "literal" }]]
    int chars;

    [[= Tag{}]]
    static int shared;

    [[= Tag{}]]
    Widget() = default;

    [[= Rename{ "greet", 3 }]]
    void greet([[= 9]] const std::string& name) const;

    [[nodiscard]]
    int plain() const { return 0; }
};

[[= Tag{}]] int Widget::shared = 0;

[[= 24]] void Widget::greet(const std::string&) const {}

using Alias [[= 5]] = int;

namespace demo
{
[[= Tag{}]] void free_function();
}

template<m::info Entity>
consteval auto summary() -> std::string_view
{
    std::string out;
    for (m::info annotation : m::annotations_of(Entity))
    {
        if (!out.empty())
        {
            out += " | ";
        }
        out += m::display_string_of(m::type_of(annotation));
        out += m::is_annotation(annotation) ? "" : " NOT";
    }
    if (out.empty())
    {
        out = "(none)";
    }
    return std::define_static_string(out);
}

template<m::info Entity, typename T>
consteval auto with_type_count() -> std::size_t
{
    return m::annotations_of_with_type(Entity, ^^T).size();
}

template<m::info Entity, typename T>
consteval auto with_const_type_count() -> std::size_t
{
    return m::annotations_of_with_type(Entity, ^^const T).size();
}

consteval auto describe(m::info entity) -> std::string_view
{
    std::string out;
    for (m::info annotation : m::annotations_of(entity))
    {
        if (!out.empty())
        {
            out += " | ";
        }
        out += m::display_string_of(m::type_of(annotation));
        out += m::is_annotation(annotation) ? "" : " NOT";
    }
    if (out.empty())
    {
        out = "(none)";
    }
    return std::define_static_string(out);
}

consteval auto base_summary() -> std::string_view
{
    const auto bases = m::bases_of(^^Widget, m::access_context::unchecked());
    return describe(bases[0]);
}

consteval auto param_summary() -> std::string_view
{
    const auto params = m::parameters_of(^^Widget::greet);
    return describe(params[0]);
}

consteval auto ctor_summary() -> std::string_view
{
    for (m::info member : m::members_of(^^Widget, m::access_context::unchecked()))
    {
        if (m::is_constructor(member) && m::is_user_declared(member))
        {
            return describe(member);
        }
    }
    return "(no ctor)";
}

consteval auto tag_extracts() -> bool
{
    [[maybe_unused]] const Tag value = m::extract<Tag>(m::annotations_of(^^Widget)[0]);
    return true;
}

consteval auto chars_extract() -> char
{
    const auto value = m::extract<std::array<char, 8>>(m::annotations_of(^^Widget::chars)[0]);
    return value[0];
}

consteval auto ref_summary() -> std::string_view
{
    return summary<^^Widget&>();
}

consteval auto ptr_summary() -> std::string_view
{
    return summary<^^Widget*>();
}

consteval auto count_first() -> int
{
    return m::extract<int>(m::annotations_of(^^Widget::count)[0]);
}

consteval auto count_second() -> int
{
    return m::extract<int>(m::annotations_of(^^Widget::count)[1]);
}

consteval auto label_rank() -> int
{
    return m::extract<Label>(m::annotations_of(^^Widget::labeled)[0]).rank;
}

consteval auto text_value() -> std::string_view
{
    const auto annotation = m::annotations_of(^^Widget::text)[0];
    return std::define_static_string(m::display_string_of(m::type_of(annotation)));
}

consteval auto alias_summary() -> std::string_view
{
    return summary<^^Alias>();
}

consteval auto free_summary() -> std::string_view
{
    return summary<^^demo::free_function>();
}

int main()
{
    std::println("class: {}", summary<^^Widget>());
    std::println("field: {}", summary<^^Widget::first>());
    std::println("ints: {}", summary<^^Widget::count>());
    std::println("static: {}", summary<^^Widget::shared>());
    std::println("ctor: {}", ctor_summary());
    std::println("method: {}", summary<^^Widget::greet>());
    std::println("plain [[nodiscard]]: {}", summary<^^Widget::plain>());
    std::println("enumerator tag: {}", summary<^^Color::Green>());
    std::println("enumerator int: {}", summary<^^Color::Red>());
    std::println("base: {}", base_summary());
    std::println("param: {}", param_summary());
    std::println("with Tag={} with const Tag={}", with_type_count<^^Widget, Tag>(),
                 with_const_type_count<^^Widget, Tag>());
    std::println("count={} {} label={}", count_first(), count_second(), label_rank());
    std::println("text type: {}", text_value());
    std::println("const class: {}", summary<^^const Widget>());
    std::println("alias: {}", alias_summary());
    std::println("free: {}", free_summary());
    std::println("chars type: {}", summary<^^Widget::chars>());
    std::println("tag extracts: {}", tag_extracts());
    std::println("chars0: {}", chars_extract());
    std::println("ref: {}", ref_summary());
    std::println("ptr: {}", ptr_summary());
}
