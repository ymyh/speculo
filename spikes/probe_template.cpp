// probe —— 模板成员的枚举行为（api_design §12.4）。
#include <meta>

#include <print>
#include <string>

#include "spike_util.hpp"

namespace m = std::meta;

struct S
{
    int var = 0;
    template <typename T>
    void tmpl(T)
    {
    }
    template <typename T>
    static T sfn(T v)
    {
        return v;
    }
    void over(int) {}
    void over(double) {}
};

consteval std::string dump()
{
    std::string out;
    for (auto mm : m::members_of(^^S, m::access_context::unchecked()))
    {
        out += "  id=" + std::string(m::has_identifier(mm) ? m::identifier_of(mm) : "<none>");
        out += " is_template=" + std::string(m::is_template(mm) ? "1" : "0");
        out += " is_fn_template=" + std::string(m::is_function_template(mm) ? "1" : "0");
        out += " is_function=" + std::string(m::is_function(mm) ? "1" : "0");
        out += "  display=" + std::string(m::display_string_of(mm)) + "\n";
    }
    return out;
}

int main()
{
    static constexpr const char* d = std::define_static_string(dump());
    std::print("{}", d);
    std::println("probe_template ok");
}
