#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

// Layout and capability predicates (api_design §5).
//
// 单类型谓词（Class 成员，物化期求值、存成一位）与编译器内建 trait 交叉验证：
//  | 类型            | trivially_copyable | trivially_dflt | trivially_dtor | std_layout | unique_repr |
//  | Pod             | T | T | T | T | T |
//  | NonTrivialCopy  | F | F | T | T | F |
//  | NonTrivialDtor  | F | T | F | T | T |
//  | StandardLayout  | T | T | T | T | T |
//  | Poly            | F | F | F | F | F |
//  | Empty           | T | T | T | T | T |
//  | WithPadding     | T | T | T | T | F |
//  | WithBool        | T | T | T | T | F |
//  | Mixed           | T | T | T | F | F |
//
// 双类型谓词是自由函数模板（见 api_design §5.2 的偏差说明）：
//  | 谓词                                  | 交叉验证对象                                    |
//  | is_layout_compatible<A, B>            | std::is_layout_compatible_v<A, B>               |
//  | is_pointer_interconvertible_with<D, B> | std::is_pointer_interconvertible_base_of_v<B, D> |
//  | is_convertible_to<From, To>           | std::is_convertible_v<From, To>                 |
//  | is_nothrow_convertible_to<From, To>   | std::is_nothrow_convertible_v<From, To>         |

namespace
{

struct Pod
{
    int a;
    double b;
};

struct NonTrivialCopy
{
    int a = 0;
    NonTrivialCopy(const NonTrivialCopy& other) : a(other.a) {}
    NonTrivialCopy() = default;
    auto operator=(const NonTrivialCopy&) -> NonTrivialCopy& = default;
};

struct NonTrivialDtor
{
    int a = 0;
    ~NonTrivialDtor() {}
};

struct StandardLayout
{
    int a = 0;

private:
    int b = 0;

public:
    int c = 0;
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
    virtual ~Poly() = default;
    int a = 0;
};

struct Empty
{
};

struct WithPadding
{
    char c;
    int i;
};

struct WithBool
{
    bool b;
};

struct WithVirtualDtor
{
    virtual void f() {}
    virtual ~WithVirtualDtor() = default;
    int a = 0;
};

struct VirtualOnly
{
    virtual void f() {}
    int a = 0;
};

union LocalUnion
{
    int i;
    float f;
};

struct Wrapper
{
    NonTrivialCopy inner;
};

struct LayoutBase
{
    int value = 0;
};

/// 标准布局：只有基类带数据成员（派生类没有），基类子对象在偏移 0。
struct InterBase
{
    int value = 0;
};

struct InterDerived : InterBase
{
};

struct LayoutDerived : LayoutBase
{
    int extra = 0;
};

struct NotInterconvertible
{
    int a = 0;
    int b = 0;
};

struct Other
{
    int value = 0;
};

struct Bools
{
    bool b = false;
    explicit operator bool() const { return b; }
};

struct ThrowingConversion
{
    operator int() const { return 1; }
};

struct NoexceptConversion
{
    operator int() const noexcept { return 1; }
};

} // namespace

TEST(LayoutTraitsTest, SingleTypePredicatesMatchTheCompilerTraits)
{
#define SPECULO_CHECK_TRAIT(Trait, Predicate, Type)                                                    \
    EXPECT_EQ(speculo::class_of<Type>().Predicate(), Trait<Type>::value) << #Predicate " for " #Type

    SPECULO_CHECK_TRAIT(std::is_trivially_copyable, is_trivially_copyable, Pod);
    SPECULO_CHECK_TRAIT(std::is_trivially_copyable, is_trivially_copyable, NonTrivialCopy);
    SPECULO_CHECK_TRAIT(std::is_trivially_copyable, is_trivially_copyable, NonTrivialDtor);
    SPECULO_CHECK_TRAIT(std::is_trivially_copyable, is_trivially_copyable, StandardLayout);

    SPECULO_CHECK_TRAIT(std::is_trivially_default_constructible, is_trivially_default_constructible, Pod);
    SPECULO_CHECK_TRAIT(std::is_trivially_default_constructible, is_trivially_default_constructible, NonTrivialCopy);
    SPECULO_CHECK_TRAIT(std::is_trivially_default_constructible, is_trivially_default_constructible, Empty);

    SPECULO_CHECK_TRAIT(std::is_trivially_destructible, is_trivially_destructible, Pod);
    SPECULO_CHECK_TRAIT(std::is_trivially_destructible, is_trivially_destructible, NonTrivialDtor);
    SPECULO_CHECK_TRAIT(std::is_trivially_destructible, is_trivially_destructible, Poly);

    SPECULO_CHECK_TRAIT(std::is_standard_layout, is_standard_layout, Pod);
    SPECULO_CHECK_TRAIT(std::is_standard_layout, is_standard_layout, StandardLayout);
    SPECULO_CHECK_TRAIT(std::is_standard_layout, is_standard_layout, LayoutDerived);
    SPECULO_CHECK_TRAIT(std::is_standard_layout, is_standard_layout, Poly);

    SPECULO_CHECK_TRAIT(std::has_unique_object_representations, has_unique_object_representations, Pod);
    SPECULO_CHECK_TRAIT(std::has_unique_object_representations, has_unique_object_representations, WithPadding);
    SPECULO_CHECK_TRAIT(std::has_unique_object_representations, has_unique_object_representations, WithBool);

    SPECULO_CHECK_TRAIT(std::has_virtual_destructor, has_virtual_destructor, WithVirtualDtor);
    SPECULO_CHECK_TRAIT(std::has_virtual_destructor, has_virtual_destructor, VirtualOnly);
    SPECULO_CHECK_TRAIT(std::has_virtual_destructor, has_virtual_destructor, Empty);

#undef SPECULO_CHECK_TRAIT
}

TEST(LayoutTraitsTest, NonClassTypesAnswerFalse)
{
    // Documented deviation (api_design §5.2): the predicates are class-level
    // capabilities, so a non-class type answers false instead of panicking --
    // even where the compiler trait itself would be true (int is trivially
    // copyable).
    EXPECT_TRUE(std::is_trivially_copyable_v<int>);
    EXPECT_FALSE(speculo::class_of<int>().is_trivially_copyable());
    EXPECT_FALSE(speculo::class_of<int>().is_trivially_default_constructible());
    EXPECT_FALSE(speculo::class_of<int>().is_trivially_destructible());
    EXPECT_FALSE(speculo::class_of<int>().is_standard_layout());
    EXPECT_FALSE(speculo::class_of<int>().has_unique_object_representations());
    EXPECT_FALSE(speculo::class_of<int>().has_virtual_destructor());

    EXPECT_FALSE(speculo::class_of<int*>().is_trivially_copyable());
    EXPECT_FALSE(speculo::class_of<int&>().is_standard_layout());
    EXPECT_FALSE(speculo::class_of<int[3]>().is_trivially_copyable());
    EXPECT_FALSE(speculo::class_of<void>().has_virtual_destructor());
}

TEST(LayoutTraitsTest, UnionsAreCoveredToo)
{
    // A union is a class type for the capability bits.
    const speculo::Class& choice = speculo::class_of<LocalUnion>();

    EXPECT_TRUE(choice.is_union());
    EXPECT_EQ(choice.is_trivially_copyable(), std::is_trivially_copyable_v<LocalUnion>);
    EXPECT_EQ(choice.is_trivially_destructible(), std::is_trivially_destructible_v<LocalUnion>);
    EXPECT_EQ(choice.has_unique_object_representations(), std::has_unique_object_representations_v<LocalUnion>);
}

TEST(LayoutTraitsTest, BitCastableIsLibrarySynthesized)
{
    // Same size and both trivially copyable.
    EXPECT_TRUE(speculo::class_of<LayoutA>().is_bit_castable_to(speculo::class_of<LayoutB>()));

    // Different sizes: false in both directions.
    EXPECT_FALSE(speculo::class_of<LayoutA>().is_bit_castable_to(speculo::class_of<Pod>()));

    // Same size but not trivially copyable: false.
    EXPECT_EQ(sizeof(Wrapper), sizeof(NonTrivialCopy));
    EXPECT_FALSE(speculo::class_of<Wrapper>().is_bit_castable_to(speculo::class_of<NonTrivialCopy>()));

    // Non-class types have no capability bits, so they never bit-cast.
    EXPECT_FALSE(speculo::class_of<int>().is_bit_castable_to(speculo::class_of<int>()));
}

TEST(LayoutTraitsTest, TwoTypePredicatesMatchTheCompilerTraits)
{
    // 同一批类型上，库的结论必须与编译器内建 trait 相同（v2 的 trait 交叉验证手段）。
    // 两侧都先取成常量，避免 `<A, B>` 里的逗号被 gtest 宏当成参数分隔符。
    constexpr bool layout_same_shape = speculo::is_layout_compatible<LayoutA, LayoutB>();
    constexpr bool trait_layout_same_shape = std::is_layout_compatible_v<LayoutA, LayoutB>;
    EXPECT_EQ(layout_same_shape, trait_layout_same_shape);
    EXPECT_TRUE(layout_same_shape);

    constexpr bool layout_different_shape = speculo::is_layout_compatible<LayoutA, Pod>();
    constexpr bool trait_layout_different_shape = std::is_layout_compatible_v<LayoutA, Pod>;
    EXPECT_EQ(layout_different_shape, trait_layout_different_shape);
    EXPECT_FALSE(layout_different_shape);

    // Operand order: the public function takes (derived, base), the std trait
    // takes (base, derived) -- the S8.10 landmine.
    constexpr bool interconvertible = speculo::is_pointer_interconvertible_with<InterDerived, InterBase>();
    constexpr bool trait_interconvertible = std::is_pointer_interconvertible_base_of_v<InterBase, InterDerived>;
    EXPECT_EQ(interconvertible, trait_interconvertible);
    EXPECT_TRUE(interconvertible);

    // Two classes that both carry data members are not standard-layout, so their
    // base subobject is not pointer-interconvertible.
    constexpr bool not_standard_layout = speculo::is_pointer_interconvertible_with<LayoutDerived, LayoutBase>();
    constexpr bool trait_not_standard_layout = std::is_pointer_interconvertible_base_of_v<LayoutBase, LayoutDerived>;
    EXPECT_EQ(not_standard_layout, trait_not_standard_layout);
    EXPECT_FALSE(not_standard_layout);

    constexpr bool unrelated = speculo::is_pointer_interconvertible_with<Other, LayoutBase>();
    constexpr bool trait_unrelated = std::is_pointer_interconvertible_base_of_v<LayoutBase, Other>;
    EXPECT_EQ(unrelated, trait_unrelated);
    EXPECT_FALSE(unrelated);
    constexpr bool not_interconvertible = speculo::is_pointer_interconvertible_with<NotInterconvertible, LayoutDerived>();
    EXPECT_FALSE(not_interconvertible);

    constexpr bool derived_to_base = speculo::is_convertible_to<LayoutDerived, LayoutBase>();
    constexpr bool trait_derived_to_base = std::is_convertible_v<LayoutDerived, LayoutBase>;
    EXPECT_EQ(derived_to_base, trait_derived_to_base);
    EXPECT_TRUE(derived_to_base);

    constexpr bool numeric = speculo::is_convertible_to<int, long>();
    constexpr bool trait_numeric = std::is_convertible_v<int, long>;
    EXPECT_EQ(numeric, trait_numeric);
    EXPECT_TRUE(numeric);

    constexpr bool base_to_derived = speculo::is_convertible_to<LayoutBase, LayoutDerived>();
    constexpr bool trait_base_to_derived = std::is_convertible_v<LayoutBase, LayoutDerived>;
    EXPECT_EQ(base_to_derived, trait_base_to_derived);
    EXPECT_FALSE(base_to_derived);

    constexpr bool unrelated_conversion = speculo::is_convertible_to<Other, LayoutBase>();
    constexpr bool trait_unrelated_conversion = std::is_convertible_v<Other, LayoutBase>;
    EXPECT_EQ(unrelated_conversion, trait_unrelated_conversion);
    EXPECT_FALSE(unrelated_conversion);

    constexpr bool quiet = speculo::is_nothrow_convertible_to<NoexceptConversion, int>();
    constexpr bool trait_quiet = std::is_nothrow_convertible_v<NoexceptConversion, int>;
    EXPECT_EQ(quiet, trait_quiet);
    EXPECT_TRUE(quiet);

    constexpr bool noisy = speculo::is_nothrow_convertible_to<ThrowingConversion, int>();
    constexpr bool trait_noisy = std::is_nothrow_convertible_v<ThrowingConversion, int>;
    EXPECT_EQ(noisy, trait_noisy);
    EXPECT_FALSE(noisy);

    constexpr bool explicit_conversion = speculo::is_nothrow_convertible_to<Bools, bool>();
    constexpr bool trait_explicit_conversion = std::is_nothrow_convertible_v<Bools, bool>;
    EXPECT_EQ(explicit_conversion, trait_explicit_conversion);
    EXPECT_FALSE(explicit_conversion);
}
