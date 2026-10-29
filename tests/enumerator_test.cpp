#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/type_fixtures.h"

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
