// S2 —— 成员枚举：members_of / nonstatic_data_members_of 的过滤与命名。
//       重点：运算符、转换运算符、析构函数是否被枚举、名字长什么样；
//       隐式生成的特殊成员是否出现；嵌套类型/别名/枚举如何处理。
#include <meta>

#include <cstddef>
#include <print>
#include <string>
#include <string_view>
#include <utility>

namespace m = std::meta;

struct Sample
{
    int data = 0;
    static int sdata;
    mutable int mut = 0;
    const int cval = 5;
    int bits : 3;
    int& ref;

    Sample(int& r) : ref(r) {}

    void method() const noexcept {}
    static void sfn() {}
    virtual void vfn() {}
    virtual void pure() = 0;
    int operator+(int x) const { return data + x; }
    operator int() const { return data; }
    Sample& operator=(const Sample&) = default;
    bool operator==(const Sample&) const = default;
    ~Sample() = default;

    struct Nested
    {
        int n;
    };
    using Alias = double;
    enum Kind
    {
        A,
        B
    };
    enum class Scoped : unsigned char
    {
        X,
        Y = 7
    };
};

int Sample::sdata = 1;

template <m::info R, std::size_t I>
consteval void append_member(std::string& out)
{
    constexpr m::info mm = m::members_of(R, m::access_context::unchecked())[I];

    out += "  ";
    if (m::has_identifier(mm))
    {
        out += std::string(m::identifier_of(mm));
    }
    else
    {
        out += "<no-identifier>";
    }

    out += "  display=\"";
    out += std::string(m::display_string_of(mm));
    out += "\"  [";

    if constexpr (m::is_class_member(mm))
    {
        if (m::is_public(mm))
        {
            out += "public ";
        }
        else if (m::is_protected(mm))
        {
            out += "protected ";
        }
        else if (m::is_private(mm))
        {
            out += "private ";
        }
    }

    if constexpr (m::is_function(mm))
    {
        if (m::is_constructor(mm))
        {
            out += "ctor ";
        }
        if (m::is_destructor(mm))
        {
            out += "dtor ";
        }
        if (m::is_static_member(mm))
        {
            out += "static ";
        }
        if (m::is_virtual(mm))
        {
            out += "virtual ";
        }
        if (m::is_pure_virtual(mm))
        {
            out += "pure ";
        }
        if (m::is_const(mm))
        {
            out += "const ";
        }
        if (m::is_noexcept(mm))
        {
            out += "noexcept ";
        }
        if (m::is_deleted(mm))
        {
            out += "deleted ";
        }
        if (m::is_defaulted(mm))
        {
            out += "defaulted ";
        }
        if (m::is_explicit(mm))
        {
            out += "explicit ";
        }
        if (m::is_operator_function(mm))
        {
            out += "operator('";
            out += std::string(m::symbol_of(m::operator_of(mm)));
            out += "') ";
        }
        if (m::is_conversion_function(mm))
        {
            out += "conversion ";
        }
        if (m::is_lvalue_reference_qualified(mm))
        {
            out += "&-qual ";
        }
        if (m::is_rvalue_reference_qualified(mm))
        {
            out += "&&-qual ";
        }
        if (!m::is_user_declared(mm))
        {
            out += "implicit ";
        }

        out += "params(";
        constexpr std::size_t np = m::parameters_of(mm).size();
        [&]<std::size_t... J>(std::index_sequence<J...>)
        {
            ((out += std::string(m::display_string_of(m::type_of(m::parameters_of(mm)[J]))) + ","), ...);
        }(std::make_index_sequence<np>{});
        out += ")";
    }
    else if constexpr (m::is_nonstatic_data_member(mm))
    {
        if (m::is_mutable_member(mm))
        {
            out += "mutable ";
        }
        if (m::is_bit_field(mm))
        {
            out += "bitfield ";
        }
        if (m::is_const_type(m::type_of(mm)))
        {
            out += "const ";
        }
        out += "type=" + std::string(m::display_string_of(m::type_of(mm)));
    }
    else if constexpr (m::is_static_member(mm))
    {
        out += "static-data type=" + std::string(m::display_string_of(m::type_of(mm)));
    }
    else if constexpr (m::is_enumerator(mm))
    {
        out += "enumerator ";
    }
    else if constexpr (m::is_type_alias(mm))
    {
        out += "type-alias ";
    }
    else if constexpr (m::is_type(mm))
    {
        out += "type ";
    }

    out += "]\n";
}

template <m::info R>
consteval std::string dump_members()
{
    constexpr std::size_t n = m::members_of(R, m::access_context::unchecked()).size();
    std::string out;
    [&]<std::size_t... I>(std::index_sequence<I...>)
    {
        (append_member<R, I>(out), ...);
    }(std::make_index_sequence<n>{});
    return out;
}

int main()
{
    // std::string 不能在命名空间作用域作为 constexpr 变量存活（非瞬时分配），
    // 用 std::define_static_string 把它提升到静态存储。
    static constexpr const char* dump = std::define_static_string(dump_members<^^Sample>());
    constexpr std::size_t entry_count = m::members_of(^^Sample, m::access_context::unchecked()).size();
    std::println("=== members_of(Sample) : {} entries ===", entry_count);
    std::print("{}", dump);

    // 精确断言
    static_assert(m::is_abstract_type(^^Sample));
    static_assert(m::is_polymorphic_type(^^Sample));

    // 运算符 / 转换 / 析构确实被枚举
    constexpr auto n = m::members_of(^^Sample, m::access_context::unchecked()).size();
    static_assert(n > 0);
    static_assert([] {
        for (std::size_t i = 0; i < m::members_of(^^Sample, m::access_context::unchecked()).size(); ++i)
        {
            if (m::is_operator_function(m::members_of(^^Sample, m::access_context::unchecked())[i]))
            {
                return true;
            }
        }
        return false;
    }());
    static_assert([] {
        for (std::size_t i = 0; i < m::members_of(^^Sample, m::access_context::unchecked()).size(); ++i)
        {
            if (m::is_conversion_function(m::members_of(^^Sample, m::access_context::unchecked())[i]))
            {
                return true;
            }
        }
        return false;
    }());
    static_assert([] {
        for (std::size_t i = 0; i < m::members_of(^^Sample, m::access_context::unchecked()).size(); ++i)
        {
            if (m::is_destructor(m::members_of(^^Sample, m::access_context::unchecked())[i]))
            {
                return true;
            }
        }
        return false;
    }());
}
