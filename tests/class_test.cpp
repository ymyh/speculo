#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/core_fixtures.h"
#include "support/class_fixtures.h"

TEST(ClassTest, FundamentalType)
{
    const speculo::Class& cls = speculo::class_of<int>();

    EXPECT_EQ(cls.get_name(), "int");
    EXPECT_EQ(cls.get_qualified_name(), "int");
    EXPECT_EQ(cls.get_size(), sizeof(int));
    EXPECT_EQ(cls.get_alignment(), alignof(int));
    EXPECT_EQ(cls.kind(), speculo::TypeKind::Fundamental);
    EXPECT_TRUE(cls.is_fundamental());
    EXPECT_FALSE(cls.is_class());

    EXPECT_EQ(cls, speculo::class_of<int>());
    EXPECT_NE(cls, speculo::class_of<double>());
    // Phase 5: class_of<T> reflects the exact type -- cv and references are not
    // normalized, so const int / int& are distinct handles.
    EXPECT_NE(speculo::class_of<const int>(), cls);
    EXPECT_NE(speculo::class_of<int&>(), cls);
    EXPECT_NE(speculo::class_of<int&&>(), cls);
    EXPECT_TRUE(speculo::class_of<const int>().is_const_qualified());
    EXPECT_EQ(speculo::type_of<int>(), cls);
}

TEST(ClassTest, ClassTypeTraits)
{
    const speculo::Class& cls = speculo::class_of<Widget>();

    EXPECT_EQ(cls.get_name(), "Widget");
    EXPECT_EQ(cls.get_size(), sizeof(Widget));
    EXPECT_TRUE(cls.is_class());
    EXPECT_TRUE(cls.is_polymorphic());
    EXPECT_FALSE(cls.is_abstract());
    EXPECT_TRUE(cls.modifiers().is_public());
}

TEST(ClassTest, PointerAndArrayKinds)
{
    EXPECT_TRUE(speculo::class_of<int*>().is_pointer());
    EXPECT_TRUE(speculo::class_of<int[4]>().is_array());
    EXPECT_TRUE(speculo::class_of<void>().is_void());
    EXPECT_TRUE(speculo::class_of<int Widget::*>().is_member_pointer());
}

TEST(ClassTest, PointIdentityAndSize)
{
    const speculo::Class& cls = speculo::class_of<Point>();

    EXPECT_EQ(cls.get_name(), "Point");
    EXPECT_EQ(cls.get_size(), sizeof(Point));
    EXPECT_EQ(cls.get_alignment(), alignof(Point));
    EXPECT_TRUE(cls.is_class());
    EXPECT_TRUE(cls.is_aggregate());
    EXPECT_FALSE(cls.is_polymorphic());
    // Phase 5: the exact type is kept -- const Point& is a distinct Class.
    EXPECT_NE(cls, speculo::class_of<const Point&>());
    EXPECT_EQ(speculo::class_of<Point&>().get_referenced_type().unwrap(), cls);
    EXPECT_EQ(speculo::class_of<const Point&>().get_referenced_type().unwrap(), speculo::class_of<const Point>());
}

TEST(ClassTest, PublicFieldsAndMethods)
{
    const speculo::Class& cls = speculo::class_of<Point>();

    EXPECT_EQ(cls.get_fields().size(), 2u);
    ASSERT_TRUE(cls.get_field("x").is_some());
    ASSERT_TRUE(cls.get_field("y").is_some());
    EXPECT_TRUE(cls.get_field("z").is_none());

    EXPECT_EQ(cls.get_methods().size(), 1u);
    ASSERT_TRUE(cls.get_method("norm").is_ok());
    EXPECT_TRUE(cls.get_method("missing").is_err());
}

TEST(Phase3Example, MinimalApiDesignExample)
{
    const speculo::Class& cls = speculo::class_of<Point>();

    speculo::Field fx = cls.get_field("x").unwrap();
    speculo::Method fn = cls.get_method("norm").unwrap();

    Point p{3, 4};
    int ten = 10;
    ASSERT_TRUE(fx.set(p, ten).is_ok());

    auto invoked = fn.invoke(p, {});
    ASSERT_TRUE(invoked.is_ok());
    speculo::Object r = std::move(invoked).unwrap();
    double v = r.as<double>().unwrap();
    EXPECT_DOUBLE_EQ(v, 14.0);
}

TEST(InheritanceTest, SingleBaseNavigation)
{
    const speculo::Class& leaf = speculo::class_of<Leaf>();

    ASSERT_EQ(leaf.get_bases().size(), 1u);
    ASSERT_TRUE(leaf.get_superclass().is_some());
    EXPECT_EQ(leaf.get_superclass().unwrap(), speculo::class_of<Root>());

    // get_fields()/get_methods() are own-class only: inherited members are
    // reached by navigating to the base first.
    EXPECT_EQ(leaf.get_fields().size(), 1u);
    EXPECT_TRUE(leaf.get_field("r").is_none());
    EXPECT_TRUE(leaf.get_method("root_method").is_err());

    const speculo::Class& root = leaf.get_superclass().unwrap();
    ASSERT_TRUE(root.get_field("r").is_some());
    ASSERT_TRUE(root.get_method("root_method").is_ok());

    Root root_obj;
    root_obj.r = 9;
    EXPECT_EQ(root.get_field("r").unwrap().get(root_obj).unwrap().as<int>().unwrap(), 9);
    EXPECT_EQ(root.get_method("root_method").unwrap().invoke(root_obj, {}).unwrap().as<int>().unwrap(), 9);
}

TEST(InheritanceTest, NoBaseAndMultipleBases)
{
    EXPECT_TRUE(speculo::class_of<Root>().get_bases().empty());
    EXPECT_TRUE(speculo::class_of<Root>().get_superclass().is_none());

    const speculo::Class& multi = speculo::class_of<Multi>();
    ASSERT_EQ(multi.get_bases().size(), 2u);
    EXPECT_TRUE(multi.get_superclass().is_none()); // ambiguous: no unique superclass
    EXPECT_EQ(multi.get_bases()[0].type, std::addressof(speculo::class_of<Root>()));
    EXPECT_EQ(multi.get_bases()[1].type, std::addressof(speculo::class_of<Other>()));
    EXPECT_TRUE(multi.get_bases()[0].modifiers.is_public());
}

TEST(InheritanceTest, VirtualBaseIsNotSuperclass)
{
    const speculo::Class& vleaf = speculo::class_of<VLeaf>();
    ASSERT_EQ(vleaf.get_bases().size(), 1u);
    EXPECT_TRUE(vleaf.get_bases()[0].is_virtual());
    EXPECT_TRUE(vleaf.get_superclass().is_none());
}

TEST(InheritanceTest, GetSpecificBase)
{
    const speculo::Class& multi = speculo::class_of<Multi>();

    ASSERT_TRUE(multi.get_base<Root>().is_some());
    EXPECT_EQ(multi.get_base<Root>().unwrap(), speculo::class_of<Root>());
    EXPECT_EQ(multi.get_base<Other>().unwrap(), speculo::class_of<Other>());

    // Leaf is unrelated to Multi -> None.
    EXPECT_TRUE(multi.get_base<Leaf>().is_none());

    // By Class handle.
    EXPECT_EQ(multi.get_base(speculo::class_of<Other>()).unwrap(), speculo::class_of<Other>());
    EXPECT_TRUE(multi.get_base(speculo::class_of<Leaf>()).is_none());
}
