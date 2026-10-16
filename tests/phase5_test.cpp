#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

namespace
{

struct Root
{
    int r = 0;
    virtual ~Root() = default;
};

struct Other
{
    int o = 0;
};

struct Multi : Root, Other
{
    int m = 0;
};

struct VRoot
{
    int vr = 0;
};

struct VLeaf : virtual VRoot
{
    int vl = 0;
};

// Non-virtual diamond: casting D -> A is ambiguous.
struct A
{
    int a = 0;
};
struct B1 : A
{
};
struct B2 : A
{
};
struct D : B1, B2
{
};

// Virtual diamond: both paths reach the one shared virtual base subobject.
struct DiamondBase
{
    int db = 0;
};
struct DiamondLeft : virtual DiamondBase
{
};
struct DiamondRight : virtual DiamondBase
{
};
struct DiamondBottom : DiamondLeft, DiamondRight
{
};

// One virtual and one non-virtual path: two distinct DiamondBase subobjects.
struct MixedVirtual : virtual DiamondBase
{
};
struct MixedPlain : DiamondBase
{
};
struct MixedBottom : MixedVirtual, MixedPlain
{
};

// Inaccessible bases: the relation exists in the type graph but cannot be used.
struct HiddenBase
{
    int hb = 0;
};
struct ProtectedBase
{
    int pb = 0;
};
struct Unrelated
{
    int u = 0;
};
class Closed : private HiddenBase, protected ProtectedBase, public Root
{
};

// Recursive member lookup: hiding, overloads, siblings, static members.
struct HideBase
{
    int x = 1;
    int y = 5;
    int f() const { return 1; }
};
struct HideDerived : HideBase
{
    int x = 2;
};

struct Over
{
    int g() const { return 1; }
    int g(int v) const { return v; }
};
struct OverDerived : Over
{
};
struct OverHiding : Over
{
    int g(double) const { return 3; }
};

struct P1
{
    int same = 1;
};
struct P2
{
    int same = 2;
};
struct PP : P1, P2
{
};

struct SBase
{
    int nv = 0;
    static int sv() { return 5; }
};
struct SLeft : SBase
{
};
struct SRight : SBase
{
};
struct SBottom : SLeft, SRight
{
};

enum class Color
{
    Red,
    Green = 5,
    Blue,
};

enum Legacy
{
    Alpha = 1,
    Beta = 2,
};

enum class Narrow : short
{
    Neg = -3,
};

enum class Small : unsigned char
{
    Low = 1,
    High = 255,
};

enum class Big : unsigned long long
{
    Huge = 0xFFFFFFFFFFFFFFFFULL,
};

struct Prop
{
    int value = 0;
};

struct Arrays
{
    int v[3] = { 1, 2, 3 };
    int m[2][2] = { { 1, 2 }, { 3, 4 } };
};

struct NoDefault
{
    int v = 0;
    NoDefault() = delete;
    explicit NoDefault(int x) : v(x) {}
};

struct Tracked
{
    static int alive;

    int v = 0;

    Tracked() { ++alive; }
    explicit Tracked(int value) : v(value) { ++alive; }
    Tracked(const Tracked& other) : v(other.v) { ++alive; }
    auto operator=(const Tracked& other) -> Tracked& = default;
    ~Tracked() { --alive; }
};

int Tracked::alive = 0;

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Type identity: exact types, decomposition of pointers / cv / references
// ─────────────────────────────────────────────────────────────────────────────

TEST(TypeIdentityTest, ExactTypesAreDistinct)
{
    EXPECT_NE(speculo::class_of<int>(), speculo::class_of<const int>());
    EXPECT_NE(speculo::class_of<int>(), speculo::class_of<int&>());
    EXPECT_NE(speculo::class_of<int&>(), speculo::class_of<int&&>());
    EXPECT_TRUE(speculo::class_of<const int>().is_fundamental());
    EXPECT_TRUE(speculo::class_of<int&>().is_reference());
    EXPECT_EQ(speculo::class_of<int&>().kind(), speculo::TypeKind::Reference);
}

TEST(TypeIdentityTest, PointerDecomposition)
{
    EXPECT_TRUE(speculo::class_of<int*>().is_pointer());
    EXPECT_EQ(speculo::class_of<int*>().get_pointee_type().unwrap(), speculo::class_of<int>());
    EXPECT_EQ(speculo::class_of<const int*>().get_pointee_type().unwrap(), speculo::class_of<const int>());
    EXPECT_EQ(speculo::class_of<int**>().get_pointee_type().unwrap(), speculo::class_of<int*>());
    EXPECT_TRUE(speculo::class_of<int>().get_pointee_type().is_none());

    EXPECT_TRUE(speculo::class_of<int Prop::*>().is_member_pointer());
    EXPECT_EQ(speculo::class_of<int Prop::*>().get_pointee_class_type().unwrap(), speculo::class_of<Prop>());
    EXPECT_TRUE(speculo::class_of<int*>().get_pointee_class_type().is_none());
}

TEST(TypeIdentityTest, CvAndReferenceDecomposition)
{
    EXPECT_TRUE(speculo::class_of<const int>().is_const_qualified());
    EXPECT_FALSE(speculo::class_of<int>().is_const_qualified());
    EXPECT_EQ(speculo::class_of<const int>().get_unqualified_type().unwrap(), speculo::class_of<int>());
    EXPECT_TRUE(speculo::class_of<int>().get_unqualified_type().is_none());

    EXPECT_TRUE(speculo::class_of<volatile int>().is_volatile_qualified());

    EXPECT_EQ(speculo::class_of<int&>().get_reference_kind(), speculo::RefQualifier::Lvalue);
    EXPECT_EQ(speculo::class_of<int&&>().get_reference_kind(), speculo::RefQualifier::Rvalue);
    EXPECT_EQ(speculo::class_of<int>().get_reference_kind(), speculo::RefQualifier::None);
    EXPECT_EQ(speculo::class_of<int&>().get_referenced_type().unwrap(), speculo::class_of<int>());
    EXPECT_EQ(speculo::class_of<const int&>().get_referenced_type().unwrap(), speculo::class_of<const int>());
}

TEST(TypeIdentityTest, ArrayRankAndExtent)
{
    EXPECT_TRUE(speculo::class_of<int[4]>().is_array());
    EXPECT_EQ(speculo::class_of<int[4]>().get_array_extent().unwrap(), 4u);
    EXPECT_EQ(speculo::class_of<int[4]>().get_array_rank(), 1u);
    EXPECT_EQ(speculo::class_of<int[4]>().get_element_type().unwrap(), speculo::class_of<int>());
    EXPECT_EQ(speculo::class_of<int[4]>().get_size(), sizeof(int[4]));

    EXPECT_EQ(speculo::class_of<int[3][5]>().get_array_rank(), 2u);
    EXPECT_EQ(speculo::class_of<int[3][5]>().get_element_type().unwrap(), speculo::class_of<int[5]>());
    EXPECT_EQ(speculo::class_of<int[3][5]>().get_element_type().unwrap().get_array_extent().unwrap(), 5u);

    EXPECT_EQ(speculo::class_of<int[]>().get_array_extent().unwrap(), 0u);
    EXPECT_TRUE(speculo::class_of<int>().get_element_type().is_none());
}

// ─────────────────────────────────────────────────────────────────────────────
// Inheritance navigation: cast_to / is_assignable_from / is_instance
// ─────────────────────────────────────────────────────────────────────────────

TEST(InheritanceTest, SingleBaseCast)
{
    Multi m;
    m.r = 11;
    m.o = 22;
    m.m = 33;

    const speculo::Class& multi = speculo::class_of<Multi>();
    speculo::ObjectRef ref(m);

    auto as_root = multi.cast_to(ref, speculo::class_of<Root>());
    ASSERT_TRUE(as_root.is_ok());
    EXPECT_EQ(as_root.unwrap().type(), speculo::class_of<Root>());
    EXPECT_EQ(as_root.unwrap().as<Root>().unwrap().r, 11);

    // Identity cast returns the same address.
    auto identity = multi.cast_to(ref, speculo::class_of<Multi>());
    ASSERT_TRUE(identity.is_ok());
    EXPECT_EQ(identity.unwrap().raw_data(), ref.raw_data());
}

TEST(InheritanceTest, MultipleBasesAdjustPointer)
{
    Multi m;
    m.r = 11;
    m.o = 22;

    const speculo::Class& multi = speculo::class_of<Multi>();
    speculo::ObjectRef ref(m);

    auto as_root = multi.cast_to(ref, speculo::class_of<Root>());
    auto as_other = multi.cast_to(ref, speculo::class_of<Other>());
    ASSERT_TRUE(as_root.is_ok());
    ASSERT_TRUE(as_other.is_ok());

    EXPECT_EQ(as_root.unwrap().as<Root>().unwrap().r, 11);
    EXPECT_EQ(as_other.unwrap().as<Other>().unwrap().o, 22);
    // The second base is not at offset 0, so the adjusted pointer differs.
    EXPECT_NE(as_root.unwrap().raw_data(), as_other.unwrap().raw_data());
}

TEST(InheritanceTest, VirtualBaseCast)
{
    VLeaf v;
    v.vr = 77;
    v.vl = 88;

    const speculo::Class& leaf = speculo::class_of<VLeaf>();
    speculo::ObjectRef ref(v);

    ASSERT_TRUE(leaf.get_bases()[0].is_virtual());
    ASSERT_NE(leaf.get_bases()[0].cast, nullptr);

    auto as_root = leaf.cast_to(ref, speculo::class_of<VRoot>());
    ASSERT_TRUE(as_root.is_ok());
    EXPECT_EQ(as_root.unwrap().type(), speculo::class_of<VRoot>());
    EXPECT_EQ(as_root.unwrap().as<VRoot>().unwrap().vr, 77);
}

TEST(InheritanceTest, CastPreservesConstnessAndErrors)
{
    const Multi m{};
    speculo::ObjectRef ref(m);
    EXPECT_TRUE(ref.is_const());

    auto as_root = speculo::class_of<Multi>().cast_to(ref, speculo::class_of<Root>());
    ASSERT_TRUE(as_root.is_ok());
    EXPECT_TRUE(as_root.unwrap().is_const());
    EXPECT_TRUE(as_root.unwrap().as<Root>().is_none()); // non-const access refused

    // Down-cast is not supported -> TypeMismatch.
    Multi mutable_m;
    speculo::ObjectRef mutable_ref(mutable_m);
    EXPECT_EQ(speculo::class_of<Root>().cast_to(mutable_ref, speculo::class_of<Multi>()).unwrap_err().kind,
              speculo::ErrorKind::TypeMismatch);

    // Null object.
    EXPECT_EQ(speculo::class_of<Multi>().cast_to(speculo::ObjectRef{}, speculo::class_of<Root>()).unwrap_err().kind,
              speculo::ErrorKind::NullInstance);
}

TEST(InheritanceTest, AmbiguousCast)
{
    D d;
    speculo::ObjectRef ref(d);
    auto ambiguous = speculo::class_of<D>().cast_to(ref, speculo::class_of<A>());
    ASSERT_TRUE(ambiguous.is_err());
    EXPECT_EQ(ambiguous.unwrap_err().kind, speculo::ErrorKind::Ambiguous);
}

TEST(InheritanceTest, CastToRequiresTheReceiverToBeThisClass)
{
    // `Multi` has a `Root` base, but the memory is not a Multi: the cast must
    // not apply Multi's layout to it.
    Unrelated other;
    speculo::ObjectRef not_multi(other);
    auto cast = speculo::class_of<Multi>().cast_to(not_multi, speculo::class_of<Root>());
    ASSERT_TRUE(cast.is_err());
    EXPECT_EQ(cast.unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    // The statically typed receiver is accepted.
    Multi multi;
    EXPECT_TRUE(speculo::class_of<Multi>().cast_to(speculo::ObjectRef(multi), speculo::class_of<Root>()).is_ok());
}

TEST(InheritanceTest, VirtualDiamondIsOnePath)
{
    DiamondBottom bottom;
    bottom.db = 31;
    speculo::ObjectRef ref(bottom);

    auto cast = speculo::class_of<DiamondBottom>().cast_to(ref, speculo::class_of<DiamondBase>());
    ASSERT_TRUE(cast.is_ok());
    EXPECT_EQ(cast.unwrap().raw_data(), static_cast<const void*>(static_cast<DiamondBase*>(&bottom)));
    EXPECT_EQ(cast.unwrap().as<DiamondBase>().unwrap().db, 31);
}

TEST(InheritanceTest, VirtualAndNonVirtualPathsStayAmbiguous)
{
    MixedBottom bottom;
    speculo::ObjectRef ref(bottom);

    auto cast = speculo::class_of<MixedBottom>().cast_to(ref, speculo::class_of<DiamondBase>());
    ASSERT_TRUE(cast.is_err());
    EXPECT_EQ(cast.unwrap_err().kind, speculo::ErrorKind::Ambiguous);
}

TEST(InheritanceTest, InaccessibleBaseIsNotAssignableAndCastSaysWhy)
{
    const speculo::Class& closed = speculo::class_of<Closed>();
    Closed object;
    speculo::ObjectRef ref(object);

    // The public base keeps working and agrees with cast_to.
    EXPECT_TRUE(speculo::class_of<Root>().is_assignable_from(closed));
    EXPECT_TRUE(closed.cast_to(ref, speculo::class_of<Root>()).is_ok());

    // Private / protected bases are in the graph but unusable: is_assignable_from
    // and cast_to agree, and cast_to names the access level instead of claiming
    // the type is not a base.
    for (const speculo::Class* base : { &speculo::class_of<HiddenBase>(), &speculo::class_of<ProtectedBase>() })
    {
        EXPECT_FALSE(base->is_assignable_from(closed));
        EXPECT_FALSE(base->is_instance(ref));

        auto cast = closed.cast_to(ref, *base);
        ASSERT_TRUE(cast.is_err());
        EXPECT_EQ(cast.unwrap_err().kind, speculo::ErrorKind::NotSupported);
    }

    // A type that is no base at all is still a TypeMismatch.
    auto unrelated = closed.cast_to(ref, speculo::class_of<Unrelated>());
    ASSERT_TRUE(unrelated.is_err());
    EXPECT_EQ(unrelated.unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_FALSE(speculo::class_of<Unrelated>().is_assignable_from(closed));
}

TEST(InheritanceTest, AssignableFromAndInstance)
{
    Multi m;
    m.r = 11;

    EXPECT_TRUE(speculo::class_of<Root>().is_assignable_from(speculo::class_of<Multi>()));
    EXPECT_TRUE(speculo::class_of<VRoot>().is_assignable_from(speculo::class_of<VLeaf>()));
    EXPECT_FALSE(speculo::class_of<Multi>().is_assignable_from(speculo::class_of<Root>()));
    EXPECT_TRUE(speculo::class_of<Multi>().is_assignable_from(speculo::class_of<Multi>()));

    speculo::ObjectRef ref(m);
    EXPECT_TRUE(speculo::class_of<Root>().is_instance(ref));
    EXPECT_TRUE(speculo::class_of<Multi>().is_instance(ref));
    EXPECT_FALSE(speculo::class_of<VLeaf>().is_instance(ref));
    EXPECT_FALSE(speculo::class_of<Root>().is_instance(speculo::ObjectRef{}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Recursive member lookup
// ─────────────────────────────────────────────────────────────────────────────

TEST(InheritedMemberTest, FindsMembersOfPublicBasesAndReachesThemThroughCastTo)
{
    const speculo::Class& multi = speculo::class_of<Multi>();
    Multi object;
    object.r = 11;
    object.o = 12;
    object.m = 13;
    speculo::ObjectRef ref(object);

    // get_fields() stays own-only; the recursive lookup also sees the bases.
    EXPECT_TRUE(multi.get_field("r").is_none());

    speculo::Field own = multi.get_field_recursive("m").unwrap();
    EXPECT_EQ(own.get_declaring_class(), multi);
    EXPECT_EQ(own.get(ref).unwrap().as<int>().unwrap(), 13);

    const auto read_through_base = [&](std::string_view name, const speculo::Class& owner, int expected)
    {
        speculo::Field field = multi.get_field_recursive(name).unwrap();
        EXPECT_EQ(field.get_declaring_class(), owner);

        auto base = multi.cast_to(ref, field.get_declaring_class());
        ASSERT_TRUE(base.is_ok());
        EXPECT_EQ(field.get(base.unwrap()).unwrap().as<int>().unwrap(), expected);
    };
    read_through_base("r", speculo::class_of<Root>(), 11);
    read_through_base("o", speculo::class_of<Other>(), 12);

    EXPECT_EQ(multi.get_field_recursive("nope").unwrap_err().kind, speculo::ErrorKind::NotFound);
}

TEST(InheritedMemberTest, ANearerDeclarationHidesTheBaseOne)
{
    const speculo::Class& derived = speculo::class_of<HideDerived>();

    EXPECT_EQ(derived.get_field_recursive("x").unwrap().get_declaring_class(), derived);
    EXPECT_EQ(derived.get_field_recursive("y").unwrap().get_declaring_class(), speculo::class_of<HideBase>());
    EXPECT_EQ(derived.get_method_recursive("f").unwrap().get_declaring_class(), speculo::class_of<HideBase>());
}

TEST(InheritedMemberTest, OverloadedNamesNeedTheirParameters)
{
    const speculo::Class& derived = speculo::class_of<OverDerived>();
    const speculo::Class& over = speculo::class_of<Over>();

    EXPECT_EQ(derived.get_method_recursive("g").unwrap_err().kind, speculo::ErrorKind::Ambiguous);

    speculo::Method nullary = derived.get_method_recursive("g", {}).unwrap();
    speculo::Method unary = derived.get_method_recursive("g", { &speculo::class_of<int>() }).unwrap();
    EXPECT_NE(nullary, unary);
    EXPECT_EQ(nullary.get_declaring_class(), over);
    EXPECT_EQ(derived.get_method_recursive("g", { &speculo::class_of<float>() }).unwrap_err().kind,
              speculo::ErrorKind::NotFound);

    // The parameter-typed lookup is not hidden by a derived overload with other parameters.
    const speculo::Class& hiding = speculo::class_of<OverHiding>();
    EXPECT_EQ(hiding.get_method_recursive("g", { &speculo::class_of<double>() }).unwrap().get_declaring_class(), hiding);
    EXPECT_EQ(hiding.get_method_recursive("g", { &speculo::class_of<int>() }).unwrap().get_declaring_class(), over);

    OverDerived object;
    speculo::ObjectRef ref(object);
    auto base = derived.cast_to(ref, nullary.get_declaring_class());
    ASSERT_TRUE(base.is_ok());
    EXPECT_EQ(nullary.invoke(base.unwrap(), {}).unwrap().as<int>().unwrap(), 1);
}

TEST(InheritedMemberTest, SiblingsAndSubobjects)
{
    // Two bases providing different members of one name.
    EXPECT_EQ(speculo::class_of<PP>().get_field_recursive("same").unwrap_err().kind, speculo::ErrorKind::Ambiguous);

    // A non-virtual diamond has two A subobjects: a non-static member is ambiguous...
    EXPECT_EQ(speculo::class_of<D>().get_field_recursive("a").unwrap_err().kind, speculo::ErrorKind::Ambiguous);
    EXPECT_EQ(speculo::class_of<SBottom>().get_field_recursive("nv").unwrap_err().kind, speculo::ErrorKind::Ambiguous);
    // ...a static one is the same function whichever subobject is meant.
    EXPECT_EQ(speculo::class_of<SBottom>().get_method_recursive("sv").unwrap().get_declaring_class(),
              speculo::class_of<SBase>());

    // A shared virtual base is a single subobject, and agrees with cast_to.
    speculo::Field shared = speculo::class_of<DiamondBottom>().get_field_recursive("db").unwrap();
    EXPECT_EQ(shared.get_declaring_class(), speculo::class_of<DiamondBase>());

    // One virtual and one plain path: two subobjects again.
    EXPECT_EQ(speculo::class_of<MixedBottom>().get_field_recursive("db").unwrap_err().kind,
              speculo::ErrorKind::Ambiguous);
}

TEST(InheritedMemberTest, InaccessibleBasesAreNotTraversed)
{
    const speculo::Class& closed = speculo::class_of<Closed>();

    EXPECT_EQ(closed.get_field_recursive("r").unwrap().get_declaring_class(), speculo::class_of<Root>());
    EXPECT_EQ(closed.get_field_recursive("hb").unwrap_err().kind, speculo::ErrorKind::NotFound);
    EXPECT_EQ(closed.get_field_recursive("pb").unwrap_err().kind, speculo::ErrorKind::NotFound);
}

// ─────────────────────────────────────────────────────────────────────────────
// Enumerators: get_enumerators / underlying type / three-way conversion
// ─────────────────────────────────────────────────────────────────────────────

TEST(EnumeratorTest, ScopedEnumMetadata)
{
    const speculo::Class& color = speculo::class_of<Color>();
    EXPECT_TRUE(color.is_enum());
    EXPECT_TRUE(color.is_scoped_enum());
    EXPECT_EQ(color.get_underlying_type().unwrap(), speculo::class_of<int>());

    ASSERT_EQ(color.get_enumerators().size(), 3u);
    EXPECT_EQ(color.get_enumerators()[0].get_name(), "Red");
    EXPECT_EQ(color.get_enumerators()[1].get_name(), "Green");
    EXPECT_EQ(color.get_enumerators()[2].get_name(), "Blue");
    EXPECT_EQ(color.get_enumerators()[0].to_int64(), 0);
    EXPECT_EQ(color.get_enumerators()[1].to_int64(), 5);
    EXPECT_EQ(color.get_enumerators()[2].to_int64(), 6);
    EXPECT_EQ(color.get_enumerators()[1].get_declaring_enum(), color);

    // Non-enums have no enumerators / underlying type.
    EXPECT_TRUE(speculo::class_of<int>().get_enumerators().empty());
    EXPECT_TRUE(speculo::class_of<int>().get_underlying_type().is_none());
}

TEST(EnumeratorTest, NameValueRoundTrip)
{
    const speculo::Class& color = speculo::class_of<Color>();

    auto by_name = color.enum_from_name("Green");
    ASSERT_TRUE(by_name.is_some());
    EXPECT_EQ(std::move(by_name).unwrap().as<Color>().unwrap(), Color::Green);

    auto by_value = color.enum_from_value(5);
    ASSERT_TRUE(by_value.is_some());
    EXPECT_EQ(std::move(by_value).unwrap().as<Color>().unwrap(), Color::Green);
    EXPECT_EQ(color.enum_from_value(0).unwrap().as<Color>().unwrap(), Color::Red);

    EXPECT_TRUE(color.enum_from_name("Purple").is_none());
    EXPECT_TRUE(color.enum_from_value(3).is_none());

    Color blue = Color::Blue;
    speculo::ObjectRef ref(blue);
    ASSERT_TRUE(color.enum_name_of(ref).is_some());
    EXPECT_EQ(color.enum_name_of(ref).unwrap(), "Blue");

    // Wrong type / non-enum value -> None.
    int not_an_enum = 0;
    EXPECT_TRUE(color.enum_name_of(speculo::ObjectRef(not_an_enum)).is_none());
    EXPECT_TRUE(speculo::class_of<int>().enum_name_of(ref).is_none());
}

TEST(EnumeratorTest, UnscopedAndUnsigned)
{
    const speculo::Class& legacy = speculo::class_of<Legacy>();
    EXPECT_FALSE(legacy.is_scoped_enum());
    EXPECT_TRUE(legacy.get_underlying_type().unwrap().is_fundamental());
    EXPECT_EQ(legacy.get_enumerators().size(), 2u);

    const speculo::Class& big = speculo::class_of<Big>();
    ASSERT_EQ(big.get_enumerators().size(), 1u);
    const speculo::Enumerator& huge = big.get_enumerators()[0];
    EXPECT_EQ(huge.to_uint64(), 0xFFFFFFFFFFFFFFFFULL);
    EXPECT_EQ(huge.to_int64(), static_cast<std::int64_t>(0xFFFFFFFFFFFFFFFFULL));

    // An unsigned value is looked up by its own (mathematical) value; a signed -1 is not it.
    auto recovered = big.enum_from_value(0xFFFFFFFFFFFFFFFFULL);
    ASSERT_TRUE(recovered.is_some());
    EXPECT_EQ(std::move(recovered).unwrap().as<Big>().unwrap(), Big::Huge);
    EXPECT_TRUE(big.enum_from_value(-1).is_none());
    EXPECT_TRUE(big.enum_from_value(static_cast<std::int64_t>(-1)).is_none());

    // value() boxes the underlying integer type (uint64 for unsigned underlying).
    auto boxed = huge.value();
    ASSERT_TRUE(boxed.is_ok());
    EXPECT_EQ(std::move(boxed).unwrap().as<std::uint64_t>().unwrap(), 0xFFFFFFFFFFFFFFFFULL);
}

TEST(EnumeratorTest, ValueIsBoxedAsTheUnderlyingType)
{
    // enum class Color (int): an int, not an int64.
    const speculo::Class& color = speculo::class_of<Color>();
    speculo::Object green = color.get_enumerators()[1].value().unwrap();
    EXPECT_EQ(green.type(), color.get_underlying_type().unwrap());
    EXPECT_EQ(green.type(), speculo::class_of<int>());
    EXPECT_EQ(green.as<int>().unwrap(), 5);
    EXPECT_TRUE(green.as<std::int64_t>().is_none());

    // Narrow and unsigned underlying types keep their width and signedness.
    const speculo::Class& narrow = speculo::class_of<Narrow>();
    speculo::Object negative = narrow.get_enumerators()[0].value().unwrap();
    EXPECT_EQ(negative.type(), speculo::class_of<short>());
    EXPECT_EQ(negative.as<short>().unwrap(), static_cast<short>(-3));

    const speculo::Class& small = speculo::class_of<Small>();
    speculo::Object high = small.get_enumerators()[1].value().unwrap();
    EXPECT_EQ(high.type(), speculo::class_of<unsigned char>());
    EXPECT_EQ(high.as<unsigned char>().unwrap(), static_cast<unsigned char>(255));

    // An unscoped enum reports whatever the compiler chose, consistently.
    const speculo::Class& legacy = speculo::class_of<Legacy>();
    EXPECT_EQ(legacy.get_enumerators()[0].value().unwrap().type(), legacy.get_underlying_type().unwrap());
}

TEST(EnumeratorTest, FromValueTakesAnyIntegerTypeByItsValue)
{
    const speculo::Class& narrow = speculo::class_of<Narrow>();
    EXPECT_EQ(narrow.enum_from_value(static_cast<short>(-3)).unwrap().as<Narrow>().unwrap(), Narrow::Neg);
    EXPECT_EQ(narrow.enum_from_value(-3).unwrap().as<Narrow>().unwrap(), Narrow::Neg);
    EXPECT_EQ(narrow.enum_from_value(-3LL).unwrap().as<Narrow>().unwrap(), Narrow::Neg);
    // The bit pattern of -3 as an unsigned value is not -3.
    EXPECT_TRUE(narrow.enum_from_value(static_cast<unsigned short>(0xFFFD)).is_none());
    EXPECT_TRUE(narrow.enum_from_value(0xFFFFFFFFFFFFFFFDULL).is_none());

    const speculo::Class& small = speculo::class_of<Small>();
    EXPECT_EQ(small.enum_from_value(static_cast<unsigned char>(255)).unwrap().as<Small>().unwrap(), Small::High);
    EXPECT_EQ(small.enum_from_value(255).unwrap().as<Small>().unwrap(), Small::High);
    EXPECT_EQ(small.enum_from_value(1u).unwrap().as<Small>().unwrap(), Small::Low);
    EXPECT_TRUE(small.enum_from_value(-1).is_none());
    EXPECT_TRUE(small.enum_from_value(256).is_none());
}

// ─────────────────────────────────────────────────────────────────────────────
// Array: static C arrays, multi-dimensional rank/extent, dynamic arrays
// ─────────────────────────────────────────────────────────────────────────────

TEST(ArrayTest, StaticArrayAccess)
{
    Arrays a;
    const speculo::Class& cls = speculo::class_of<Arrays>();

    speculo::ObjectRef arr = cls.get_field("v").unwrap().get_ref(a).unwrap();
    EXPECT_TRUE(arr.type().is_array());
    EXPECT_EQ(arr.type(), speculo::class_of<int[3]>());
    EXPECT_EQ(speculo::Array::get_length(arr).unwrap(), 3u);
    EXPECT_EQ(speculo::Array::get_element_type(arr).unwrap(), speculo::class_of<int>());

    EXPECT_EQ(speculo::Array::get(arr, 1).unwrap().as<int>().unwrap(), 2);

    int seven = 7;
    EXPECT_TRUE(speculo::Array::set(arr, 1, seven).is_ok());
    EXPECT_EQ(a.v[1], 7);

    auto element = speculo::Array::get_ref(arr, 2);
    ASSERT_TRUE(element.is_ok());
    EXPECT_EQ(element.unwrap().as<int>().unwrap(), 3);
    element.unwrap().as<int>().unwrap() = 9;
    EXPECT_EQ(a.v[2], 9);

    // Out of range / wrong element type.
    EXPECT_EQ(speculo::Array::get(arr, 5).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
    double wrong = 1.0;
    EXPECT_EQ(speculo::Array::set(arr, 0, wrong).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    // Non-array object.
    int scalar = 0;
    EXPECT_EQ(speculo::Array::get_length(speculo::ObjectRef(scalar)).unwrap_err().kind,
              speculo::ErrorKind::TypeMismatch);
}

TEST(ArrayTest, MultiDimensionalRankExtent)
{
    Arrays a;
    const speculo::Class& cls = speculo::class_of<Arrays>();

    speculo::ObjectRef matrix = cls.get_field("m").unwrap().get_ref(a).unwrap();
    EXPECT_EQ(matrix.type().get_array_rank(), 2u);
    EXPECT_EQ(matrix.type().get_array_extent().unwrap(), 2u);
    EXPECT_EQ(speculo::Array::get_length(matrix).unwrap(), 2u);

    auto row = speculo::Array::get_ref(matrix, 1);
    ASSERT_TRUE(row.is_ok());
    EXPECT_EQ(row.unwrap().type().get_array_rank(), 1u);
    EXPECT_EQ(row.unwrap().type().get_array_extent().unwrap(), 2u);
    EXPECT_EQ(speculo::Array::get_length(row.unwrap()).unwrap(), 2u);
    EXPECT_EQ(speculo::Array::get_element_type(row.unwrap()).unwrap(), speculo::class_of<int>());

    EXPECT_EQ(speculo::Array::get(row.unwrap(), 0).unwrap().as<int>().unwrap(), 3);
    EXPECT_EQ(speculo::Array::get(row.unwrap(), 1).unwrap().as<int>().unwrap(), 4);

    int nine = 9;
    EXPECT_TRUE(speculo::Array::set(row.unwrap(), 0, nine).is_ok());
    EXPECT_EQ(a.m[1][0], 9);

    // Multi-dim arrays decompose one level at a time from the Class API too.
    EXPECT_EQ(speculo::class_of<int[2][3]>().get_array_rank(), 2u);
    EXPECT_EQ(speculo::class_of<int[2][3]>().get_element_type().unwrap(), speculo::class_of<int[3]>());
}

TEST(ArrayTest, ConstElementArraysAreReadableThroughUnqualifiedHandles)
{
    static const int values[3] = { 4, 5, 6 };
    static_assert(std::is_const_v<std::remove_extent_t<decltype(values)>>);

    // A handle typed by the cv-qualified array class (element type is `const int`).
    speculo::ObjectRef arr(static_cast<const void*>(values), speculo::class_of<const int[3]>());
    EXPECT_EQ(speculo::Array::get_length(arr).unwrap(), 3u);
    EXPECT_EQ(speculo::Array::get_element_type(arr).unwrap(), speculo::class_of<const int>());

    // Values can be read by copy...
    auto copy = speculo::Array::get(arr, 1);
    ASSERT_TRUE(copy.is_ok());
    EXPECT_EQ(std::move(copy).unwrap().as<int>().unwrap(), 5);

    // ...and viewed: the element handle is keyed by `int` and marked const.
    auto element = speculo::Array::get_ref(arr, 2);
    ASSERT_TRUE(element.is_ok());
    EXPECT_TRUE(element.unwrap().is_const());
    EXPECT_EQ(element.unwrap().type(), speculo::class_of<int>());
    ASSERT_TRUE(element.unwrap().as<const int>().is_some());
    EXPECT_EQ(element.unwrap().as<const int>().unwrap(), 6);
    EXPECT_TRUE(element.unwrap().as<int>().is_none());

    // Const elements are read-only, not "unsupported".
    int one = 1;
    EXPECT_EQ(speculo::Array::set(arr, 0, one).unwrap_err().kind, speculo::ErrorKind::ReadOnly);
}

TEST(ArrayTest, VolatileElementsAreOnlyAccessibleThroughVolatileViews)
{
    volatile int values[3] = { 1, 2, 3 };
    speculo::ObjectRef arr(values);
    EXPECT_TRUE(arr.is_volatile());
    EXPECT_EQ(speculo::Array::get_length(arr).unwrap(), 3u);

    int nine = 9;
    EXPECT_EQ(speculo::Array::get(arr, 0).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(speculo::Array::set(arr, 0, nine).unwrap_err().kind, speculo::ErrorKind::NotSupported);

    auto element = speculo::Array::get_ref(arr, 1);
    ASSERT_TRUE(element.is_ok());
    EXPECT_TRUE(element.unwrap().is_volatile());
    EXPECT_TRUE(element.unwrap().as<int>().is_none());
    EXPECT_EQ(element.unwrap().as<volatile int>().unwrap(), 2);

    // Same when only the element type carries the qualifier.
    static volatile int device[2] = { 5, 6 };
    speculo::ObjectRef qualified(static_cast<void*>(const_cast<int*>(device)), speculo::class_of<volatile int[2]>());
    auto qualified_element = speculo::Array::get_ref(qualified, 0);
    ASSERT_TRUE(qualified_element.is_ok());
    EXPECT_TRUE(qualified_element.unwrap().is_volatile());
    EXPECT_EQ(speculo::Array::get(qualified, 0).unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(ArrayTest, DynamicArrayNewInstance)
{
    auto made = speculo::Array::new_instance(speculo::class_of<int>(), 4);
    ASSERT_TRUE(made.is_ok());
    speculo::Object dyn = std::move(made).unwrap();

    speculo::ObjectRef ref = dyn.ref();
    EXPECT_TRUE(ref.type().is_array());
    EXPECT_EQ(speculo::Array::get_length(ref).unwrap(), 4u);
    EXPECT_EQ(speculo::Array::get_element_type(ref).unwrap(), speculo::class_of<int>());

    int forty_two = 42;
    EXPECT_TRUE(speculo::Array::set(ref, 2, forty_two).is_ok());
    EXPECT_EQ(speculo::Array::get(ref, 2).unwrap().as<int>().unwrap(), 42);

    auto element = speculo::Array::get_ref(ref, 3);
    ASSERT_TRUE(element.is_ok());
    element.unwrap().as<int>().unwrap() = 7;
    EXPECT_EQ(speculo::Array::get(ref, 3).unwrap().as<int>().unwrap(), 7);

    // Zero length is valid.
    auto empty = speculo::Array::new_instance(speculo::class_of<int>(), 0);
    ASSERT_TRUE(empty.is_ok());
    EXPECT_EQ(speculo::Array::get_length(std::move(empty).unwrap().ref()).unwrap(), 0u);

    // Element types that are not default-constructible cannot back a dynamic array.
    auto bad = speculo::Array::new_instance(speculo::class_of<NoDefault>(), 3);
    ASSERT_TRUE(bad.is_err());
    EXPECT_EQ(bad.unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(speculo::Array::get_ref(ref, 9).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
}

namespace
{
struct alignas(64) OverAligned
{
    int v = 3;
};

struct SmallObject
{
    int v = 1;
};

struct PinnedElement
{
    int v = 0;
    PinnedElement() = default;
    PinnedElement(const PinnedElement&) = delete;
};
} // namespace

TEST(ArrayTest, DynamicArrayClassIsAFullDescriptor)
{
    auto ints = speculo::Array::new_instance(speculo::class_of<int>(), 3);
    auto smalls = speculo::Array::new_instance(speculo::class_of<SmallObject>(), 2);
    ASSERT_TRUE(ints.is_ok());
    ASSERT_TRUE(smalls.is_ok());
    speculo::Object int_array = std::move(ints).unwrap();
    speculo::Object small_array = std::move(smalls).unwrap();

    const speculo::Class& int_class = int_array.type();
    const speculo::Class& small_class = small_array.type();

    // A stable display name, not a registrable canonical one.
    EXPECT_EQ(int_class.get_name(), "int[]");
    EXPECT_EQ(int_class.get_qualified_name(), "int[]");
    EXPECT_NE(small_class.get_name(), "");
    EXPECT_TRUE(small_class.get_qualified_name().ends_with("SmallObject[]"));
    EXPECT_NE(int_class, small_class);
    EXPECT_FALSE(int_class.has_canonical_name());
    EXPECT_FALSE(small_class.has_canonical_name());

    // Nothing was registered under the empty (or any array) name.
    speculo::register_class(int_class);
    speculo::register_class(small_class);
    EXPECT_TRUE(speculo::Class::for_name("").is_none());
    // (`int[]` is also the spelling of the real unbounded array type, which may be
    // registered; the dynamic array class itself must not be.)
    auto by_array_name = speculo::Class::for_name("int[]");
    EXPECT_TRUE(by_array_name.is_none() || by_array_name.unwrap() != int_class);

    // It describes itself as an array of E.
    EXPECT_TRUE(int_class.is_array());
    EXPECT_EQ(int_class.get_element_type().unwrap(), speculo::class_of<int>());
    EXPECT_EQ(int_class.get_array_rank(), 1u);

    // The length is per object, never a property of the class.
    EXPECT_TRUE(int_class.get_array_extent().is_none());
    EXPECT_EQ(speculo::Array::get_length(int_array.ref()).unwrap(), 3u);
    EXPECT_EQ(speculo::class_of<int[3]>().get_array_extent().unwrap(), 3u);
}

TEST(ArrayTest, DynamicArrayCloneCopiesElements)
{
    auto made = speculo::Array::new_instance(speculo::class_of<SmallObject>(), 3);
    ASSERT_TRUE(made.is_ok());
    speculo::Object original = std::move(made).unwrap();
    speculo::ObjectRef original_ref = original.ref();
    SmallObject nine{ 9 };
    ASSERT_TRUE(speculo::Array::set(original_ref, 1, nine).is_ok());

    auto copied = original.clone();
    ASSERT_TRUE(copied.is_ok());
    speculo::Object copy = std::move(copied).unwrap();
    EXPECT_EQ(copy.type(), original.type());
    EXPECT_NE(copy.raw_data(), original.raw_data());
    EXPECT_EQ(speculo::Array::get_length(copy.ref()).unwrap(), 3u);
    EXPECT_EQ(std::move(speculo::Array::get(copy.ref(), 1)).unwrap().as<SmallObject>().unwrap().v, 9);
    EXPECT_EQ(std::move(speculo::Array::get(copy.ref(), 0)).unwrap().as<SmallObject>().unwrap().v, 1);

    // The copy is independent of the source.
    speculo::Array::get_ref(copy.ref(), 1).unwrap().as<SmallObject>().unwrap().v = 42;
    EXPECT_EQ(std::move(speculo::Array::get(original_ref, 1)).unwrap().as<SmallObject>().unwrap().v, 9);

    // ObjectRef::clone works the same way, including for empty arrays.
    EXPECT_TRUE(original_ref.clone().is_ok());
    auto empty = speculo::Array::new_instance(speculo::class_of<SmallObject>(), 0);
    ASSERT_TRUE(empty.is_ok());
    speculo::Object empty_array = std::move(empty).unwrap();
    auto empty_copy = empty_array.clone();
    ASSERT_TRUE(empty_copy.is_ok());
    EXPECT_EQ(speculo::Array::get_length(std::move(empty_copy).unwrap().ref()).unwrap(), 0u);

    // Elements that cannot be copied keep the array uncopyable.
    auto pinned = speculo::Array::new_instance(speculo::class_of<PinnedElement>(), 2);
    ASSERT_TRUE(pinned.is_ok());
    speculo::Object pinned_array = std::move(pinned).unwrap();
    EXPECT_EQ(pinned_array.clone().unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(ArrayTest, DynamicArrayOfTrackedElementsClonesAndDestroys)
{
    Tracked::alive = 0;
    {
        auto made = speculo::Array::new_instance(speculo::class_of<Tracked>(), 4);
        ASSERT_TRUE(made.is_ok());
        speculo::Object original = std::move(made).unwrap();
        EXPECT_EQ(Tracked::alive, 4);

        auto copied = original.clone();
        ASSERT_TRUE(copied.is_ok());
        speculo::Object copy = std::move(copied).unwrap();
        EXPECT_EQ(Tracked::alive, 8);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

TEST(ArrayTest, DynamicArrayHonorsOverAlignedElements)
{
    for (std::size_t count : { std::size_t{ 0 }, std::size_t{ 1 }, std::size_t{ 5 } })
    {
        auto made = speculo::Array::new_instance(speculo::class_of<OverAligned>(), count);
        ASSERT_TRUE(made.is_ok());
        speculo::Object dyn = std::move(made).unwrap();

        EXPECT_EQ(speculo::Array::get_length(dyn.ref()).unwrap(), count);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(dyn.raw_data()) % alignof(OverAligned), 0u);

        if (count > 0)
        {
            auto element = speculo::Array::get_ref(dyn.ref(), count - 1);
            ASSERT_TRUE(element.is_ok());
            EXPECT_EQ(element.unwrap().as<OverAligned>().unwrap().v, 3);
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(element.unwrap().raw_data()) % alignof(OverAligned), 0u);
        }
    }
}

TEST(ArrayTest, DynamicArrayRejectsOverflowingLength)
{
    constexpr std::size_t max = std::numeric_limits<std::size_t>::max();

    // count * sizeof(E) alone would wrap to a tiny allocation.
    auto wrapped = speculo::Array::new_instance(speculo::class_of<SmallObject>(), max / sizeof(SmallObject) + 1);
    ASSERT_TRUE(wrapped.is_err());
    EXPECT_EQ(wrapped.unwrap_err().kind, speculo::ErrorKind::InvalidArgument);

    // So would adding the header to an otherwise representable product.
    auto header = speculo::Array::new_instance(speculo::class_of<char>(), max);
    ASSERT_TRUE(header.is_err());
    EXPECT_EQ(header.unwrap_err().kind, speculo::ErrorKind::InvalidArgument);

    auto over_aligned = speculo::Array::new_instance(speculo::class_of<OverAligned>(), max / alignof(OverAligned));
    ASSERT_TRUE(over_aligned.is_err());
    EXPECT_EQ(over_aligned.unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
}

TEST(ArrayTest, DynamicArrayOfTrackedElements)
{
    // Verifies the header-based length and per-element destruction via a
    // non-trivial element type (leaks/double frees would surface here).
    Tracked::alive = 0;
    {
        auto made = speculo::Array::new_instance(speculo::class_of<Tracked>(), 3);
        ASSERT_TRUE(made.is_ok());
        speculo::Object dyn = std::move(made).unwrap();
        speculo::ObjectRef ref = dyn.ref();

        EXPECT_EQ(Tracked::alive, 3); // three default-constructed elements
        EXPECT_EQ(speculo::Array::get_length(ref).unwrap(), 3u);

        Tracked value(99);
        EXPECT_TRUE(speculo::Array::set(ref, 0, value).is_ok());
        EXPECT_EQ(speculo::Array::get(ref, 0).unwrap().as<Tracked>().unwrap().v, 99);
    }
    EXPECT_EQ(Tracked::alive, 0); // all elements destroyed by the dynamic array
}
