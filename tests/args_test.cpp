#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/instance_fixtures.h"
#include "support/registry_fixtures.h"

TEST(ArgsTest, EmptyInlineAndHeap)
{
    speculo::Args empty;
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty.size(), 0u);
    EXPECT_EQ(empty.span().size(), 0u);

    int a = 1;
    int b = 2;
    int c = 3;
    speculo::Args small(a, b, c);
    EXPECT_EQ(small.size(), 3u);
    const auto small_span = small.span();
    ASSERT_EQ(small_span.size(), 3u);
    EXPECT_EQ(small_span[0].type().get_name(), "int");
    EXPECT_EQ(small_span[2].as<int>().unwrap(), 3);

    int values[10] = {};
    speculo::Args big(values[0], values[1], values[2], values[3], values[4],
                      values[5], values[6], values[7], values[8], values[9]);
    EXPECT_EQ(big.size(), 10u);
    EXPECT_EQ(big.span().size(), 10u);
    EXPECT_EQ(big[9].type().get_name(), "int");

    speculo::Args moved(std::move(big));
    EXPECT_EQ(moved.size(), 10u);
    EXPECT_EQ(big.size(), 0u);
}

TEST(ArgsTest, InitializerListAndObject)
{
    int a = 1;
    int b = 2;
    speculo::Args listed{ speculo::ObjectRef(a), speculo::ObjectRef(b) };
    EXPECT_EQ(listed.size(), 2u);

    speculo::Object obj = std::move(speculo::Object::from_value(5).unwrap());
    speculo::Args single(obj);
    ASSERT_EQ(single.size(), 1u);
    EXPECT_EQ(single[0].type().get_name(), "int");
}

TEST(ArgumentLiteralTest, ConstructWithLiterals)
{
    const speculo::Class& cls = speculo::class_of<Vec>();

    auto made = cls.new_instance({ 3, 4 });
    ASSERT_TRUE(made.is_ok());
    speculo::Object vec = std::move(made).unwrap();
    EXPECT_EQ(vec.as<Vec>().unwrap().x, 3);
    EXPECT_EQ(vec.as<Vec>().unwrap().y, 4);

    // Value expressions work too, and still need an exact type match.
    int base = 10;
    auto computed = cls.new_instance({ base + 1, base * 2 });
    ASSERT_TRUE(computed.is_ok());
    EXPECT_EQ(std::move(computed).unwrap().as<Vec>().unwrap().y, 20);

    EXPECT_EQ(cls.new_instance({ 3, 4.0 }).unwrap_err().kind, speculo::ErrorKind::NotFound);
    EXPECT_EQ(cls.new_instance({ true }).unwrap_err().kind, speculo::ErrorKind::NotFound);

    speculo::Constructor two = cls.get_constructor<int, int>().unwrap();
    EXPECT_TRUE(two.new_instance({ 1, 2 }).is_ok());
    EXPECT_EQ(two.new_instance({ 3, 4.0 }).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
}

TEST(ArgumentLiteralTest, InvokeWithTemporariesAndStringLiterals)
{
    const speculo::Class& cls = speculo::class_of<Greeter>();
    Greeter greeter;

    // A string literal is passed as const char*.
    speculo::Method length = cls.get_method("length").unwrap();
    EXPECT_EQ(std::move(length.invoke(greeter, { "hello" })).unwrap().as<int>().unwrap(), 5);

    // A class temporary binds to a const& parameter...
    speculo::Method weigh = cls.get_method("weigh").unwrap();
    EXPECT_EQ(std::move(weigh.invoke(greeter, { Vec(2, 5) })).unwrap().as<int>().unwrap(), 7);

    // ...and to a by-value parameter, alongside other literals.
    speculo::Method scaled = cls.get_method("scaled").unwrap();
    EXPECT_EQ(std::move(scaled.invoke(greeter, { Vec(1, 2), 3 })).unwrap().as<int>().unwrap(), 9);

    speculo::Method flip = cls.get_method("flip").unwrap();
    EXPECT_FALSE(std::move(flip.invoke(greeter, { true })).unwrap().as<bool>().unwrap());

    // No numeric promotion: a short is not an int, and a char* literal is not a Vec.
    EXPECT_EQ(scaled.invoke(greeter, { Vec(1, 2), short{ 3 } }).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_EQ(weigh.invoke(greeter, { "abc" }).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    // Lvalues, ObjectRefs and Objects are still accepted alongside temporaries.
    int times = 2;
    EXPECT_EQ(std::move(scaled.invoke(greeter, { Vec(1, 1), times })).unwrap().as<int>().unwrap(), 4);
    speculo::Object owned = std::move(speculo::Object::from_value(4)).unwrap();
    EXPECT_EQ(std::move(scaled.invoke(greeter, { Vec(1, 1), owned })).unwrap().as<int>().unwrap(), 8);
    EXPECT_EQ(std::move(scaled.invoke(greeter, { Vec(1, 1), speculo::ObjectRef(times) })).unwrap().as<int>().unwrap(), 4);

    // More than the inline capacity still works (heap path); the call itself fails on count.
    EXPECT_EQ(scaled.invoke(greeter, { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }).unwrap_err().kind,
              speculo::ErrorKind::InvalidArgument);
}

TEST(ArgumentPointersTest, InlineAndHeapCallsBothWork)
{
    const speculo::Class& cls = speculo::class_of<p6b::Wide>();
    p6b::Wide wide;
    int v[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    // Exactly the inline capacity (8) ...
    const auto eight = cls.get_method("sum8").unwrap();
    EXPECT_EQ(eight.invoke(wide, { v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7] }).unwrap().as<int>().unwrap(), 36);

    // ... and past it (10), which spills to the heap fallback.
    const auto ten = cls.get_method("sum").unwrap();
    EXPECT_EQ(ten.invoke(wide, { v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9] })
                  .unwrap().as<int>().unwrap(),
              55);
}
