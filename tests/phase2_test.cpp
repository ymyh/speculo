#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

namespace
{

// Counts destructions to exercise Object / ObjectRef ownership.
struct Counter
{
    static int destroyed;

    int value = 0;

    Counter() = default;
    explicit Counter(int v) : value(v) {}
    Counter(const Counter&) = default;
    auto operator=(const Counter&) -> Counter& = default;
    ~Counter() { ++destroyed; }
};

int Counter::destroyed = 0;

struct Widget
{
    int x = 0;
    virtual ~Widget() = default;
};

} // namespace

TEST(ErrorTest, KindsStatusAndOutcome)
{
    speculo::Status ok = speculo::None{};
    EXPECT_TRUE(ok.is_ok());

    speculo::Status bad = speculo::Error{speculo::ErrorKind::NotFound, "missing"};
    ASSERT_TRUE(bad.is_err());
    EXPECT_EQ(bad.unwrap_err().kind, speculo::ErrorKind::NotFound);
    EXPECT_EQ(bad.unwrap_err().message, "missing");

    EXPECT_EQ(speculo::to_string(speculo::ErrorKind::TypeMismatch), "TypeMismatch");

    speculo::Outcome<int> value = 42;
    ASSERT_TRUE(value.is_ok());
    EXPECT_EQ(value.unwrap(), 42);
}

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
