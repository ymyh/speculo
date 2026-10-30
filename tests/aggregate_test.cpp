#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

// Class::new_aggregate / get_aggregate_initializer, Object::make_aggregate
// (api_design §3).
//
// Class::new_aggregate —— 类型能力 × 实参，错误优先级见 api_design §0.1/§3
// 类型 \ 实参   | 个数不符          | 类型不符      | 正确
//  聚合          | InvalidArgument   | TypeMismatch  | Ok
//  非聚合        | NotSupported（能力先于个数）
//  私有基 / 数组 / 右值引用成员 | NotSupported
//
// get_aggregate_initializer()（Option）：可构造的聚合 -> Some；其余 -> None

namespace
{

struct Base1
{
    int b1 = 0;
};

struct Base2
{
    int b2 = 0;
};

struct Agg : Base1, Base2
{
    int x = 0;
    double y = 0;
};

struct NotAggregate
{
    int a = 0;
    explicit NotAggregate(int value) : a(value) {}
};

struct HiddenBase
{
    int h = 0;
};

class PrivateBase : private HiddenBase
{
public:
    int v = 0;
};

struct WithArray
{
    int values[2] = { 0, 0 };
};

struct WithConst
{
    const int c;
};

struct WithRef
{
    int& r;
};

struct WithRvalueRef
{
    int&& rr;
};

struct Scalar
{
    int value = 0;
};

union Choice
{
    int i;
    float f;
};

} // namespace

TEST(AggregateTest, AggregateInitializerDescribesEverySlot)
{
    const speculo::Class& cls = speculo::class_of<Agg>();

    ASSERT_TRUE(cls.is_aggregate());
    ASSERT_TRUE(cls.get_aggregate_initializer().is_some());

    const speculo::Constructor initializer = cls.get_aggregate_initializer().unwrap();

    // public bases first (declaration order), then the data members (S8.6).
    ASSERT_EQ(initializer.get_parameter_count(), 4u);
    EXPECT_EQ(initializer.get_parameter_types()[0], &speculo::class_of<Base1>());
    EXPECT_EQ(initializer.get_parameter_types()[1], &speculo::class_of<Base2>());
    EXPECT_EQ(initializer.get_parameter_types()[2], &speculo::class_of<int>());
    EXPECT_EQ(initializer.get_parameter_types()[3], &speculo::class_of<double>());
    EXPECT_EQ(initializer.get_declaring_class(), cls);
    EXPECT_TRUE(initializer.modifiers().is_public());
    EXPECT_FALSE(initializer.is_copy_constructor());
    EXPECT_FALSE(initializer.is_move_constructor());
    EXPECT_NE(initializer.get_signature().find("Agg("), std::string_view::npos);
}

TEST(AggregateTest, NewAggregateFillsBasesAndMembers)
{
    const speculo::Class& cls = speculo::class_of<Agg>();

    speculo::Object object = cls.new_aggregate({ Base1{ 1 }, Base2{ 2 }, 3, 4.5 }).unwrap();
    ASSERT_EQ(object.type(), cls);

    const speculo::Field x = cls.get_field("x").unwrap();
    const speculo::Field y = cls.get_field("y").unwrap();
    EXPECT_EQ(x.get(object).unwrap().as<int>().unwrap(), 3);
    EXPECT_DOUBLE_EQ(y.get(object).unwrap().as<double>().unwrap(), 4.5);

    // The base subobjects are reachable through cast_to, and hold what was passed.
    const speculo::Class& base1 = cls.get_base<Base1>().unwrap();
    speculo::ObjectRef base_view = cls.cast_to(object, base1).unwrap();
    EXPECT_EQ(base1.get_field("b1").unwrap().get(base_view).unwrap().as<int>().unwrap(), 1);
}

TEST(AggregateTest, InitializerIsAUsableConstructor)
{
    const speculo::Class& cls = speculo::class_of<Agg>();
    const speculo::Constructor initializer = cls.get_aggregate_initializer().unwrap();

    // new_instance() goes through the same thunk as Class::new_aggregate().
    speculo::Object object = initializer.new_instance({ Base1{ 5 }, Base2{ 6 }, 7, 8.5 }).unwrap();
    EXPECT_EQ(cls.get_field("x").unwrap().get(object).unwrap().as<int>().unwrap(), 7);

    // construct_at / destroy_at work on caller-provided storage.
    alignas(Agg) std::byte storage[sizeof(Agg)];
    EXPECT_TRUE(initializer.construct_at(storage, { Base1{ 9 }, Base2{ 10 }, 11, 12.5 }).is_ok());

    auto* placed = reinterpret_cast<Agg*>(storage);
    EXPECT_EQ(placed->x, 11);
    EXPECT_DOUBLE_EQ(placed->y, 12.5);
    EXPECT_TRUE(initializer.destroy_at(storage).is_ok());
}

TEST(AggregateTest, CompileTimeAggregateConstruction)
{
    speculo::Object object = speculo::Object::make_aggregate<Agg>(Base1{ 2 }, Base2{ 4 }, 6, 8.0).unwrap();

    const Agg& value = object.as<Agg>().unwrap();
    EXPECT_EQ(value.b1, 2);
    EXPECT_EQ(value.b2, 4);
    EXPECT_EQ(value.x, 6);
    EXPECT_DOUBLE_EQ(value.y, 8.0);
}

TEST(AggregateTest, ConstAndReferenceMembersAreInitializable)
{
    speculo::Object with_const = speculo::class_of<WithConst>().new_aggregate({ 42 }).unwrap();
    EXPECT_EQ(with_const.as<WithConst>().unwrap().c, 42);

    int referent = 7;
    speculo::Object with_ref = speculo::class_of<WithRef>().new_aggregate({ referent }).unwrap();
    EXPECT_EQ(std::addressof(with_ref.as<WithRef>().unwrap().r), std::addressof(referent));
}

TEST(AggregateTest, UnsupportedShapesAreRejected)
{
    // A plain aggregate is supported; everything below is not.
    EXPECT_TRUE(speculo::class_of<Scalar>().get_aggregate_initializer().is_some());

    // Not an aggregate (user-declared constructor).
    EXPECT_TRUE(speculo::class_of<NotAggregate>().get_aggregate_initializer().is_none());
    EXPECT_EQ(speculo::class_of<NotAggregate>().new_aggregate({ 1 }).unwrap_err().kind,
              speculo::ErrorKind::NotSupported);

    // Private base: the slot cannot be initialized at namespace scope.
    EXPECT_TRUE(speculo::class_of<PrivateBase>().get_aggregate_initializer().is_none());
    EXPECT_EQ(speculo::class_of<PrivateBase>().new_aggregate({}).unwrap_err().kind,
              speculo::ErrorKind::NotSupported);

    // Array member: an array cannot be passed by value (api_design §3.2).
    EXPECT_TRUE(speculo::class_of<WithArray>().get_aggregate_initializer().is_none());

    // Rvalue-reference member: an lvalue view cannot bind to it.
    EXPECT_TRUE(speculo::class_of<WithRvalueRef>().get_aggregate_initializer().is_none());

    // A union aggregate initializes one member; there is no positional list.
    EXPECT_TRUE(speculo::class_of<Choice>().get_aggregate_initializer().is_none());

    // Non-class types have no aggregate initializer at all.
    EXPECT_TRUE(speculo::class_of<int>().get_aggregate_initializer().is_none());
    EXPECT_TRUE(speculo::class_of<int[3]>().get_aggregate_initializer().is_none());
}

TEST(AggregateTest, ArgumentErrorsFollowTheDocumentedOrder)
{
    const speculo::Class& cls = speculo::class_of<Agg>();

    // Capability first: a non-aggregate reports NotSupported whatever the arguments.
    EXPECT_EQ(speculo::class_of<NotAggregate>().new_aggregate({}).unwrap_err().kind,
              speculo::ErrorKind::NotSupported);

    // Then the argument count.
    EXPECT_EQ(cls.new_aggregate({}).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
    EXPECT_EQ(cls.new_aggregate({ Base1{ 1 }, Base2{ 2 }, 3 }).unwrap_err().kind,
              speculo::ErrorKind::InvalidArgument);
    EXPECT_EQ(cls.new_aggregate({ Base1{ 1 }, Base2{ 2 }, 3, 4.5, 5 }).unwrap_err().kind,
              speculo::ErrorKind::InvalidArgument);

    // Then the argument types, left to right (exact match; a base slot takes the
    // base object, never its members -- S8.6).
    EXPECT_EQ(cls.new_aggregate({ 1, Base2{ 2 }, 3, 4.5 }).unwrap_err().kind,
              speculo::ErrorKind::TypeMismatch);
    EXPECT_EQ(cls.new_aggregate({ Base1{ 1 }, Base2{ 2 }, 3.0, 4.5 }).unwrap_err().kind,
              speculo::ErrorKind::TypeMismatch);

    // A wrong count wins over a wrong type.
    EXPECT_EQ(cls.new_aggregate({ 1 }).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
}
