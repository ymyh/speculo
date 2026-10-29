#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/instance_fixtures.h"

TEST(ModifierTest, BitsPredicatesAndString)
{
    using speculo::Modifier;

    constexpr Modifier m = Modifier(Modifier::PUBLIC | Modifier::STATIC | Modifier::NOEXCEPT);
    static_assert(m.is_public());
    static_assert(m.is_static());
    static_assert(m.is_noexcept());
    static_assert(!m.is_private());

    EXPECT_EQ(m.bits(), Modifier::PUBLIC | Modifier::STATIC | Modifier::NOEXCEPT);
    EXPECT_EQ(m.to_string(), "public static noexcept");
    EXPECT_EQ(m, Modifier(Modifier::PUBLIC | Modifier::STATIC | Modifier::NOEXCEPT));
    EXPECT_NE(m, Modifier(Modifier::PRIVATE));
    EXPECT_EQ(Modifier().to_string(), "");
    EXPECT_EQ((Modifier(Modifier::PUBLIC) | Modifier(Modifier::CONST)).to_string(), "public const");
}

TEST(ModifierTest, InterfaceIsAbstractStatelessAndAllPureVirtual)
{
    EXPECT_TRUE(speculo::class_of<IShape>().modifiers().is_interface());
    EXPECT_TRUE(speculo::class_of<IShape>().modifiers().is_abstract());
    EXPECT_TRUE(speculo::class_of<IShape>().modifiers().to_string().contains("interface"));

    // An interface that extends an interface is still one.
    EXPECT_TRUE(speculo::class_of<IRound>().modifiers().is_interface());

    // Data members, a concrete virtual function or a stateful base all disqualify.
    EXPECT_TRUE(speculo::class_of<AbstractWithState>().modifiers().is_abstract());
    EXPECT_FALSE(speculo::class_of<AbstractWithState>().modifiers().is_interface());
    EXPECT_TRUE(speculo::class_of<AbstractWithBody>().modifiers().is_abstract());
    EXPECT_FALSE(speculo::class_of<AbstractWithBody>().modifiers().is_interface());
    EXPECT_FALSE(speculo::class_of<DerivedFromState>().modifiers().is_interface());

    // Concrete and non-class types are not interfaces.
    EXPECT_FALSE(speculo::class_of<Vec>().modifiers().is_interface());
    EXPECT_FALSE(speculo::class_of<int>().modifiers().is_interface());
}

TEST(ModifierTest, FinalOnVirtualFunctions)
{
    const speculo::Class& base = speculo::class_of<Overrides>();
    const speculo::Class& derived = speculo::class_of<OverridesDerived>();

    for (std::string_view name : { "plain", "locked", "sealed" })
    {
        EXPECT_FALSE(base.get_declared_method(name).unwrap().modifiers().is_final()) << name;
    }

    EXPECT_FALSE(derived.get_declared_method("plain").unwrap().modifiers().is_final());
    EXPECT_TRUE(derived.get_declared_method("locked").unwrap().modifiers().is_final());
    EXPECT_TRUE(derived.get_declared_method("sealed").unwrap().modifiers().is_final());
    EXPECT_TRUE(derived.get_declared_method("sealed").unwrap().is_override());

    // Non-virtual functions never carry it.
    EXPECT_FALSE(speculo::class_of<Mixed>().get_method("only").unwrap().modifiers().is_final());
}

TEST(ModifierTest, ClassAccessFollowsNesting)
{
    EXPECT_TRUE(speculo::class_of<Vec>().modifiers().is_public());
    EXPECT_TRUE(speculo::class_of<Outer>().modifiers().is_public());
    EXPECT_TRUE(speculo::class_of<Outer::Open>().modifiers().is_public());

    const speculo::Modifier hidden = outer_private_class().modifiers();
    EXPECT_TRUE(hidden.is_private());
    EXPECT_FALSE(hidden.is_public());

    const speculo::Modifier guarded = outer_protected_class().modifiers();
    EXPECT_TRUE(guarded.is_protected());
    EXPECT_FALSE(guarded.is_public());
}
