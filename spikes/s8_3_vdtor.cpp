// S8.3 虚析构：<meta> 是否提供 has_virtual_destructor，以及 api_design §5.3 的降级推导是否成立。
//
// 结论摘要（详见 review.md「Phase 8」）：
//   - GCC 16.2 提供 std::meta::has_virtual_destructor，10/10 与 std::has_virtual_destructor_v 一致 → 直接用谓词。
//   - api_design §5.3 原第 2 条「is_polymorphic() 为真时析构必然为虚」是**错的**：
//     隐式析构只有在某个基类有虚析构时才虚，本类有虚函数并不使它变虚（经典 -Wdelete-non-virtual-dtor）。
//     正确规则：① 用户声明了虚析构 → 真；② 否则任一基类的（递归）has_virtual_destructor 为真 → 真；③ 其余为假。
//   - 另注：GCC 的 members_of() 对隐式虚析构报 is_virtual()==false，不能靠 is_virtual 反推。
#include <meta>

#include <print>
#include <string>
#include <type_traits>

#include "spike_util.hpp"

namespace m = std::meta;

// 1. 无虚函数、平凡析构
struct Plain
{
    int a;
};

// 2. 有虚函数、无用户析构 → 析构**不**是虚的（关键反例）
struct PolyNoDtor
{
    virtual void f();
};

// 3. 用户声明虚析构
struct UserVirtualDtor
{
    virtual ~UserVirtualDtor();
};

// 4. 用户声明非虚析构（无虚函数）
struct UserNonVirtualDtor
{
    ~UserNonVirtualDtor();
};

// 5. 基类有虚函数 + 本类用户声明非虚析构 → 析构仍非虚
struct PolyUserNonVirtualDtor
{
    virtual void f();
    ~PolyUserNonVirtualDtor();
};

// 6. 抽象基类、虚析构 defaulted
struct AbstractBase
{
    virtual void f() = 0;
    virtual ~AbstractBase() = default;
};

// 7. 派生自带虚析构的基类、自己无用户析构 → 隐式析构继承虚性
struct DerivedFromVirtualDtor : UserVirtualDtor
{
    int x;
};

// 8. 非多态基类
struct NonPolymorphicBase
{
    int b;
};

// 9. 空类
struct Empty
{
};

// 10. final 多态类（无虚析构）
struct FinalPoly final
{
    virtual void g();
};

// 11. 两级继承：虚析构来自祖父
struct MidFromVirtual : DerivedFromVirtualDtor
{
};
struct LeafFromMid : MidFromVirtual
{
};

// ---- 谓词层 ----
template <m::info T>
consteval bool has_user_declared_dtor()
{
    for (auto mm : m::members_of(T, m::access_context::unchecked()))
    {
        if (m::is_destructor(mm) && m::is_user_declared(mm))
        {
            return true;
        }
    }
    return false;
}

template <m::info T>
consteval bool has_user_declared_virtual_dtor()
{
    for (auto mm : m::members_of(T, m::access_context::unchecked()))
    {
        if (m::is_destructor(mm) && m::is_user_declared(mm) && m::is_virtual(mm))
        {
            return true;
        }
    }
    return false;
}

// 修正后的降级推导（非模板递归，避免 reflection-as-template-argument 的额外风险）
consteval bool vdtor_rule_info(m::info T)
{
    for (auto mm : m::members_of(T, m::access_context::unchecked()))
    {
        if (m::is_destructor(mm) && m::is_user_declared(mm) && m::is_virtual(mm))
        {
            return true;
        }
    }
    for (auto b : m::bases_of(T, m::access_context::unchecked()))
    {
        if (vdtor_rule_info(m::type_of(b)))
        {
            return true;
        }
    }
    return false;
}

template <m::info T>
consteval bool vdtor_rule()
{
    return vdtor_rule_info(T);
}

// api_design §5.3 原第 2 条（已证伪）——保留为反例对照
template <m::info T>
consteval bool vdtor_rule_wrong()
{
    if (has_user_declared_virtual_dtor<T>())
    {
        return true;
    }
    if (!has_user_declared_dtor<T>() && m::is_polymorphic_type(T))
    {
        return true;
    }
    return false;
}

#define CHECK_VDTOR(T)                                                                                                 \
    static_assert(m::has_virtual_destructor(^^T) == std::has_virtual_destructor_v<T>, #T " <meta> != compiler");        \
    static_assert(vdtor_rule<^^T>() == std::has_virtual_destructor_v<T>, #T " corrected rule != compiler")

CHECK_VDTOR(Plain);
CHECK_VDTOR(PolyNoDtor);
CHECK_VDTOR(UserVirtualDtor);
CHECK_VDTOR(UserNonVirtualDtor);
CHECK_VDTOR(PolyUserNonVirtualDtor);
CHECK_VDTOR(AbstractBase);
CHECK_VDTOR(DerivedFromVirtualDtor);
CHECK_VDTOR(NonPolymorphicBase);
CHECK_VDTOR(Empty);
CHECK_VDTOR(FinalPoly);
CHECK_VDTOR(LeafFromMid);

consteval const char* yn(bool v)
{
    return v ? "T" : "F";
}

consteval std::string dump()
{
    std::string s;
    s += "class                    meta compiler rule_wrong rule_fixed\n";
    int meta_bad = 0;
    int wrong_bad = 0;
    int fixed_bad = 0;
    auto row = [&]<m::info T>(const char* name, bool compiler)
    {
        const bool meta = m::has_virtual_destructor(T);
        const bool wrong = vdtor_rule_wrong<T>();
        const bool fixed = vdtor_rule<T>();
        s += name;
        s += "  meta=";
        s += yn(meta);
        s += "  compiler=";
        s += yn(compiler);
        s += "  wrong=";
        s += yn(wrong);
        s += "  fixed=";
        s += yn(fixed);
        if (meta != compiler)
        {
            ++meta_bad;
            s += "  <-- meta 错";
        }
        if (wrong != compiler)
        {
            ++wrong_bad;
            s += "  <-- 旧规则错";
        }
        if (fixed != compiler)
        {
            ++fixed_bad;
            s += "  <-- 新规则错";
        }
        s += "\n";
    };
    row.template operator()<^^Plain>("Plain", std::has_virtual_destructor_v<Plain>);
    row.template operator()<^^PolyNoDtor>("PolyNoDtor", std::has_virtual_destructor_v<PolyNoDtor>);
    row.template operator()<^^UserVirtualDtor>("UserVirtualDtor", std::has_virtual_destructor_v<UserVirtualDtor>);
    row.template operator()<^^UserNonVirtualDtor>("UserNonVirtualDtor", std::has_virtual_destructor_v<UserNonVirtualDtor>);
    row.template operator()<^^PolyUserNonVirtualDtor>("PolyUserNonVirtualDtor", std::has_virtual_destructor_v<PolyUserNonVirtualDtor>);
    row.template operator()<^^AbstractBase>("AbstractBase", std::has_virtual_destructor_v<AbstractBase>);
    row.template operator()<^^DerivedFromVirtualDtor>("DerivedFromVirtualDtor", std::has_virtual_destructor_v<DerivedFromVirtualDtor>);
    row.template operator()<^^NonPolymorphicBase>("NonPolymorphicBase", std::has_virtual_destructor_v<NonPolymorphicBase>);
    row.template operator()<^^Empty>("Empty", std::has_virtual_destructor_v<Empty>);
    row.template operator()<^^FinalPoly>("FinalPoly", std::has_virtual_destructor_v<FinalPoly>);
    row.template operator()<^^LeafFromMid>("LeafFromMid", std::has_virtual_destructor_v<LeafFromMid>);
    s += "mismatch: meta=" + spike::to_dec(static_cast<long long>(meta_bad));
    s += " rule_wrong=" + spike::to_dec(static_cast<long long>(wrong_bad));
    s += " rule_fixed=" + spike::to_dec(static_cast<long long>(fixed_bad));
    s += "  (共 11 个类)\n";
    return s;
}

int main()
{
    static constexpr const char* d = std::define_static_string(dump());
    std::print("{}", d);
    std::println("S8.3 ok: 谓词可直接用；§5.3 旧规则需修正");
}
