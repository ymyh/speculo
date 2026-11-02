// S13 probe: what P13 (relation graph) can and cannot get from <meta>.
//
// 1. `{aka ...}` in display strings: does a typedef'd type show up as
//    "int {aka MyInt}" (which would leak into Class::get_qualified_name)?
// 2. vtable slot: is there any <meta> facility at all? (expected: no)
// 3. override recognition: is_override / is_virtual on a derived method.

#include <meta>
#include <print>
#include <string_view>
#include <vector>

namespace m = std::meta;

using MyInt = int;
struct Named
{
    int value = 0;
};
using NamedAlias = Named;
using IntVec = std::vector<int>;

struct Base
{
    virtual ~Base() = default;
    virtual void speak(int times) const { std::println("Base {}", times); }
    virtual int answer() { return 42; }
    void nonvirtual() {}
};

struct Derived : Base
{
    void speak(int times) const override { std::println("Derived {}", times); }
    int answer() override { return 7; }
    void extra() {}
};

template<typename T>
consteval auto name_of() -> std::string_view
{
    return m::display_string_of(^^T);
}

template<m::info C>
consteval auto member_count() -> std::size_t
{
    return m::members_of(C, m::access_context::unchecked()).size();
}

template<m::info C, std::size_t I>
consteval auto member_at() -> m::info
{
    return m::members_of(C, m::access_context::unchecked())[I];
}

template<m::info C>
consteval auto members()
{
    return []<std::size_t... I>(std::index_sequence<I...>) -> std::array<m::info, sizeof...(I)>
    {
        return { member_at<C, I>()... };
    }(std::make_index_sequence<member_count<C>()>{});
}

template<typename T>
void report_members(std::string_view label)
{
    std::println("-- {} --", label);
    template for (constexpr m::info M : members<^^T>())
    {
        if constexpr (m::is_function(M) && !m::is_special_member_function(M))
        {
            std::println("  [{}] virtual={} override={} pure={} display=[{}]", m::identifier_of(M), m::is_virtual(M),
                         m::is_override(M), m::is_pure_virtual(M), m::display_string_of(M));
        }
    }
}

int main()
{
    std::println("MyInt                = [{}]", name_of<MyInt>());
    std::println("Named                = [{}]", name_of<Named>());
    std::println("NamedAlias           = [{}]", name_of<NamedAlias>());
    std::println("IntVec               = [{}]", name_of<IntVec>());
    std::println("std::size_t          = [{}]", name_of<std::size_t>());
    std::println("const MyInt&         = [{}]", name_of<const MyInt&>());
    std::println("MyInt[3]             = [{}]", name_of<MyInt[3]>());
    std::println("qualified Named      = [{}]", m::display_string_of(^^Named));

    report_members<Derived>("Derived");
    report_members<Base>("Base");
}
