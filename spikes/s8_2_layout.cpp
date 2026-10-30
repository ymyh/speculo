// S8.2 布局谓词：GCC 16.2 <meta> 里哪些谓词存在、名字叫什么、取值是否与编译器内建 trait 一致。
//   注意：P2996 最终措辞给类型谓词统一加了 _type 后缀（is_trivially_copyable_type），
//   裸名 is_trivially_copyable 在 libstdc++ 16.2 中不存在（见文件末尾 #if 0 的反证）。
#include <meta>

#include <print>
#include <string>
#include <string_view>
#include <type_traits>

#include "spike_util.hpp"

namespace m = std::meta;

struct Pod
{
    int a;
    double b;
};

struct NonTrivialCopy
{
    int a;
    NonTrivialCopy(const NonTrivialCopy&);
};

struct NonTrivialDtor
{
    ~NonTrivialDtor();
};

struct StandardLayout
{
    int a;

private:
    int b;

public:
    int c;
};

struct LayoutA
{
    int x;
    short y;
};

struct LayoutB
{
    int x;
    short y;
};

struct Poly
{
    virtual void f();
    int a;
};

struct Empty
{
};

struct WithBool
{
    bool b;
};

struct WithPadding
{
    char c;
    int i;
};

struct Base
{
    int b;
};

struct Derived : Base
{
    int d;
};

struct NotInterconvertible
{
    int a;
    int b;
};

// ---- 谓词存在性：逐一交叉验证（左：<meta> 谓词，右：<type_traits>）----
#define CHECK1(pred, stdtrait, T) static_assert(m::pred(^^T) == stdtrait<T>::value, #pred " != " #stdtrait " for " #T)

CHECK1(is_trivially_copyable_type, std::is_trivially_copyable, Pod);
CHECK1(is_trivially_copyable_type, std::is_trivially_copyable, NonTrivialCopy);
CHECK1(is_trivially_copyable_type, std::is_trivially_copyable, NonTrivialDtor);
CHECK1(is_trivially_copyable_type, std::is_trivially_copyable, WithPadding);
CHECK1(is_trivially_default_constructible_type, std::is_trivially_default_constructible, Pod);
CHECK1(is_trivially_default_constructible_type, std::is_trivially_default_constructible, NonTrivialCopy);
CHECK1(is_trivially_destructible_type, std::is_trivially_destructible, Pod);
CHECK1(is_trivially_destructible_type, std::is_trivially_destructible, NonTrivialDtor);
CHECK1(is_trivially_destructible_type, std::is_trivially_destructible, Poly);
CHECK1(is_standard_layout_type, std::is_standard_layout, Pod);
CHECK1(is_standard_layout_type, std::is_standard_layout, StandardLayout);
CHECK1(is_standard_layout_type, std::is_standard_layout, Derived);
CHECK1(is_standard_layout_type, std::is_standard_layout, Poly);
CHECK1(has_unique_object_representations, std::has_unique_object_representations, Pod);
CHECK1(has_unique_object_representations, std::has_unique_object_representations, WithPadding);
CHECK1(has_unique_object_representations, std::has_unique_object_representations, WithBool);
CHECK1(is_empty_type, std::is_empty, Empty);
CHECK1(is_aggregate_type, std::is_aggregate, Pod);
CHECK1(is_polymorphic_type, std::is_polymorphic, Poly);
CHECK1(is_polymorphic_type, std::is_polymorphic, Pod);

static_assert(m::is_layout_compatible_type(^^LayoutA, ^^LayoutB) == std::is_layout_compatible_v<LayoutA, LayoutB>);
static_assert(m::is_pointer_interconvertible_base_of_type(^^Derived, ^^Base) == std::is_pointer_interconvertible_base_of_v<Derived, Base>);
static_assert(m::is_pointer_interconvertible_base_of_type(^^Base, ^^Derived) == std::is_pointer_interconvertible_base_of_v<Base, Derived>);
static_assert(m::is_convertible_type(^^Derived, ^^Base) == std::is_convertible_v<Derived, Base>);
static_assert(m::is_convertible_type(^^int, ^^long) == std::is_convertible_v<int, long>);
static_assert(m::is_nothrow_convertible_type(^^Derived, ^^Base) == std::is_nothrow_convertible_v<Derived, Base>);

consteval const char* yn(bool v)
{
    return v ? "T" : "F";
}

consteval std::string dump()
{
    std::string s;
    auto row = [&s](const char* name, const char* meta_value, const char* trait_value, bool agree)
    {
        s += name;
        s += "  meta=";
        s += meta_value;
        s += " trait=";
        s += trait_value;
        s += agree ? "  [agree]" : "  [DISAGREE]";
        s += "\n";
    };

    row("is_trivially_copyable            ", yn(m::is_trivially_copyable_type(^^Pod)), yn(std::is_trivially_copyable_v<Pod>), m::is_trivially_copyable_type(^^Pod) == std::is_trivially_copyable_v<Pod>);
    row("is_trivially_default_constructible", yn(m::is_trivially_default_constructible_type(^^Pod)), yn(std::is_trivially_default_constructible_v<Pod>), m::is_trivially_default_constructible_type(^^Pod) == std::is_trivially_default_constructible_v<Pod>);
    row("is_trivially_destructible        ", yn(m::is_trivially_destructible_type(^^Pod)), yn(std::is_trivially_destructible_v<Pod>), m::is_trivially_destructible_type(^^Pod) == std::is_trivially_destructible_v<Pod>);
    row("is_standard_layout               ", yn(m::is_standard_layout_type(^^StandardLayout)), yn(std::is_standard_layout_v<StandardLayout>), m::is_standard_layout_type(^^StandardLayout) == std::is_standard_layout_v<StandardLayout>);
    row("has_unique_object_representations", yn(m::has_unique_object_representations(^^WithPadding)), yn(std::has_unique_object_representations_v<WithPadding>), m::has_unique_object_representations(^^WithPadding) == std::has_unique_object_representations_v<WithPadding>);
    row("is_layout_compatible(A,B)        ", yn(m::is_layout_compatible_type(^^LayoutA, ^^LayoutB)), yn(std::is_layout_compatible_v<LayoutA, LayoutB>), m::is_layout_compatible_type(^^LayoutA, ^^LayoutB) == std::is_layout_compatible_v<LayoutA, LayoutB>);
    row("is_pointer_interconvertible(D,B) ", yn(m::is_pointer_interconvertible_base_of_type(^^Derived, ^^Base)), yn(std::is_pointer_interconvertible_base_of_v<Derived, Base>), m::is_pointer_interconvertible_base_of_type(^^Derived, ^^Base) == std::is_pointer_interconvertible_base_of_v<Derived, Base>);
    row("is_convertible(Derived,Base)     ", yn(m::is_convertible_type(^^Derived, ^^Base)), yn(std::is_convertible_v<Derived, Base>), m::is_convertible_type(^^Derived, ^^Base) == std::is_convertible_v<Derived, Base>);
    row("is_nothrow_convertible(Derived,B)", yn(m::is_nothrow_convertible_type(^^Derived, ^^Base)), yn(std::is_nothrow_convertible_v<Derived, Base>), m::is_nothrow_convertible_type(^^Derived, ^^Base) == std::is_nothrow_convertible_v<Derived, Base>);
    s += "has_virtual_destructor          = ";
    s += yn(m::has_virtual_destructor(^^Poly));
    s += "   (见 S8.3)\n";
    s += "is_bit_castable_to              = 库合成（size 相等 && 双方 is_trivially_copyable_type）\n";
    s += "naming: 所有类型谓词带 _type 后缀；裸名 is_trivially_copyable(...) 不存在\n";
    return s;
}

int main()
{
    static constexpr const char* d = std::define_static_string(dump());
    std::print("{}", d);
    std::println("S8.2 ok");
}

#if 0
// 反证：裸名不存在（取消注释即编译错误：'is_trivially_copyable' is not a member of 'std::meta'）
constexpr bool bad = std::meta::is_trivially_copyable(^^Pod);
#endif
