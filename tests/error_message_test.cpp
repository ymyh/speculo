#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/registry_fixtures.h"

namespace
{

auto contains_all(const std::string& text, std::initializer_list<std::string_view> parts) -> bool
{
    for (std::string_view part : parts)
    {
        if (!text.contains(part))
        {
            return false;
        }
    }

    return true;
}

} // namespace

TEST(ErrorMessageTest, FieldSetNamesMemberAndBothTypes)
{
    const auto field = speculo::class_of<p6::Point>().get_field("x").unwrap();
    p6::Point point{};
    double wrong = 1.0;

    const speculo::Error error = field.set(point, wrong).unwrap_err();
    EXPECT_EQ(error.kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_TRUE(contains_all(error.message, { "Field::set", "p6::Point::x", "double", "int" })) << error.message;
}

TEST(ErrorMessageTest, ReceiverMismatchNamesBothClasses)
{
    const auto field = speculo::class_of<p6::Point>().get_field("x").unwrap();
    p6::Base other{};

    const speculo::Error error = field.get(other).unwrap_err();
    EXPECT_EQ(error.kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_TRUE(contains_all(error.message, { "Field::get", "p6::Base", "p6::Point" })) << error.message;
}

TEST(ErrorMessageTest, MethodArgumentMismatchNamesIndexAndTypes)
{
    const auto method = speculo::class_of<p6::Point>().get_method("scale").unwrap();
    p6::Point point{};

    double wrong = 2.0;
    const speculo::Error mismatch = method.invoke(point, { wrong }).unwrap_err();
    EXPECT_EQ(mismatch.kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_TRUE(contains_all(mismatch.message, { "Method::invoke", "scale", "argument 0", "double", "int" }))
        << mismatch.message;

    int one = 1;
    const speculo::Error count = method.invoke(point, { one, one }).unwrap_err();
    EXPECT_EQ(count.kind, speculo::ErrorKind::InvalidArgument);
    EXPECT_TRUE(contains_all(count.message, { "scale", "expected 1", "got 2" })) << count.message;
}

TEST(ErrorMessageTest, ConstructorMismatchNamesTheSignature)
{
    const auto ctor = speculo::class_of<p6::Point>().get_constructor<int, int>().unwrap();
    int one = 1;
    double wrong = 1.0;

    const speculo::Error error = ctor.new_instance({ one, wrong }).unwrap_err();
    EXPECT_EQ(error.kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_TRUE(contains_all(error.message, { "Constructor::new_instance", "argument 1", "double", "int" }))
        << error.message;
}

TEST(ErrorMessageTest, LookupFailuresNameTheClassAndTheMember)
{
    const auto& cls = speculo::class_of<p6::Point>();

    const speculo::Error no_method = cls.get_method("nope").unwrap_err();
    EXPECT_EQ(no_method.kind, speculo::ErrorKind::NotFound);
    EXPECT_TRUE(contains_all(no_method.message, { "p6::Point", "nope" })) << no_method.message;

    const speculo::Error no_field = cls.get_field_recursive("nope").unwrap_err();
    EXPECT_EQ(no_field.kind, speculo::ErrorKind::NotFound);
    EXPECT_TRUE(contains_all(no_field.message, { "p6::Point" })) << no_field.message;

    char one = 1;
    const speculo::Error no_ctor = cls.new_instance({ one }).unwrap_err();
    EXPECT_EQ(no_ctor.kind, speculo::ErrorKind::NotFound);
    EXPECT_TRUE(contains_all(no_ctor.message, { "p6::Point", "(char)" })) << no_ctor.message;
}

TEST(ErrorMessageTest, CastAndArrayErrorsCarryTheirOperands)
{
    p6::Point point{};
    const speculo::Error cast =
        speculo::class_of<p6::Point>().cast_to(point, speculo::class_of<p6::Base>()).unwrap_err();
    EXPECT_EQ(cast.kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_TRUE(contains_all(cast.message, { "cast_to", "p6::Point", "p6::Base" })) << cast.message;

    speculo::Object array = std::move(speculo::Array::new_instance(speculo::class_of<int>(), 3).unwrap());
    const speculo::Error range = speculo::Array::get(array.ref(), 5).unwrap_err();
    EXPECT_EQ(range.kind, speculo::ErrorKind::InvalidArgument);
    EXPECT_TRUE(contains_all(range.message, { "index 5", "length 3" })) << range.message;
}

TEST(ErrorMessageTest, ErrorHasAFormatter)
{
    const speculo::Error error{ speculo::ErrorKind::NotFound, "nothing here" };
    EXPECT_EQ(std::format("{}", error), "NotFound: nothing here");
    EXPECT_EQ(std::format("{}", speculo::ErrorKind::ReadOnly), "ReadOnly");
    EXPECT_EQ(std::format("[{:>10}]", speculo::ErrorKind::Ambiguous), "[ Ambiguous]");
}
