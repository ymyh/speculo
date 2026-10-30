// S8.8 存储期：是否有 is_thread_local 类谓词（实际名字是 has_thread_storage_duration），
//      以及 thread_local 静态成员/命名空间变量的识别。
#include <meta>

#include <print>
#include <string>

#include "spike_util.hpp"

namespace m = std::meta;

int g_plain = 1;
thread_local int g_tl = 2;
inline constexpr int g_ice = 3;

struct Holder
{
    static int s_plain;
    static thread_local int s_tl;
    inline static constexpr int s_ice = 4;
    int inst = 0;
};

int Holder::s_plain = 5;
thread_local int Holder::s_tl = 6;

// ---- 编译期结论 ----
static_assert(!m::has_thread_storage_duration(^^g_plain));
static_assert(m::has_thread_storage_duration(^^g_tl));
static_assert(m::has_static_storage_duration(^^g_plain));
static_assert(m::has_static_storage_duration(^^g_ice));
static_assert(!m::has_static_storage_duration(^^g_tl)); // thread_local 是独立的存储期，不算 static

consteval std::string dump()
{
    std::string s;
    s += "变量            static thread automatic is_variable\n";
    auto row = [&s](std::string_view name, m::info v)
    {
        s += name;
        s += "  static=";
        s += m::has_static_storage_duration(v) ? "T" : "F";
        s += "  thread=";
        s += m::has_thread_storage_duration(v) ? "T" : "F";
        s += "  auto=";
        s += m::has_automatic_storage_duration(v) ? "T" : "F";
        s += "  is_variable=";
        s += m::is_variable(v) ? "T" : "F";
        s += "\n";
    };
    row("g_plain", ^^g_plain);
    row("g_tl", ^^g_tl);
    row("g_ice", ^^g_ice);
    for (auto mm : m::members_of(^^Holder, m::access_context::unchecked()))
    {
        if (m::is_variable(mm) && m::has_identifier(mm))
        {
            row(std::string("Holder::") + std::string(m::identifier_of(mm)), mm);
        }
    }
    // 静态成员识别（get_static_address 的前置条件）
    for (auto mm : m::members_of(^^Holder, m::access_context::unchecked()))
    {
        if (m::is_variable(mm) && m::has_identifier(mm) && m::identifier_of(mm) == "s_plain")
        {
            s += "s_plain: is_static_member = ";
            s += m::is_static_member(mm) ? "1" : "0";
            s += "  is_nonstatic_data_member = ";
            s += m::is_nonstatic_data_member(mm) ? "1" : "0";
            s += "\n";
        }
    }
    s += "注意：<meta> 没有 is_thread_local 这个名字，只有 has_thread_storage_duration。\n";
    s += "Modifier::THREAD_LOCAL 可直接由 has_thread_storage_duration 置位（无需降级）。\n";
    return s;
}

int main()
{
    static constexpr const char* d = std::define_static_string(dump());
    std::print("{}", d);
    std::println("S8.8 ok");
}
