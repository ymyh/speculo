#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

// speculo::ObjectRef::value_category / as_rvalue / as_lvalue and speculo::Method::invoke_rvalue
// (api_design §8.2).
//
// 矩阵 1：ObjectRef 视图 × 操作（6 × 3）——所有格子都由 ObjectRefCategoryMatrix 断言
//  视图 \ 操作       | value_category | as_rvalue | as_lvalue
//  -----------------+----------------+-----------+----------
//  V0 左值          | Lvalue         | Rvalue    | Lvalue
//  V1 已标记右值    | Rvalue         | Rvalue    | Lvalue
//  V2 const         | Lvalue         | Rvalue（保留 const） | Lvalue（保留 const）
//  V3 volatile      | Lvalue         | Rvalue（保留 volatile） | Lvalue（保留 volatile）
//  V4 类型不符      | Lvalue         | Rvalue    | Lvalue   （视图本身仍成立）
//  V5 空 ObjectRef  | Lvalue         | Rvalue    | Lvalue
//
// 矩阵 2：Method::invoke_rvalue 接收者 × 实参个数
//  `&&` 方法 take()（6 × 2，InvokeRvalueMatrixForRvalueQualifiedMethod）
//  接收者 \ 实参     | 0 个          | 1 个
//  -----------------+---------------+----------------
//  左值 / 右值视图  | Ok（移动）    | InvalidArgument
//  const            | ReadOnly      | InvalidArgument
//  volatile         | NotSupported  | InvalidArgument
//  类型不符         | TypeMismatch  | InvalidArgument
//  空               | NullInstance  | InvalidArgument
//
//  `&` 方法 bump()（6 × 1，InvokeRvalueErrorPriorityForLvalueQualifiedMethod）
//  左值 / 右值 / const / volatile → NotSupported；类型不符 → TypeMismatch；空 → NullInstance。
//
//  无 qualifier 的 const 方法 peek()（6 × 1，InvokeRvalueMatrixForUnqualifiedMethod）
//  左值 / 右值 / const → Ok(42)；volatile → NotSupported；类型不符 → TypeMismatch；空 → NullInstance。
//
// 错误优先级：实参个数 → 空接收者 → 接收者类型 → ref-qualifier → const → volatile → 逐参。

namespace
{

struct MoveOnly
{
    int value = 0;

    MoveOnly() = default;
    explicit MoveOnly(int value)
        : value(value)
    {
    }

    MoveOnly(const MoveOnly&) = delete;
    auto operator=(const MoveOnly&) -> MoveOnly& = delete;

    MoveOnly(MoveOnly&& other) noexcept
        : value(other.value)
    {
        other.value = -1;
    }

    auto operator=(MoveOnly&& other) noexcept -> MoveOnly&
    {
        value = other.value;
        other.value = -1;
        return *this;
    }
};

struct RValueTarget
{
    MoveOnly payload{ 7 };
    int      lvalue_calls = 0;

    int bump() &
    {
        return ++lvalue_calls;
    }

    int peek() const
    {
        return 42;
    }

    MoveOnly take() &&
    {
        return std::move(payload);
    }

    static int add(int a, int b)
    {
        return a + b;
    }
};

struct Other
{
    int value = 0;
};

struct Base
{
    int base = 0;
};

struct Derived : Base
{
    int derived = 1;
};

enum class ViewForm
{
    Lvalue,
    RvalueView,
    Const,
    Volatile,
    WrongType,
    Null,
};

static_assert(sizeof(speculo::ObjectRef) == 24,
              "speculo::ObjectRef keeps its 24-byte layout after adding the value-category flag");

TEST(ValueCategoryTest, DefaultViewIsLvalueAndMarkingDoesNotCopy)
{
    RValueTarget target;

    speculo::ObjectRef view = target;
    EXPECT_EQ(view.value_category(), speculo::ValueCategory::Lvalue);

    speculo::ObjectRef rvalue = view.as_rvalue();
    EXPECT_EQ(rvalue.value_category(), speculo::ValueCategory::Rvalue);
    EXPECT_EQ(view.value_category(), speculo::ValueCategory::Lvalue); // the original is unchanged
    EXPECT_EQ(rvalue.raw_data(), view.raw_data());
    EXPECT_EQ(rvalue.type(), view.type());

    speculo::ObjectRef back = rvalue.as_lvalue();
    EXPECT_EQ(back.value_category(), speculo::ValueCategory::Lvalue);
}

TEST(ValueCategoryTest, MarkingPreservesConstAndVolatile)
{
    const RValueTarget target;
    speculo::ObjectRef view = target;

    EXPECT_TRUE(view.is_const());
    speculo::ObjectRef rvalue = view.as_rvalue();
    EXPECT_TRUE(rvalue.is_const());
    EXPECT_EQ(rvalue.value_category(), speculo::ValueCategory::Rvalue);

    volatile RValueTarget volatile_target;
    speculo::ObjectRef volatile_view = volatile_target;
    EXPECT_TRUE(volatile_view.is_volatile());
    EXPECT_TRUE(volatile_view.as_rvalue().is_volatile());
}

TEST(ValueCategoryTest, LvalueQualifiedMethodOnlyUsesInvoke)
{
    RValueTarget target;
    const speculo::Method method = speculo::class_of<RValueTarget>().get_method<>("bump").unwrap();

    EXPECT_TRUE(method.is_invokable());
    EXPECT_FALSE(method.is_rvalue_invokable());
    EXPECT_EQ(method.invoke(target).unwrap().as<int>().unwrap(), 1);

    speculo::ObjectRef rvalue = speculo::ObjectRef(target).as_rvalue();
    const auto rejected = method.invoke_rvalue(rvalue);
    ASSERT_TRUE(rejected.is_err());
    EXPECT_EQ(rejected.unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(ValueCategoryTest, RvalueQualifiedMethodOnlyUsesInvokeRvalue)
{
    RValueTarget target;
    const speculo::Method method = speculo::class_of<RValueTarget>().get_method<>("take").unwrap();

    EXPECT_FALSE(method.is_invokable());
    EXPECT_TRUE(method.is_rvalue_invokable());

    const auto through_invoke = method.invoke(target);
    ASSERT_TRUE(through_invoke.is_err());
    EXPECT_EQ(through_invoke.unwrap_err().kind, speculo::ErrorKind::NotSupported);

    speculo::ObjectRef rvalue = speculo::ObjectRef(target).as_rvalue();
    auto result = method.invoke_rvalue(rvalue);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(std::move(result).unwrap().as<MoveOnly>().unwrap().value, 7);
    EXPECT_EQ(target.payload.value, -1); // take() really moved from the receiver
}

TEST(ValueCategoryTest, UnqualifiedMethodWorksThroughBothPaths)
{
    RValueTarget target;
    const speculo::Method method = speculo::class_of<RValueTarget>().get_method<>("peek").unwrap();

    EXPECT_TRUE(method.is_invokable());
    EXPECT_TRUE(method.is_rvalue_invokable());
    EXPECT_EQ(method.invoke(target).unwrap().as<int>().unwrap(), 42);
    EXPECT_EQ(method.invoke_rvalue(speculo::ObjectRef(target).as_rvalue()).unwrap().as<int>().unwrap(), 42);
}

TEST(ValueCategoryTest, StaticMethodIgnoresTheReceiverCategory)
{
    const speculo::Method method = speculo::class_of<RValueTarget>().get_method<int, int>("add").unwrap();
    int          a = 2;
    int          b = 3;

    EXPECT_TRUE(method.is_rvalue_invokable());
    EXPECT_EQ(method.invoke_rvalue(speculo::ObjectRef{}, { a, b }).unwrap().as<int>().unwrap(), 5);
}

TEST(ValueCategoryTest, RvaluePathKeepsReceiverChecks)
{
    RValueTarget target;
    const speculo::Method method = speculo::class_of<RValueTarget>().get_method<>("take").unwrap();

    const auto null_receiver = method.invoke_rvalue(speculo::ObjectRef{});
    ASSERT_TRUE(null_receiver.is_err());
    EXPECT_EQ(null_receiver.unwrap_err().kind, speculo::ErrorKind::NullInstance);

    Other other;
    const auto wrong_type = method.invoke_rvalue(other);
    ASSERT_TRUE(wrong_type.is_err());
    EXPECT_EQ(wrong_type.unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    const RValueTarget const_target;
    const auto const_receiver = method.invoke_rvalue(const_target);
    ASSERT_TRUE(const_receiver.is_err());
    EXPECT_EQ(const_receiver.unwrap_err().kind, speculo::ErrorKind::ReadOnly);

    volatile RValueTarget volatile_target;
    const auto volatile_receiver = method.invoke_rvalue(volatile_target);
    ASSERT_TRUE(volatile_receiver.is_err());
    EXPECT_EQ(volatile_receiver.unwrap_err().kind, speculo::ErrorKind::NotSupported);

    int extra = 1;
    const auto wrong_count = method.invoke_rvalue(target, { extra });
    ASSERT_TRUE(wrong_count.is_err());
    EXPECT_EQ(wrong_count.unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
}

TEST(ValueCategoryTest, InvokeRvalueMatrixForUnqualifiedMethod)
{
    constexpr std::array<ViewForm, 6> kForms{ ViewForm::Lvalue, ViewForm::RvalueView, ViewForm::Const,
                                              ViewForm::Volatile, ViewForm::WrongType, ViewForm::Null };

    const speculo::Method method = speculo::class_of<RValueTarget>().get_method<>("peek").unwrap();
    for (ViewForm form : kForms)
    {
        SCOPED_TRACE(std::to_string(static_cast<int>(form)));

        RValueTarget          object;
        const RValueTarget    const_object;
        volatile RValueTarget volatile_object;
        Other                 other;

        speculo::ObjectRef receiver;
        switch (form)
        {
        case ViewForm::Lvalue:     receiver = speculo::ObjectRef(object); break;
        case ViewForm::RvalueView: receiver = speculo::ObjectRef(object).as_rvalue(); break;
        case ViewForm::Const:      receiver = speculo::ObjectRef(const_object); break;
        case ViewForm::Volatile:   receiver = speculo::ObjectRef(volatile_object); break;
        case ViewForm::WrongType:  receiver = speculo::ObjectRef(other); break;
        case ViewForm::Null:       break;
        }

        auto result = method.invoke_rvalue(receiver);
        if (form == ViewForm::Null)
        {
            ASSERT_TRUE(result.is_err());
            EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::NullInstance);
        }
        else if (form == ViewForm::WrongType)
        {
            ASSERT_TRUE(result.is_err());
            EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
        }
        else if (form == ViewForm::Volatile)
        {
            ASSERT_TRUE(result.is_err());
            EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::NotSupported);
        }
        else
        {
            ASSERT_TRUE(result.is_ok());
            EXPECT_EQ(std::move(result).unwrap().as<int>().unwrap(), 42);
        }
    }
}

TEST(ValueCategoryTest, CastToPreservesTheValueCategory)
{
    Derived derived;
    speculo::ObjectRef rvalue = speculo::ObjectRef(derived).as_rvalue();

    auto base = speculo::class_of<Derived>().cast_to(rvalue, speculo::class_of<Base>());
    ASSERT_TRUE(base.is_ok());
    EXPECT_EQ(base.unwrap().value_category(), speculo::ValueCategory::Rvalue);
}

TEST(ValueCategoryTest, ObjectRefCategoryMatrix)
{
    constexpr std::array<ViewForm, 6> kForms{ ViewForm::Lvalue, ViewForm::RvalueView, ViewForm::Const,
                                              ViewForm::Volatile, ViewForm::WrongType, ViewForm::Null };

    RValueTarget       object;
    const RValueTarget const_object;
    volatile RValueTarget volatile_object;
    Other              other;

    for (ViewForm form : kForms)
    {
        SCOPED_TRACE(std::to_string(static_cast<int>(form)));

        speculo::ObjectRef view;
        switch (form)
        {
        case ViewForm::Lvalue:     view = speculo::ObjectRef(object); break;
        case ViewForm::RvalueView: view = speculo::ObjectRef(object).as_rvalue(); break;
        case ViewForm::Const:      view = speculo::ObjectRef(const_object); break;
        case ViewForm::Volatile:   view = speculo::ObjectRef(volatile_object); break;
        case ViewForm::WrongType:  view = speculo::ObjectRef(other); break;
        case ViewForm::Null:       break;
        }

        const speculo::ValueCategory expected =
            form == ViewForm::RvalueView ? speculo::ValueCategory::Rvalue : speculo::ValueCategory::Lvalue;
        EXPECT_EQ(view.value_category(), expected);
        EXPECT_EQ(view.as_rvalue().value_category(), speculo::ValueCategory::Rvalue);
        EXPECT_EQ(view.as_lvalue().value_category(), speculo::ValueCategory::Lvalue);

        EXPECT_EQ(view.as_rvalue().is_const(), view.is_const());
        EXPECT_EQ(view.as_rvalue().is_volatile(), view.is_volatile());
        EXPECT_EQ(view.as_lvalue().is_const(), view.is_const());
        EXPECT_EQ(view.as_lvalue().is_volatile(), view.is_volatile());
        EXPECT_EQ(view.as_rvalue().raw_data(), view.raw_data());
    }
}

TEST(ValueCategoryTest, InvokeRvalueMatrixForRvalueQualifiedMethod)
{
    constexpr std::array<ViewForm, 6> kForms{ ViewForm::Lvalue, ViewForm::RvalueView, ViewForm::Const,
                                              ViewForm::Volatile, ViewForm::WrongType, ViewForm::Null };
    constexpr std::array<int, 2> kArgCounts{ 0, 1 };

    const speculo::Method method = speculo::class_of<RValueTarget>().get_method<>("take").unwrap();
    for (ViewForm form : kForms)
    {
        for (int arg_count : kArgCounts)
        {
            SCOPED_TRACE(std::to_string(static_cast<int>(form)) + "x" + std::to_string(arg_count));

            RValueTarget       object;
            const RValueTarget const_object;
            volatile RValueTarget volatile_object;
            Other              other;
            int                extra = 1;

            speculo::ObjectRef receiver;
            switch (form)
            {
            case ViewForm::Lvalue:     receiver = speculo::ObjectRef(object); break;
            case ViewForm::RvalueView: receiver = speculo::ObjectRef(object).as_rvalue(); break;
            case ViewForm::Const:      receiver = speculo::ObjectRef(const_object); break;
            case ViewForm::Volatile:   receiver = speculo::ObjectRef(volatile_object); break;
            case ViewForm::WrongType:  receiver = speculo::ObjectRef(other); break;
            case ViewForm::Null:       break;
            }

            auto result = arg_count == 0 ? method.invoke_rvalue(receiver)
                                         : method.invoke_rvalue(receiver, { extra });

            if (arg_count == 1)
            {
                ASSERT_TRUE(result.is_err());
                EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
                continue;
            }

            switch (form)
            {
            case ViewForm::Null:
                ASSERT_TRUE(result.is_err());
                EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::NullInstance);
                break;
            case ViewForm::Const:
                ASSERT_TRUE(result.is_err());
                EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::ReadOnly);
                break;
            case ViewForm::Volatile:
                ASSERT_TRUE(result.is_err());
                EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::NotSupported);
                break;
            case ViewForm::WrongType:
                ASSERT_TRUE(result.is_err());
                EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
                break;
            default:
                ASSERT_TRUE(result.is_ok());
                EXPECT_EQ(std::move(result).unwrap().as<MoveOnly>().unwrap().value, 7);
                break;
            }
        }
    }
}

TEST(ValueCategoryTest, InvokeRvalueErrorPriorityForLvalueQualifiedMethod)
{
    constexpr std::array<ViewForm, 6> kForms{ ViewForm::Lvalue, ViewForm::RvalueView, ViewForm::Const,
                                              ViewForm::Volatile, ViewForm::WrongType, ViewForm::Null };

    const speculo::Method method = speculo::class_of<RValueTarget>().get_method<>("bump").unwrap();
    for (ViewForm form : kForms)
    {
        SCOPED_TRACE(std::to_string(static_cast<int>(form)));

        RValueTarget       object;
        const RValueTarget const_object;
        volatile RValueTarget volatile_object;
        Other              other;

        speculo::ObjectRef receiver;
        switch (form)
        {
        case ViewForm::Lvalue:     receiver = speculo::ObjectRef(object); break;
        case ViewForm::RvalueView: receiver = speculo::ObjectRef(object).as_rvalue(); break;
        case ViewForm::Const:      receiver = speculo::ObjectRef(const_object); break;
        case ViewForm::Volatile:   receiver = speculo::ObjectRef(volatile_object); break;
        case ViewForm::WrongType:  receiver = speculo::ObjectRef(other); break;
        case ViewForm::Null:       break;
        }

        auto result = method.invoke_rvalue(receiver);
        if (form == ViewForm::Null)
        {
            ASSERT_TRUE(result.is_err());
            EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::NullInstance);
        }
        else if (form == ViewForm::WrongType)
        {
            ASSERT_TRUE(result.is_err());
            EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
        }
        else
        {
            ASSERT_TRUE(result.is_err());
            EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::NotSupported);
        }
    }
}

} // namespace
