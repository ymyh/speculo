// probe —— 基类元信息：bases_of / 虚基 / 访问级别 / type_of(base) vs 直接拼接。
#include <meta>

#include <print>
#include <string>

#include "spike_util.hpp"

namespace m = std::meta;

struct B1
{
};
struct B2
{
};
struct VB
{
};
struct D : public B1, protected B2, virtual VB
{
};

consteval std::string dump_bases()
{
    std::string out;
    const auto bases = m::bases_of(^^D, m::access_context::unchecked());
    out += "count=" + spike::to_dec(bases.size()) + "\n";
    for (auto b : bases)
    {
        out += "  base=" + std::string(m::display_string_of(m::type_of(b)));
        out += " is_virtual=" + std::string(m::is_virtual(b) ? "true" : "false");
        out += " is_public=" + std::string(m::is_public(b) ? "true" : "false");
        out += " is_protected=" + std::string(m::is_protected(b) ? "true" : "false");
        out += " is_private=" + std::string(m::is_private(b) ? "true" : "false");
        out += " is_base=" + std::string(m::is_base(b) ? "true" : "false");
        out += " is_virtual_base_of=" + std::string(m::is_virtual_base_of_type(m::type_of(b), ^^D) ? "true" : "false");
        out += "\n";
    }
    out += "D is_polymorphic=" + std::string(m::is_polymorphic_type(^^D) ? "true" : "false") + "\n";
    out += "D is_empty=" + std::string(m::is_empty_type(^^D) ? "true" : "false") + "\n";
    return out;
}

int main()
{
    static_assert(m::bases_of(^^D, m::access_context::unchecked()).size() == 3);
    static_assert(m::is_virtual(m::bases_of(^^D, m::access_context::unchecked())[2]));
    static_assert(!m::is_virtual(m::bases_of(^^D, m::access_context::unchecked())[0]));

    static constexpr const char* d = std::define_static_string(dump_bases());
    std::print("{}", d);
    std::println("probe_base ok");
}
