#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

// Callable::of / invoke / invoke_ref (api_design §2.3).
//
// Callable::invoke —— 接收者 × 实参，错误优先级见 api_design §0.1（接收者适用性 → 个数 →
// 接收者 → 实参）
// 接收者 \ 实参        | 个数不符         | 类型不符      | 正确
//  声明类的对象        | InvalidArgument  | TypeMismatch  | Ok / Ok(视图)
//  空                  | InvalidArgument  | NullInstance  | NullInstance
//  类型不符            | InvalidArgument  | TypeMismatch  | TypeMismatch
//  自由函数指针（带 self）| NotSupported  | NotSupported  | NotSupported
//
// Callable::of(value)（Option）：
//  函数指针 / 成员函数指针 -> Some；普通值 / 空引用 -> None

namespace
{

int add(int a, int b)
{
    return a + b;
}

int silent(int value) noexcept
{
    return value;
}

int sum_fixed(int first, ...)
{
    return first;
}

int& refer_to(int& value)
{
    return value;
}

struct Calc
{
    int base = 10;

    int plus(int x) const { return base + x; }
    virtual int scale(int x) const { return x * 2; }
    int& cell() { return base; }
};

struct DerivedCalc : Calc
{
    int scale(int x) const override { return x * 3; }
};

struct Other
{
    int v = 0;
};

struct Stored
{
    int (*fn)(int, int) = &add;
    int (Calc::*method)(int) const = &Calc::plus;
    int& (Calc::*cell)() = &Calc::cell;
    int (*variadic)(int, ...) = &sum_fixed;
    int (*noexcept_fn)(int) noexcept = &silent;
};

} // namespace

TEST(CallableTest, FunctionPointerValue)
{
    Stored stored;
    const speculo::Field field = speculo::class_of<Stored>().get_field("fn").unwrap();

    speculo::Object boxed = field.get(stored).unwrap();
    ASSERT_TRUE(speculo::Callable::of(boxed.cref()).is_some());

    const speculo::Callable callable = speculo::Callable::of(boxed.cref()).unwrap();
    EXPECT_FALSE(callable.is_member_function_pointer());
    EXPECT_TRUE(callable.get_declaring_class().is_none());
    EXPECT_EQ(callable.get_type(), speculo::class_of<int (*)(int, int)>());

    // The function *type* behind the pointer is decomposed exactly like a
    // reflected function type (api_design §2.2).
    const speculo::Class& signature = callable.get_function_type();
    EXPECT_EQ(signature.kind(), speculo::TypeKind::Function);
    ASSERT_TRUE(signature.get_return_type().is_some());
    EXPECT_EQ(signature.get_return_type().unwrap(), speculo::class_of<int>());
    ASSERT_EQ(signature.get_parameter_count(), 2u);
    EXPECT_EQ(signature.get_parameter_types()[0], &speculo::class_of<int>());
    EXPECT_EQ(signature.get_parameter_types()[1], &speculo::class_of<int>());
    EXPECT_FALSE(signature.is_variadic());
    EXPECT_FALSE(signature.is_noexcept());
    EXPECT_TRUE(callable.is_invokable());
    EXPECT_FALSE(callable.returns_reference());

    int three = 3;
    int four = 4;
    speculo::Object result = callable.invoke({ three, four }).unwrap();
    EXPECT_EQ(result.as<int>().unwrap(), 7);
}

TEST(CallableTest, MemberFunctionPointerValue)
{
    Stored stored;
    const speculo::Field field = speculo::class_of<Stored>().get_field("method").unwrap();

    speculo::Object boxed = field.get(stored).unwrap();
    const speculo::Callable callable = speculo::Callable::of(boxed.cref()).unwrap();

    EXPECT_TRUE(callable.is_member_function_pointer());
    ASSERT_TRUE(callable.get_declaring_class().is_some());
    EXPECT_EQ(callable.get_declaring_class().unwrap(), speculo::class_of<Calc>());
    EXPECT_EQ(callable.get_function_type().get_parameter_count(), 1u);
    EXPECT_EQ(callable.get_function_type().get_return_type().unwrap(), speculo::class_of<int>());

    Calc calc;
    calc.base = 10;
    int increment = 5;
    speculo::Object result = callable.invoke(calc, { increment }).unwrap();
    EXPECT_EQ(result.as<int>().unwrap(), 15);
}

TEST(CallableTest, MemberPointerDispatchesVirtually)
{
    DerivedCalc derived;
    Calc& as_base = derived;

    // The pointer is a *value* held in a field of the member-pointer type.
    Stored stored;
    stored.method = &Calc::scale;

    const speculo::Callable callable =
        speculo::Callable::of(speculo::class_of<Stored>().get_field("method").unwrap().get_ref(stored).unwrap())
            .unwrap();

    int two = 2;
    // `as_base` is typed Calc but refers to a DerivedCalc: the member pointer call
    // dispatches virtually, exactly like a C++ call through the base.
    speculo::Object result = callable.invoke(as_base, { two }).unwrap();
    EXPECT_EQ(result.as<int>().unwrap(), 6);
}

TEST(CallableTest, ReferenceReturnIsViewedWithoutCopy)
{
    Calc calc;
    calc.base = 21;

    Stored stored;
    const speculo::Callable callable =
        speculo::Callable::of(speculo::class_of<Stored>().get_field("cell").unwrap().get_ref(stored).unwrap()).unwrap();

    EXPECT_TRUE(callable.returns_reference());
    EXPECT_TRUE(callable.is_ref_invokable());

    speculo::Result<speculo::ObjectRef, speculo::Error> view = callable.invoke_ref(calc, {});
    ASSERT_TRUE(view.is_ok());
    speculo::ObjectRef referenced = view.unwrap();
    EXPECT_EQ(referenced.as<int>().unwrap(), 21);

    // Writing through the view reaches the original object.
    referenced.as<int>().unwrap() = 99;
    EXPECT_EQ(calc.base, 99);
}

TEST(CallableTest, FreeFunctionPointerRejectsAReceiver)
{
    Stored stored;
    const speculo::Callable callable =
        speculo::Callable::of(speculo::class_of<Stored>().get_field("fn").unwrap().get_ref(stored).unwrap()).unwrap();

    Other other;
    EXPECT_EQ(callable.invoke(other, {}).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(callable.invoke_ref(other, {}).unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(CallableTest, NoexceptAndVariadicFunctionTypes)
{
    Stored stored;
    const speculo::Class& stored_class = speculo::class_of<Stored>();

    const speculo::Callable noexcept_callable =
        speculo::Callable::of(stored_class.get_field("noexcept_fn").unwrap().get_ref(stored).unwrap()).unwrap();
    EXPECT_TRUE(noexcept_callable.get_function_type().is_noexcept());

    const speculo::Callable variadic_callable =
        speculo::Callable::of(stored_class.get_field("variadic").unwrap().get_ref(stored).unwrap()).unwrap();
    EXPECT_TRUE(variadic_callable.get_function_type().is_variadic());
    EXPECT_EQ(variadic_callable.get_function_type().get_parameter_count(), 1u);

    // A C-variadic pointer is described, never called (it cannot be driven from a
    // fixed argument array) -- the same rule as Method::invoke.
    EXPECT_FALSE(variadic_callable.is_invokable());
    int nine = 9;
    EXPECT_EQ(variadic_callable.invoke({ nine }).unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(CallableTest, OfAcceptsOnlyCallableValues)
{
    int plain = 3;
    EXPECT_TRUE(speculo::Callable::of(speculo::ObjectRef(plain)).is_none());

    Other other;
    EXPECT_TRUE(speculo::Callable::of(other).is_none());

    // A member *object* pointer holds no code.
    int Other::*data_member = &Other::v;
    EXPECT_TRUE(speculo::Callable::of(speculo::ObjectRef(data_member)).is_none());

    EXPECT_TRUE(speculo::Callable::of(speculo::ObjectRef{}).is_none());
}

TEST(CallableTest, InvokeErrorPrecedence)
{
    Stored stored;
    const speculo::Callable member =
        speculo::Callable::of(speculo::class_of<Stored>().get_field("method").unwrap().get_ref(stored).unwrap()).unwrap();

    Calc calc;
    Other other;
    int increment = 1;
    std::string text("x");

    // 个数 → 接收者 → 实参
    EXPECT_EQ(member.invoke(calc, {}).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
    EXPECT_EQ(member.invoke(calc, { increment, increment }).unwrap_err().kind,
              speculo::ErrorKind::InvalidArgument);
    EXPECT_EQ(member.invoke(speculo::ObjectRef{}, { increment }).unwrap_err().kind,
              speculo::ErrorKind::NullInstance);
    EXPECT_EQ(member.invoke(other, { increment }).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_EQ(member.invoke(calc, { text }).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    // A null receiver loses to a wrong argument count.
    EXPECT_EQ(member.invoke(speculo::ObjectRef{}, {}).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
    // A wrong receiver loses to a wrong argument count.
    EXPECT_EQ(member.invoke(other, {}).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
    // A wrong argument type loses to a null receiver.
    EXPECT_EQ(member.invoke(speculo::ObjectRef{}, { text }).unwrap_err().kind,
              speculo::ErrorKind::NullInstance);
    // Receiver type is checked before the arguments, so both wrong is still a
    // receiver mismatch.
    EXPECT_EQ(member.invoke(other, { text }).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    // A free function pointer refuses a receiver whatever the arguments are.
    Stored function_field;
    const speculo::Callable free_function =
        speculo::Callable::of(speculo::class_of<Stored>().get_field("fn").unwrap().get_ref(function_field).unwrap())
            .unwrap();
    EXPECT_EQ(free_function.invoke(calc, {}).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(free_function.invoke(calc, { increment, increment }).unwrap_err().kind,
              speculo::ErrorKind::NotSupported);
    // Without a receiver the free pointer is called normally.
    EXPECT_EQ(free_function.invoke({ increment, increment }).unwrap().as<int>().unwrap(), 2);
}

TEST(CallableTest, InvokeRefRequiresAReferenceResult)
{
    Stored stored;
    const speculo::Callable callable =
        speculo::Callable::of(speculo::class_of<Stored>().get_field("fn").unwrap().get_ref(stored).unwrap()).unwrap();

    int three = 3;
    int four = 4;
    EXPECT_EQ(callable.invoke_ref({ three, four }).unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(CallableTest, IdentityIsTheTypeAndTheValue)
{
    int (*first)(int, int) = &add;
    int (*second)(int, int) = &add;

    const speculo::Callable a = speculo::Callable::of(speculo::ObjectRef(first)).unwrap();
    const speculo::Callable b = speculo::Callable::of(speculo::ObjectRef(second)).unwrap();

    EXPECT_EQ(a, b);
    EXPECT_EQ(a.get_type(), b.get_type());
}

TEST(CallableTest, EmptyHandlePanics)
{
    const speculo::Callable empty;
    EXPECT_DEATH(static_cast<void>(empty.is_member_function_pointer()), "");
}
