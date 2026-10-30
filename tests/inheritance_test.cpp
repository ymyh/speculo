#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/type_fixtures.h"

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
// cast_to matrix: target base × receiver constness
//
// 5 targets × 2 constnesses = 10 cells. Only public paths are reachable — a
// private or protected base has no cast thunk — and the receiver's constness is
// carried into the result.
// ─────────────────────────────────────────────────────────────────────────────

TEST(CastMatrix, TargetBaseByConstness)
{
    Closed       closed;
    const Closed const_closed{};

    struct Target
    {
        std::string_view      name;
        const speculo::Class* type = nullptr;
        // An inaccessible base is still a base: the cast is refused with
        // NotSupported (the access level), not with TypeMismatch.
        speculo::ErrorKind    expected_kind = speculo::ErrorKind::TypeMismatch;
        bool                  reachable = false;
    };

    const std::array<Target, 5> targets{{
        { "Root",          std::addressof(speculo::class_of<Root>()),          speculo::ErrorKind::TypeMismatch, true },
        { "HiddenBase",    std::addressof(speculo::class_of<HiddenBase>()),    speculo::ErrorKind::NotSupported, false },
        { "ProtectedBase", std::addressof(speculo::class_of<ProtectedBase>()), speculo::ErrorKind::NotSupported, false },
        { "self",          std::addressof(speculo::class_of<Closed>()),        speculo::ErrorKind::TypeMismatch, true },
        { "Unrelated",     std::addressof(speculo::class_of<Unrelated>()),     speculo::ErrorKind::TypeMismatch, false },
    }};

    constexpr std::array<bool, 2> kConstnesses{ false, true };

    for (const Target& target : targets)
    {
        for (const bool is_const : kConstnesses)
        {
            SCOPED_TRACE(std::format("{}/{}", target.name, is_const ? "const" : "mutable"));

            const speculo::ObjectRef receiver =
                is_const ? speculo::ObjectRef(const_closed) : speculo::ObjectRef(closed);
            auto cast = speculo::class_of<Closed>().cast_to(receiver, *target.type);

            if (target.reachable)
            {
                ASSERT_TRUE(cast.is_ok());
                const speculo::ObjectRef view = cast.unwrap();
                EXPECT_EQ(view.type(), *target.type);
                EXPECT_EQ(view.is_const(), is_const);
            }
            else
            {
                ASSERT_TRUE(cast.is_err());
                EXPECT_EQ(cast.unwrap_err().kind, target.expected_kind);
            }
        }
    }
}
