#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

// Default arguments: default_argument<T>, get_parameter_default,
// get_required_parameter_count and invoke_with_defaults (api_design §7).
//
// 矩阵：Method::invoke_with_defaults(receiver, args) —— 5 × 6 = 30 格，InvokeWithDefaultsMatrix 全断言
//  receiver \ args | 0 个 (None) | 1 个可读 | 1 个错类型 | 1 个 volatile | 2 个 | 3 个
//  ----------------+-------------+----------+-------------+---------------+-------+--------
//  正常            | InvalidArgument | Ok(15) | TypeMismatch | TypeMismatch | Ok(11) | InvalidArgument
//  const           | InvalidArgument | Ok(15) | TypeMismatch | TypeMismatch | Ok(11) | InvalidArgument
//  volatile        | InvalidArgument | NotSupported | NotSupported | NotSupported | NotSupported | InvalidArgument
//  类型不符        | InvalidArgument | TypeMismatch | TypeMismatch | TypeMismatch | TypeMismatch | InvalidArgument
//  空              | InvalidArgument | NullInstance | NullInstance | NullInstance | NullInstance | InvalidArgument
//  检查顺序：实参个数上限 → 最少个数 → 接收者空/类型/cv → 补默认值 → 逐参。
//
//  Constructor / Function 的 invoke_with_defaults：0/1/2/3 个实参各一格（4 格）。
//  get_parameter_default(i): 注解存在 → Some（即使不可读）；无注解 → None；越界 → panic。
//  不可读注解：显式给全实参时不读注解 → Ok；需要补齐时 → NotSupported。

namespace
{

struct Defaults
{
    int a = 0;
    int b = 0;

    Defaults() = default;

    Defaults(int a, [[= speculo::default_argument<int>{ 7 }]] int b = 7)
        : a(a)
        , b(b)
    {
    }

    int add(int a, [[= speculo::default_argument<int>{ 10 }]] int b = 10) const
    {
        return a + b;
    }

    int required(int a) const
    {
        return a;
    }

    static int multiply(int a, [[= speculo::default_argument<int>{ 2 }]] int b = 2)
    {
        return a * b;
    }
};

int scale(int a, [[= speculo::default_argument<int>{ 3 }]] int b = 3)
{
    return a * b;
}

struct Unreadable
{
    int f(int a, [[= speculo::default_argument<int*>{ nullptr }]] int* p = nullptr) const
    {
        return p == nullptr ? a : a + *p;
    }
};

struct Other
{
    int value = 0;
};

enum class ReceiverForm
{
    Normal,
    Const,
    Volatile,
    WrongType,
    Null,
};

enum class ArgsForm
{
    None,
    One,
    OneWrong,
    OneVolatile,
    Two,
    Three,
};

struct Expected
{
    bool              ok = false;
    speculo::ErrorKind kind = speculo::ErrorKind::NotSupported;
    int               value = 0;
};

[[nodiscard]] auto defaults_expected(ReceiverForm receiver, ArgsForm args) -> Expected
{
    if (args == ArgsForm::Three)
    {
        return { false, speculo::ErrorKind::InvalidArgument, 0 };
    }

    if (args == ArgsForm::None)
    {
        return { false, speculo::ErrorKind::InvalidArgument, 0 };
    }

    if (receiver == ReceiverForm::Null)
    {
        return { false, speculo::ErrorKind::NullInstance, 0 };
    }

    if (receiver == ReceiverForm::WrongType)
    {
        return { false, speculo::ErrorKind::TypeMismatch, 0 };
    }

    if (receiver == ReceiverForm::Volatile)
    {
        return { false, speculo::ErrorKind::NotSupported, 0 };
    }

    if (args == ArgsForm::OneWrong || args == ArgsForm::OneVolatile)
    {
        return { false, speculo::ErrorKind::TypeMismatch, 0 };
    }

    return { true, speculo::ErrorKind::NotSupported, args == ArgsForm::One ? 15 : 11 };
}

TEST(DefaultArgumentTest, AnnotationIsVisibleOnTheParameter)
{
    const speculo::Method method = speculo::class_of<Defaults>().get_method<int, int>("add").unwrap();

    ASSERT_EQ(method.get_parameter_count(), 2);
    EXPECT_EQ(method.get_required_parameter_count(), 1);

    const speculo::Option<speculo::Annotation> optional = method.get_parameter_default(1);
    ASSERT_TRUE(optional.is_some());
    const speculo::Annotation& annotation = optional.unwrap();
    EXPECT_TRUE(annotation.is_readable());
    EXPECT_EQ(annotation.get_type(), speculo::class_of<speculo::default_argument<int>>());

    const auto held = annotation.get<speculo::default_argument<int>>();
    ASSERT_TRUE(held.is_some());
    EXPECT_EQ(held.unwrap().value, 10);

    EXPECT_TRUE(method.get_parameter_default(0).is_none());
}

TEST(DefaultArgumentTest, InvokeWithDefaultsFillsTrailingParameters)
{
    Defaults object;
    const speculo::Method method = speculo::class_of<Defaults>().get_method<int, int>("add").unwrap();

    int provided = 5;
    EXPECT_EQ(method.invoke_with_defaults(object, { provided }).unwrap().as<int>().unwrap(), 15);

    int explicit_b = 6;
    EXPECT_EQ(method.invoke_with_defaults(object, { provided, explicit_b }).unwrap().as<int>().unwrap(), 11);
}

TEST(DefaultArgumentTest, ConstructorAndFreeFunctionShareTheRule)
{
    const speculo::Constructor ctor = speculo::class_of<Defaults>().get_constructor<int, int>().unwrap();
    ASSERT_TRUE(ctor.get_parameter_default(1).is_some());
    EXPECT_EQ(ctor.get_required_parameter_count(), 1);

    int first = 4;
    auto built = ctor.invoke_with_defaults({ first });
    ASSERT_TRUE(built.is_ok());
    EXPECT_EQ(std::move(built).unwrap().as<Defaults>().unwrap().b, 7);

    const speculo::Function fn = speculo::function_of<scale>();
    ASSERT_TRUE(fn.get_parameter_default(1).is_some());
    EXPECT_EQ(fn.get_required_parameter_count(), 1);
    EXPECT_EQ(fn.invoke_with_defaults({ first }).unwrap().as<int>().unwrap(), 12);
}

TEST(DefaultArgumentTest, StaticMethodAndNoArgumentDefaults)
{
    const speculo::Method method = speculo::class_of<Defaults>().get_method<int, int>("multiply").unwrap();
    int          provided = 9;
    EXPECT_EQ(method.invoke_with_defaults(speculo::ObjectRef{}, { provided }).unwrap().as<int>().unwrap(), 18);

    const speculo::Method required = speculo::class_of<Defaults>().get_method<int>("required").unwrap();
    EXPECT_EQ(required.get_required_parameter_count(), 1);
    EXPECT_TRUE(required.get_parameter_default(0).is_none());
}

TEST(DefaultArgumentTest, ArgumentCountErrors)
{
    Defaults object;
    const speculo::Method method = speculo::class_of<Defaults>().get_method<int, int>("add").unwrap();

    const auto too_few = method.invoke_with_defaults(object, {});
    ASSERT_TRUE(too_few.is_err());
    EXPECT_EQ(too_few.unwrap_err().kind, speculo::ErrorKind::InvalidArgument);

    int a = 1;
    int b = 2;
    int c = 3;
    const auto too_many = method.invoke_with_defaults(object, { a, b, c });
    ASSERT_TRUE(too_many.is_err());
    EXPECT_EQ(too_many.unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
}

TEST(DefaultArgumentTest, UnreadableAnnotationIsNotSupportedOnlyWhenFilling)
{
    Unreadable object;
    const speculo::Method method = speculo::class_of<Unreadable>().get_method<int, int*>("f").unwrap();

    const speculo::Option<speculo::Annotation> optional = method.get_parameter_default(1);
    ASSERT_TRUE(optional.is_some());
    EXPECT_FALSE(optional.unwrap().is_readable());

    int provided = 4;
    const auto filled = method.invoke_with_defaults(object, { provided });
    ASSERT_TRUE(filled.is_err());
    EXPECT_EQ(filled.unwrap_err().kind, speculo::ErrorKind::NotSupported);

    int* pointer = nullptr;
    auto explicit_argument = method.invoke_with_defaults(object, { provided, speculo::ObjectRef(pointer) });
    ASSERT_TRUE(explicit_argument.is_ok());
    EXPECT_EQ(std::move(explicit_argument).unwrap().as<int>().unwrap(), 4);
}

TEST(DefaultArgumentTest, InvokeWithDefaultsMatrix)
{
    constexpr std::array<ReceiverForm, 5> kReceivers{ ReceiverForm::Normal, ReceiverForm::Const,
                                                      ReceiverForm::Volatile, ReceiverForm::WrongType,
                                                      ReceiverForm::Null };
    constexpr std::array<ArgsForm, 6> kArgs{ ArgsForm::None, ArgsForm::One, ArgsForm::OneWrong,
                                             ArgsForm::OneVolatile, ArgsForm::Two, ArgsForm::Three };

    int           a = 5;
    int           b = 6;
    int           c = 7;
    volatile int  volatile_arg = 8;
    Other         other;
    Defaults      object;
    const Defaults const_object;
    volatile Defaults volatile_object;

    const speculo::Method method = speculo::class_of<Defaults>().get_method<int, int>("add").unwrap();

    for (ReceiverForm receiver_form : kReceivers)
    {
        for (ArgsForm args_form : kArgs)
        {
            SCOPED_TRACE(std::to_string(static_cast<int>(receiver_form)) + "x"
                         + std::to_string(static_cast<int>(args_form)));

            speculo::ObjectRef receiver;
            switch (receiver_form)
            {
            case ReceiverForm::Normal:    receiver = speculo::ObjectRef(object); break;
            case ReceiverForm::Const:     receiver = speculo::ObjectRef(const_object); break;
            case ReceiverForm::Volatile:  receiver = speculo::ObjectRef(volatile_object); break;
            case ReceiverForm::WrongType: receiver = speculo::ObjectRef(other); break;
            case ReceiverForm::Null:      break;
            }

            speculo::Args args;
            switch (args_form)
            {
            case ArgsForm::None:       break;
            case ArgsForm::One:        args = speculo::Args{ a }; break;
            case ArgsForm::OneWrong:   args = speculo::Args{ other }; break;
            case ArgsForm::OneVolatile: args = speculo::Args{ volatile_arg }; break;
            case ArgsForm::Two:        args = speculo::Args{ a, b }; break;
            case ArgsForm::Three:      args = speculo::Args{ a, b, c }; break;
            }

            const Expected expected = defaults_expected(receiver_form, args_form);
            auto           result = method.invoke_with_defaults(receiver, std::move(args));
            if (!expected.ok)
            {
                ASSERT_TRUE(result.is_err());
                EXPECT_EQ(result.unwrap_err().kind, expected.kind);
                continue;
            }

            ASSERT_TRUE(result.is_ok());
            EXPECT_EQ(std::move(result).unwrap().as<int>().unwrap(), expected.value);
        }
    }
}

TEST(DefaultArgumentTest, ConstructorAndFunctionArityMatrix)
{
    int a = 4;
    int b = 6;
    int c = 8;

    const speculo::Constructor ctor = speculo::class_of<Defaults>().get_constructor<int, int>().unwrap();
    for (int count = 0; count <= 3; ++count)
    {
        SCOPED_TRACE(std::to_string(count));
        speculo::Args args;
        if (count >= 1) { args = speculo::Args{ a }; }
        if (count >= 2) { args = speculo::Args{ a, b }; }
        if (count >= 3) { args = speculo::Args{ a, b, c }; }

        auto result = ctor.invoke_with_defaults(std::move(args));
        if (count == 0 || count == 3)
        {
            ASSERT_TRUE(result.is_err());
            EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
        }
        else
        {
            ASSERT_TRUE(result.is_ok());
            EXPECT_EQ(std::move(result).unwrap().as<Defaults>().unwrap().b, count == 1 ? 7 : 6);
        }
    }

    const speculo::Function function = speculo::function_of<scale>();
    for (int count = 0; count <= 3; ++count)
    {
        SCOPED_TRACE(std::to_string(count));
        speculo::Args args;
        if (count >= 1) { args = speculo::Args{ a }; }
        if (count >= 2) { args = speculo::Args{ a, b }; }
        if (count >= 3) { args = speculo::Args{ a, b, c }; }

        auto result = function.invoke_with_defaults(std::move(args));
        if (count == 0 || count == 3)
        {
            ASSERT_TRUE(result.is_err());
            EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
        }
        else
        {
            ASSERT_TRUE(result.is_ok());
            EXPECT_EQ(std::move(result).unwrap().as<int>().unwrap(), count == 1 ? 12 : 24);
        }
    }
}

TEST(DefaultArgumentTest, OutOfRangeIndexPanics)
{
    const speculo::Method method = speculo::class_of<Defaults>().get_method<int, int>("add").unwrap();
    EXPECT_DEATH(method.get_parameter_default(2), "");
}

} // namespace
