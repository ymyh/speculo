// S1 —— access_context：unchecked() 是否存在？能否物化并 splice/extract private 成员？
//
// 结论汇总见 .agents/review.md。本文件同时验证：
//   a) `access_context::unchecked()` 存在（头文件确认 + 编译确认）
//   b) current() 在命名空间作用域看不见 private/protected
//   c) unchecked() 能枚举出 private/protected
//   d) unchecked 得到的反射可 extract 私有静态成员的值
//   e) unchecked 得到的反射可拼接（[: M :]）私有静态成员
//   f) unchecked 得到的私有成员函数可 extract 成员函数指针并调用
#include <meta>

#include <cstddef>
#include <cstdint>
#include <print>
#include <string>
#include <string_view>

namespace m = std::meta;

struct Secret
{
    int pub = 1;

private:
    int priv = 2;
    static constexpr int stat = 42;
    int hidden() const { return priv; }

protected:
    int prot = 3;
};

// ---------------------------------------------------------------------------
// 在同一个常量表达式内按名字找成员编号（vector 不能在命名空间作用域长期存活）
// ---------------------------------------------------------------------------
consteval std::size_t find_member(std::string_view nm, m::access_context ctx)
{
    auto ms = m::members_of(^^Secret, ctx);
    for (std::size_t i = 0; i < ms.size(); ++i)
    {
        m::info mm = ms[i];
        if (m::has_identifier(mm) && m::identifier_of(mm) == nm)
        {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

static constexpr auto k_npos = static_cast<std::size_t>(-1);

static constexpr std::size_t k_pub_current  = find_member("pub", m::access_context::current());
static constexpr std::size_t k_priv_current = find_member("priv", m::access_context::current());
static constexpr std::size_t k_priv_unchecked = find_member("priv", m::access_context::unchecked());
static constexpr std::size_t k_prot_unchecked = find_member("prot", m::access_context::unchecked());
static constexpr std::size_t k_stat_unchecked = find_member("stat", m::access_context::unchecked());
static constexpr std::size_t k_hidden_unchecked = find_member("hidden", m::access_context::unchecked());

template <std::size_t I>
consteval m::info member_unchecked()
{
    return m::members_of(^^Secret, m::access_context::unchecked())[I];
}

// (d) extract 私有静态成员的常量值（unchecked）
static_assert(m::extract<int>(member_unchecked<k_stat_unchecked>()) == 42);

// (e) 拼接私有静态成员为对象
consteval int splice_private_static()
{
    return [: member_unchecked<k_stat_unchecked>() :];
}
static_assert(splice_private_static() == 42);

// (f) 私有成员函数的 invoker
template <m::info M>
consteval auto make_private_invoker()
{
    using C = typename[: m::parent_of(M) :];
    using Fp = int (C::*)() const;
    return +[](const void* p) -> int
    {
        constexpr Fp fp = m::extract<Fp>(M);
        return (static_cast<const C*>(p)->*fp)();
    };
}

int main()
{
    // (b) current() 看不到 private（也看不到 protected）
    static_assert(k_pub_current != k_npos);
    static_assert(k_priv_current == k_npos);

    // (c) unchecked() 能看到 private / protected
    static_assert(k_priv_unchecked != k_npos);
    static_assert(k_prot_unchecked != k_npos);
    static_assert(m::is_private(member_unchecked<k_priv_unchecked>()));
    static_assert(m::is_protected(member_unchecked<k_prot_unchecked>()));
    static_assert(m::is_public(member_unchecked<k_pub_current>()));

    // (f) 私有成员函数调用
    Secret s;
    auto inv = make_private_invoker<member_unchecked<k_hidden_unchecked>()>();
    const int got = inv(&s);

    std::println("S1 ok: private static extract=42, splice=42, hidden()={}", got);
}
