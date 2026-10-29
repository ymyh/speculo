#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/core_fixtures.h"

TEST(ObjectTest, FromValueAsCloneAndDestroy)
{
    Counter::destroyed = 0;
    {
        auto made = speculo::Object::from_value(Counter(7));
        ASSERT_TRUE(made.is_ok());
        speculo::Object obj = std::move(made).unwrap();
        ASSERT_FALSE(obj.is_null());
        EXPECT_EQ(obj.type().get_name(), "Counter");

        auto ref = obj.as<Counter>();
        ASSERT_TRUE(ref.is_some());
        EXPECT_EQ(ref.unwrap().value, 7);
        EXPECT_TRUE(obj.as<int>().is_none());

        auto cloned = obj.clone();
        ASSERT_TRUE(cloned.is_ok());
        speculo::Object copy = std::move(cloned).unwrap();
        EXPECT_EQ(copy.as<Counter>().unwrap().value, 7);
        // Only the by-value `from_value` parameter has destructed so far; the
        // boxed value and its clone are still alive.
        EXPECT_EQ(Counter::destroyed, 1);
    }
    // The by-value `from_value` parameter also destructs, hence 3 = param + obj + clone.
    EXPECT_EQ(Counter::destroyed, 3);
}

TEST(ObjectTest, MakeAndTake)
{
    Counter::destroyed = 0;
    {
        speculo::Object obj = std::move(speculo::Object::make<Counter>(11).unwrap());
        ASSERT_FALSE(obj.is_null());

        auto taken = std::move(obj).take<Counter>();
        ASSERT_TRUE(taken.is_ok());
        // Read through as_ref() so the lvalue unwrap() does not copy the Counter
        // (crab_cpp 0.4.0: lvalue unwrap() copies, rvalue unwrap() moves out).
        EXPECT_EQ(taken.as_ref().unwrap().value, 11);
        EXPECT_TRUE(obj.is_null());
    }
    // 3 = take()'s extracted local + the original storage reset + the returned value.
    EXPECT_EQ(Counter::destroyed, 3);
}

TEST(ObjectTest, MoveTransfersOwnership)
{
    Counter::destroyed = 0;
    {
        speculo::Object a = std::move(speculo::Object::make<Counter>(1).unwrap());
        speculo::Object b = std::move(a);
        EXPECT_TRUE(a.is_null());
        ASSERT_FALSE(b.is_null());
        EXPECT_EQ(b.as<Counter>().unwrap().value, 1);
        EXPECT_EQ(Counter::destroyed, 0);
    }
    EXPECT_EQ(Counter::destroyed, 1);
}

TEST(ObjectTest, NonCopyableTypeReportsNotSupported)
{
    struct Pinned
    {
        int value = 0;
        Pinned() = default;
        Pinned(const Pinned&) = delete;
        auto operator=(const Pinned&) -> Pinned& = delete;
    };

    speculo::Object obj = std::move(speculo::Object::make<Pinned>().unwrap());
    obj.as<Pinned>().unwrap().value = 12;

    auto cloned = obj.clone();
    ASSERT_TRUE(cloned.is_err());
    EXPECT_EQ(cloned.unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(ObjectTest, MoveOnlySemantics)
{
    static_assert(std::is_move_constructible_v<speculo::Object>);
    static_assert(!std::is_copy_constructible_v<speculo::Object>);
    static_assert(!std::is_copy_constructible_v<speculo::Args>);
    static_assert(std::is_move_constructible_v<speculo::Args>);

    EXPECT_TRUE(true);
}

TEST(ObjectRefTest, AsAndMutation)
{
    int x = 5;
    speculo::ObjectRef ref(x);
    EXPECT_FALSE(ref.is_null());
    EXPECT_EQ(ref.type().get_name(), "int");

    auto value = ref.as<int>();
    ASSERT_TRUE(value.is_some());
    value.unwrap() = 9;
    EXPECT_EQ(x, 9);
    EXPECT_TRUE(ref.as<double>().is_none());

    speculo::ObjectRef empty;
    EXPECT_TRUE(empty.is_null());
}

TEST(ObjectRefTest, CloneReferencesExistingObject)
{
    Counter::destroyed = 0;
    {
        Counter original(3);
        speculo::ObjectRef ref(original);

        auto cloned = ref.clone();
        ASSERT_TRUE(cloned.is_ok());
        speculo::Object clone = std::move(cloned).unwrap();
        EXPECT_EQ(clone.as<Counter>().unwrap().value, 3);
        EXPECT_EQ(Counter::destroyed, 0);
    }
    EXPECT_EQ(Counter::destroyed, 2);
}
