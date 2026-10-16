// S3 —— 数据布局：offset_of / bit_offset / size_of / alignment_of / bit_size_of，
//       以及位域的位偏移与宽度获取方式。
#include <meta>

#include <cstddef>
#include <print>
#include <string>

#include "spike_util.hpp"

namespace m = std::meta;

struct Layout
{
    char a;
    int b;
    double c;
};

struct Bits
{
    unsigned x : 3;
    unsigned y : 5;
    unsigned z : 1;
    unsigned w;
    signed s : 4;
};

template <m::info R, std::size_t I>
consteval m::info member_at()
{
    return m::members_of(R, m::access_context::unchecked())[I];
}

consteval std::string dump_layout()
{
    std::string out;
    out += "Layout: size=" + spike::to_dec(m::size_of(^^Layout)) +
           " align=" + spike::to_dec(m::alignment_of(^^Layout)) + "\n";
    for (std::size_t i = 0; i < m::nonstatic_data_members_of(^^Layout, m::access_context::unchecked()).size(); ++i)
    {
        auto md = m::nonstatic_data_members_of(^^Layout, m::access_context::unchecked())[i];
        auto o = m::offset_of(md);
        out += "  " + std::string(m::identifier_of(md)) +
               ": bytes=" + spike::to_dec(o.bytes) +
               " bits=" + spike::to_dec(o.bits) +
               " total_bits=" + spike::to_dec(o.total_bits()) +
               " size=" + spike::to_dec(m::size_of(m::type_of(md))) +
               " align=" + spike::to_dec(m::alignment_of(m::type_of(md))) + "\n";
    }

    out += "Bits: size=" + spike::to_dec(m::size_of(^^Bits)) +
           " align=" + spike::to_dec(m::alignment_of(^^Bits)) +
           " bit_size_of=" + spike::to_dec(m::bit_size_of(^^Bits)) + "\n";
    for (std::size_t i = 0; i < m::nonstatic_data_members_of(^^Bits, m::access_context::unchecked()).size(); ++i)
    {
        auto md = m::nonstatic_data_members_of(^^Bits, m::access_context::unchecked())[i];
        auto o = m::offset_of(md);
        out += "  " + std::string(m::identifier_of(md)) +
               ": is_bit_field=" + (m::is_bit_field(md) ? "true" : "false") +
               " bytes=" + spike::to_dec(o.bytes) +
               " bits=" + spike::to_dec(o.bits) +
               " total_bits=" + spike::to_dec(o.total_bits()) +
               " bit_size_of=" + spike::to_dec(m::bit_size_of(md)) +
               " sizeof=" + spike::to_dec(m::size_of(m::type_of(md))) +
               " type=" + std::string(m::display_string_of(m::type_of(md))) + "\n";
    }
    return out;
}

int main()
{
    static_assert(m::size_of(^^Layout) == 16);
    static_assert(m::alignment_of(^^Layout) == 8);

    // Layout::b 在偏移 4；Layout::c 在偏移 8
    static_assert(m::offset_of(member_at<^^Layout, 1>()).bytes == 4);
    static_assert(m::offset_of(member_at<^^Layout, 2>()).bytes == 8);

    // 位域：is_bit_field / offset_of / bit_size_of
    static_assert(m::is_bit_field(member_at<^^Bits, 0>()));
    static_assert(m::is_bit_field(member_at<^^Bits, 1>()));
    static_assert(!m::is_bit_field(member_at<^^Bits, 3>())); // w 是普通 unsigned

    static constexpr const char* dump = std::define_static_string(dump_layout());
    std::print("{}", dump);
}
