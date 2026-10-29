#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/instance_fixtures.h"

TEST(ConstCorrectnessTest, MethodInvocation)
{
    const speculo::Class& cls = speculo::class_of<Widget>();
    speculo::Method get = cls.get_method("get").unwrap();
    speculo::Method set = cls.get_method("set").unwrap();

    Widget mutable_object;
    const Widget const_object{};

    EXPECT_TRUE(get.is_const());
    EXPECT_FALSE(set.is_const());

    // const method on a mutable or const object -> OK.
    EXPECT_EQ(get.invoke(mutable_object, {}).unwrap().as<int>().unwrap(), 1);
    EXPECT_EQ(get.invoke(const_object, {}).unwrap().as<int>().unwrap(), 1);

    // non-const method on a const object -> ReadOnly.
    int five = 5;
    EXPECT_EQ(set.invoke(const_object, { five }).unwrap_err().kind, speculo::ErrorKind::ReadOnly);
    EXPECT_TRUE(set.invoke(mutable_object, { five }).is_ok());
    EXPECT_EQ(mutable_object.v, 5);
}

TEST(ConstCorrectnessTest, ObjectRefConstness)
{
    Widget mutable_object;
    const Widget const_object{};

    speculo::ObjectRef mutable_ref(mutable_object);
    EXPECT_FALSE(mutable_ref.is_const());
    EXPECT_TRUE(mutable_ref.as<Widget>().is_some());

    // A view of a const object: non-const `as` is refused...
    speculo::ObjectRef const_ref(const_object);
    EXPECT_TRUE(const_ref.is_const());
    EXPECT_TRUE(const_ref.as<Widget>().is_none());

    // ...while a const-qualified handle yields a const view.
    const speculo::ObjectRef readonly_ref = const_object;
    EXPECT_TRUE(readonly_ref.as<Widget>().is_some());
}

TEST(ConstCorrectnessTest, ConstQualifiedAsReadsConstObjectThroughNonConstHandle)
{
    Widget mutable_object;
    const Widget const_object{};

    speculo::ObjectRef const_ref(const_object);
    auto read = const_ref.as<const Widget>();
    ASSERT_TRUE(read.is_some());
    EXPECT_EQ(&read.unwrap(), &const_object);
    static_assert(std::is_same_v<decltype(read.unwrap()), const Widget&>);

    // Asking for a writable T from a const view still fails.
    EXPECT_TRUE(const_ref.as<Widget>().is_none());

    // A mutable view can still be read as const, and as writable.
    speculo::ObjectRef mutable_ref(mutable_object);
    EXPECT_TRUE(mutable_ref.as<const Widget>().is_some());
    EXPECT_TRUE(mutable_ref.as<Widget>().is_some());

    // A type mismatch is still None.
    EXPECT_TRUE(const_ref.as<const int>().is_none());
}

TEST(ConstCorrectnessTest, RawDataOfNonConstHandleIsReadOnlyPointer)
{
    const Widget const_object{};
    speculo::ObjectRef const_ref(const_object);

    static_assert(std::is_same_v<decltype(const_ref.raw_data()), const void*>);
    EXPECT_EQ(const_ref.raw_data(), static_cast<const void*>(&const_object));
}

TEST(ConstCorrectnessTest, FieldAccess)
{
    const speculo::Class& cls = speculo::class_of<Widget>();
    speculo::Field field = cls.get_field("v").unwrap();

    Widget mutable_object;
    const Widget const_object{};
    int nine = 9;

    EXPECT_EQ(field.set(const_object, nine).unwrap_err().kind, speculo::ErrorKind::ReadOnly);
    EXPECT_TRUE(field.set(mutable_object, nine).is_ok());
    EXPECT_EQ(mutable_object.v, 9);

    // get_ref() honours the object's constness.
    auto mutable_view = field.get_ref(mutable_object);
    ASSERT_TRUE(mutable_view.is_ok());
    EXPECT_FALSE(mutable_view.unwrap().is_const());
    EXPECT_TRUE(mutable_view.unwrap().as<int>().is_some());

    auto const_view = field.get_ref(const_object);
    ASSERT_TRUE(const_view.is_ok());
    EXPECT_TRUE(const_view.unwrap().is_const());
    EXPECT_TRUE(const_view.unwrap().as<int>().is_none());
    const speculo::ObjectRef readonly_view = const_view.unwrap();
    EXPECT_TRUE(readonly_view.as<int>().is_some());
}
