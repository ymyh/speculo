#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/registry_fixtures.h"

TEST(AnnotationTest, ClassFieldMethodAndParameter)
{
    const speculo::Class& cls = speculo::class_of<p6ann::Derived>();
    EXPECT_EQ(cls.get_annotation<p6ann::Label>().unwrap().rank, 9);
    EXPECT_TRUE(cls.get_annotation<p6ann::Tag>().is_none());

    const speculo::Class& as_const = speculo::class_of<const p6ann::Derived>();
    EXPECT_EQ(as_const.get_annotation<p6ann::Label>().unwrap().rank, 9);
    EXPECT_TRUE(speculo::class_of<p6ann::Derived&>().get_annotations().empty());
    EXPECT_TRUE(speculo::class_of<p6ann::Derived*>().get_annotations().empty());

    const speculo::Field x = cls.get_field("x").unwrap();
    EXPECT_EQ(x.get_annotations().size(), 3u);
    EXPECT_EQ(x.get_annotation<p6ann::Label>().unwrap().rank, 3);
    EXPECT_EQ(x.get_annotation<int>().unwrap(), 1);
    EXPECT_EQ(x.get_annotations()[0].get_type(), speculo::class_of<p6ann::Label>());
    EXPECT_EQ(x.get_annotations()[1].get<int>().unwrap(), 1);
    EXPECT_EQ(x.get_annotations()[2].get<int>().unwrap(), 2);

    const speculo::Object boxed = x.get_annotations()[1].value().unwrap();
    EXPECT_EQ(boxed.as<int>().unwrap(), 1);

    const speculo::Method scale = cls.get_method("scale").unwrap();
    EXPECT_TRUE(scale.get_annotations().empty());
    EXPECT_TRUE(scale.get_parameter_annotation<p6ann::Tag>(0).is_some());

    const speculo::Method plain = cls.get_method("plain").unwrap();
    EXPECT_TRUE(plain.get_annotations().empty());

    const speculo::Constructor ctor = cls.get_constructor().unwrap();
    EXPECT_TRUE(ctor.get_annotation<p6ann::Tag>().is_some());
}

TEST(AnnotationTest, UnreadablePointerValuesStayListed)
{
    const speculo::Class& cls = speculo::class_of<p6ann::Derived>();

    const speculo::Field text = cls.get_field("text").unwrap();
    ASSERT_EQ(text.get_annotations().size(), 1u);
    const speculo::Annotation& literal = text.get_annotations()[0];
    EXPECT_EQ(literal.get_type(), speculo::class_of<const char*>());
    EXPECT_FALSE(literal.is_readable());
    EXPECT_TRUE(literal.get<const char*>().is_none());
    EXPECT_EQ(literal.value().unwrap_err().kind, speculo::ErrorKind::NotSupported);

    const speculo::Field renamed = cls.get_field("renamed").unwrap();
    const speculo::Annotation& rename = renamed.get_annotations()[0];
    EXPECT_EQ(rename.get_type(), speculo::class_of<p6ann::Rename>());
    EXPECT_FALSE(rename.is_readable());
    EXPECT_TRUE(rename.get<p6ann::Rename>().is_none());

    const speculo::Field chars = cls.get_field("chars").unwrap();
    const speculo::Annotation& array = chars.get_annotations()[0];
    EXPECT_TRUE(array.is_readable());
    const std::array<char, 8>& stored = array.get<std::array<char, 8>>().unwrap();
    EXPECT_EQ(stored[0], 'l');
    const speculo::Object boxed = array.value().unwrap();
    EXPECT_EQ((boxed.as<std::array<char, 8>>().unwrap()[0]), 'l');
}

TEST(AnnotationTest, BasesEnumeratorsAndNoInheritance)
{
    const speculo::Class& derived = speculo::class_of<p6ann::Derived>();
    const speculo::BaseClass& base = derived.get_bases()[0];
    EXPECT_EQ(base.type, &speculo::class_of<p6ann::Base>());
    EXPECT_TRUE(base.get_annotation<p6ann::Tag>().is_some());
    EXPECT_TRUE(speculo::class_of<p6ann::Base>().get_annotation<p6ann::Tag>().is_some());
    EXPECT_TRUE(derived.get_annotation<p6ann::Tag>().is_none());

    const speculo::Class& color = speculo::class_of<p6ann::Color>();
    EXPECT_TRUE(color.get_annotations().empty());
    const speculo::Enumerator red = color.get_enumerators()[0];
    EXPECT_EQ(red.get_name(), "Red");
    EXPECT_EQ(red.get_annotation<int>().unwrap(), 7);
    EXPECT_TRUE(color.get_enumerators()[1].get_annotations().empty());
}
