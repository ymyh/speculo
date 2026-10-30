// S8.4 函数类型分解：return_type_of / parameters_of / is_vararg_function 对函数类型与成员函数是否可用，
//      以及**成员函数指针类型**能否被反射分解（答案：不能，见文件末尾反证）。
#include <meta>

#include <print>
#include <string>

#include "spike_util.hpp"

namespace m = std::meta;

int free_fn(int, double);
double noexcept_fn(int) noexcept;

struct S
{
    int mf(int, double) const & noexcept;
    void vararg_mf(...);
    long plain(int);
};

// 从「成员函数反射」重建成员函数指针类型：F C::*（F 取 type_of，含 cv/ref 限定）
template <class C, class F>
struct mfp_of
{
    using type = F C::*;
};

template <m::info M>
consteval bool can_extract_mfp()
{
    using C = typename[:m::parent_of(M):];
    using F = typename[:m::type_of(M):];
    using MP = typename mfp_of<C, F>::type;
    constexpr MP p = m::extract<MP>(M);
    (void)p;
    return true;
}

static_assert(can_extract_mfp<^^S::mf>());
static_assert(can_extract_mfp<^^S::plain>());
static_assert(can_extract_mfp<^^S::vararg_mf>());

consteval std::string dump_fn_type(std::string& s, const char* tag, m::info T)
{
    s += "--- ";
    s += tag;
    s += "\n  is_function_type   = ";
    s += m::is_function_type(T) ? "true" : "false";
    s += "\n  is_vararg_function = ";
    s += m::is_vararg_function(T) ? "true" : "false";
    s += "\n  is_noexcept        = ";
    s += m::is_noexcept(T) ? "true" : "false";
    s += "\n  return_type_of     = [";
    s += m::display_string_of(m::return_type_of(T));
    s += "]\n  params = " + spike::to_dec(m::parameters_of(T).size()) + " ->";
    for (auto p : m::parameters_of(T))
    {
        s += " [";
        // 函数类型的形参没有声明，parameters_of 直接给出类型反射；函数声明给出形参实体。
        s += m::is_type(p) ? m::display_string_of(p) : m::display_string_of(m::type_of(p));
        s += "]";
    }
    s += "\n";
    return s;
}

consteval std::string dump()
{
    std::string s;
    dump_fn_type(s, "void(int, ...)", ^^void(int, ...));
    dump_fn_type(s, "double(int) noexcept", ^^double(int) noexcept);
    dump_fn_type(s, "void()", ^^void());

    s += "\n=== 自由函数反射 ===\n";
    s += "free_fn: is_function=";
    s += m::is_function(^^free_fn) ? "1" : "0";
    s += " parent_has_ident=";
    s += m::has_identifier(m::parent_of(^^free_fn)) ? "1" : "0";
    s += " is_namespace(parent)=";
    s += m::is_namespace(m::parent_of(^^free_fn)) ? "1" : "0";
    s += " type_of=[";
    s += m::display_string_of(m::type_of(^^free_fn));
    s += "] ret=[";
    s += m::display_string_of(m::return_type_of(^^free_fn));
    s += "] params=" + spike::to_dec(m::parameters_of(^^free_fn).size());
    s += "\n";

    s += "\n=== 成员函数反射 ===\n";
    for (auto mm : m::members_of(^^S, m::access_context::unchecked()))
    {
        if (!m::is_function(mm) || !m::has_identifier(mm))
        {
            continue;
        }
        s += "[";
        s += m::identifier_of(mm);
        s += "] parent=[";
        s += m::display_string_of(m::parent_of(mm));
        s += "] type_of=[";
        s += m::display_string_of(m::type_of(mm));
        s += "]\n  is_function_type(type_of)=";
        s += m::is_function_type(m::type_of(mm)) ? "1" : "0";
        s += " is_vararg(type_of)=";
        s += m::is_vararg_function(m::type_of(mm)) ? "1" : "0";
        s += " is_vararg(decl)=";
        s += m::is_vararg_function(mm) ? "1" : "0";
        s += " ret=[";
        s += m::display_string_of(m::return_type_of(mm));
        s += "] params=" + spike::to_dec(m::parameters_of(mm).size());
        s += " const=";
        s += m::is_const(mm) ? "1" : "0";
        s += " lref=";
        s += m::is_lvalue_reference_qualified(mm) ? "1" : "0";
        s += " rref=";
        s += m::is_rvalue_reference_qualified(mm) ? "1" : "0";
        s += " noexcept=";
        s += m::is_noexcept(mm) ? "1" : "0";
        s += "\n";
    }

    s += "\n=== 成员函数指针类型 ===\n";
    constexpr m::info MFP = ^^int (S::*)(int, double) const & noexcept;
    s += "is_member_function_pointer_type = ";
    s += m::is_member_function_pointer_type(MFP) ? "true" : "false";
    s += "\n  display_string_of = [";
    s += m::display_string_of(MFP);
    s += "]\n  return_type_of / parent_of / parameters_of: 抛 std::meta::exception（不可用）\n";
    s += "  可用替代：从成员函数反射 parent_of + type_of 组合出 F C::*，再 extract<>(见本文件末尾 static_assert)\n";
    return s;
}

int main()
{
    static constexpr const char* d = std::define_static_string(dump());
    std::print("{}", d);
    std::println("S8.4 ok");
}

#if 0
// 反证 1：成员函数指针类型不能取返回类型
//   error: uncaught exception of type 'std::meta::exception';
//   what(): 'reflection does not represent a function or function type with a return type'
constexpr auto bad1 = std::meta::return_type_of(^^int (S::*)(int, double) const);

// 反证 2：成员函数指针类型没有 parent
//   error: uncaught exception of type 'std::meta::exception';
//   what(): 'reflection does not represent an entity with parent'
constexpr auto bad2 = std::meta::parent_of(^^int (S::*)(int, double) const);

// 反证 3：parameters_of 同理不可用（consteval-only 表达式在运行期上下文外求值即报错）
constexpr auto bad3 = std::meta::parameters_of(^^int (S::*)(int, double) const);
#endif
