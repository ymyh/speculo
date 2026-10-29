#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/registry_fixtures.h"

TEST(StdLibraryTest, StructWithStringMembersIsReflectable)
{
    const speculo::Class& cls = speculo::class_of<p6c::Person>();
    EXPECT_EQ(cls.get_fields().size(), 3u);

    p6c::Person person;
    person.name = "Ada";

    const auto name = cls.get_field("name").unwrap();
    EXPECT_EQ(name.get_type(), speculo::class_of<std::string>());
    EXPECT_EQ(name.get(person).unwrap().as<std::string>().unwrap(), "Ada");

    std::string grace = "Grace";
    ASSERT_TRUE(name.set(person, grace).is_ok());
    EXPECT_EQ(person.name, "Grace");

    auto name_ref = name.get_ref(person);
    ASSERT_TRUE(name_ref.is_ok());
    EXPECT_EQ(name_ref.unwrap().type(), speculo::class_of<std::string>());
    name_ref.unwrap().as<std::string>().unwrap() = "Lin";
    EXPECT_EQ(person.name, "Lin");

    person.tags = { "a", "b" };
    const auto tags = cls.get_field("tags").unwrap();
    EXPECT_EQ(tags.get_type(), speculo::class_of<std::vector<std::string>>());
    EXPECT_EQ(tags.get(person).unwrap().as<std::vector<std::string>>().unwrap().size(), 2u);
    std::vector<std::string> renamed{ "x" };
    ASSERT_TRUE(tags.set(person, renamed).is_ok());
    EXPECT_EQ(person.tags, renamed);

    const auto greet = cls.get_method("greet").unwrap();
    std::string hello = "Hello, ";
    EXPECT_EQ(greet.invoke(person, { hello }).unwrap().as<std::string>().unwrap(), "Hello, Lin");
}

TEST(StdLibraryTest, StdTypesKeepIdentityWithoutMembers)
{
    // A standard type is still a real Class (so a field or parameter can name it,
    // and Object can copy and destroy a value), but its own members are not expanded.
    const speculo::Class& text = speculo::class_of<std::string>();
    EXPECT_EQ(text.get_size(), sizeof(std::string));
    EXPECT_EQ(text.get_alignment(), alignof(std::string));
    EXPECT_FALSE(text.get_qualified_name().empty());
    EXPECT_TRUE(text.get_fields().empty());
    EXPECT_TRUE(text.get_declared_fields().empty());
    EXPECT_TRUE(text.get_methods().empty());
    EXPECT_TRUE(text.get_declared_methods().empty());
    EXPECT_TRUE(text.get_constructors().empty());
    EXPECT_TRUE(text.get_declared_constructors().empty());
    EXPECT_TRUE(text.get_bases().empty());

    std::string value = "reflection";
    const speculo::Object boxed = speculo::Object::from_value(value).unwrap();
    EXPECT_EQ(boxed.as<std::string>().unwrap(), "reflection");

    const speculo::Class& vec = speculo::class_of<std::vector<std::string>>();
    EXPECT_TRUE(vec.get_methods().empty());
    EXPECT_TRUE(vec.get_fields().empty());
    EXPECT_EQ(vec.get_size(), sizeof(std::vector<std::string>));

    const speculo::Class& order = speculo::class_of<std::endian>();
    EXPECT_TRUE(order.is_enum());
    EXPECT_TRUE(order.get_enumerators().empty());
    EXPECT_EQ(order.get_size(), sizeof(std::endian));
}

TEST(StdLibraryTest, AnonymousUnionMembersAreReflectable)
{
    // The anonymous union is one unnamed field of the enclosing class. Its members
    // are not injected into that class; they are fields of the union type.
    const speculo::Class& cls = speculo::class_of<p6c::Anon>();
    EXPECT_EQ(cls.get_fields().size(), 2u);
    EXPECT_EQ(cls.get_field("tag").unwrap().get_offset(), 0u);
    EXPECT_TRUE(cls.get_field("as_int").is_none());
    EXPECT_TRUE(cls.get_field("as_float").is_none());

    speculo::Option<speculo::Field> union_field = speculo::None{};
    for (const speculo::Field& field : cls.get_fields())
    {
        if (field.get_type().is_union())
        {
            union_field = field;
            break;
        }
    }
    ASSERT_TRUE(union_field.is_some());

    const speculo::Field anon = union_field.unwrap();
    EXPECT_FALSE(anon.get_name().empty());
    EXPECT_EQ(anon.get_offset(), sizeof(int));

    const speculo::Class& union_type = anon.get_type();
    EXPECT_TRUE(union_type.is_union());
    EXPECT_FALSE(union_type.is_class());
    EXPECT_EQ(union_type.get_fields().size(), 2u);

    const speculo::Field as_int = union_type.get_field("as_int").unwrap();
    const speculo::Field as_float = union_type.get_field("as_float").unwrap();
    EXPECT_EQ(as_int.get_declaring_class(), union_type);
    EXPECT_EQ(as_int.get_type(), speculo::class_of<int>());
    EXPECT_EQ(as_float.get_type(), speculo::class_of<float>());
    EXPECT_EQ(as_int.get_offset(), 0u);
    EXPECT_EQ(as_float.get_offset(), 0u);

    p6c::Anon value{};
    value.as_int = 7;
    const speculo::ObjectRef sub = anon.get_ref(value).unwrap();
    EXPECT_EQ(sub.type(), union_type);
    EXPECT_EQ(as_int.get(sub).unwrap().as<int>().unwrap(), 7);
}
