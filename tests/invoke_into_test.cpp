#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/phase12_fixtures.h"

// Method::invoke_into(void* dst, ObjectRef self, Args args) / (Object& dst, ...)
// Function::invoke_into(void* dst, Args args) / (Object& dst, ...)
//   —— 参数值类见 .agents/testing.md §2；组合见 §3；错误优先级见 api_design §0.1：
//      实参个数 → 接收者空 → 接收者类型 → ref-qualifier → const → volatile → 逐参，
//      然后才是 invoke_into 自己的能力检查。
//
// dst 形态 \ 被调种类 | 标量 | void | 非平凡 | 静态 | 返回引用 | && 限定
//   void* 正确对齐    | Ok   | Ok   | Ok     | Ok   | NotSupported | NotSupported
//   Object 类型正确   | Ok   | InvalidArgument | Ok | Ok | NotSupported | NotSupported
//   Object 空         | InvalidArgument（所有列；void 也是先判 void）
//   Object 类型不符   | TypeMismatch（返回引用 / && 的 decayed 仍是 int，所以同样 TypeMismatch）
//   void* 为空        | panic（前置条件）
//   void* 未对齐      | panic（前置条件）
//
// 分配计数（§9）：
//   invoke_into        | 0 次分配
//   invoke（无分配器） | 1 次分配，析构时释放
//   invoke（默认分配器）| 1 次分配，释放走 allocator
//
// 接收者维度（V3/V4）单独覆盖：空 → NullInstance；错类型 → TypeMismatch；
// const → ReadOnly；volatile → NotSupported。

namespace
{

class CountingAllocator final : public speculo::Allocator
{
public:
    std::size_t allocations = 0;
    std::size_t deallocations = 0;
    std::size_t last_size = 0;
    std::size_t last_alignment = 0;

    [[nodiscard]] auto allocate(std::size_t size, std::size_t alignment) -> void* override
    {
        ++allocations;
        last_size = size;
        last_alignment = alignment;

        void* block = alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__
            ? ::operator new(size, std::align_val_t(alignment))
            : ::operator new(size);
        m_alignments.emplace(block, alignment);

        return block;
    }

    void deallocate(void* block) noexcept override
    {
        ++deallocations;

        const std::size_t alignment = m_alignments[block];
        m_alignments.erase(block);
        if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
        {
            ::operator delete(block, std::align_val_t(alignment));
        }
        else
        {
            ::operator delete(block);
        }
    }

private:
    std::unordered_map<void*, std::size_t> m_alignments;
};

class RefusingAllocator final : public speculo::Allocator
{
public:
    [[nodiscard]] auto allocate(std::size_t, std::size_t) -> void* override { return nullptr; }
    void deallocate(void*) noexcept override {}
};

class InvokeIntoTest : public ::testing::Test
{
protected:
    void TearDown() override { speculo::Object::set_default_allocator(nullptr); }
};

enum class DstForm
{
    Pointer,
    Object,
    EmptyObject,
    WrongObject,
};

enum class Callee
{
    Scalar,
    Void,
    Text,
    Static,
    Reference,
    Rvalue,
};

struct CalleeInfo
{
    const char* name;
    speculo::Method method;
    bool needs_argument;
    bool returns_text;
};

auto callee_info(Callee callee) -> CalleeInfo
{
    const speculo::Class& cls = speculo::class_of<P12Counter>();
    switch (callee)
    {
        case Callee::Scalar:
            return { "scalar", cls.get_method("get").unwrap(), false, false };
        case Callee::Void:
            return { "void", cls.get_method("set").unwrap(), true, false };
        case Callee::Text:
            return { "text", cls.get_method("label").unwrap(), false, true };
        case Callee::Static:
            return { "static", cls.get_method("twice").unwrap(), true, false };
        case Callee::Reference:
            return { "reference", cls.get_method("slot").unwrap(), false, false };
        case Callee::Rvalue:
            return { "rvalue", cls.get_method("consume").unwrap(), false, false };
    }

    return { "?", speculo::Method{}, false, false };
}

/// Expectation for the matrix: success maps to std::nullopt.
auto expected_kind(Callee callee, DstForm form) -> std::optional<speculo::ErrorKind>
{
    if (callee == Callee::Void)
    {
        return form == DstForm::Pointer ? std::nullopt : std::optional(speculo::ErrorKind::InvalidArgument);
    }

    if (callee == Callee::Reference || callee == Callee::Rvalue)
    {
        switch (form)
        {
            case DstForm::Pointer:
            case DstForm::Object:
                return speculo::ErrorKind::NotSupported;
            case DstForm::EmptyObject:
                return speculo::ErrorKind::InvalidArgument;
            case DstForm::WrongObject:
                return speculo::ErrorKind::TypeMismatch;
        }
    }

    switch (form)
    {
        case DstForm::Pointer:
        case DstForm::Object:
            return std::nullopt;
        case DstForm::EmptyObject:
            return speculo::ErrorKind::InvalidArgument;
        case DstForm::WrongObject:
            return speculo::ErrorKind::TypeMismatch;
    }

    return std::nullopt;
}

} // namespace

TEST_F(InvokeIntoTest, DestinationByCalleeMatrix)
{
    constexpr std::array<Callee, 6> callees{ Callee::Scalar, Callee::Void, Callee::Text,
                                             Callee::Static, Callee::Reference, Callee::Rvalue };
    constexpr std::array<DstForm, 4> forms{ DstForm::Pointer, DstForm::Object, DstForm::EmptyObject,
                                            DstForm::WrongObject };

    for (const Callee callee : callees)
    {
        const CalleeInfo info = callee_info(callee);
        P12Counter counter(5);
        int argument = 4;

        int         int_storage = 0;
        std::string text_storage;
        void*       pointer = nullptr;
        if (info.returns_text)
        {
            pointer = std::addressof(text_storage);
        }
        else if (callee != Callee::Void)
        {
            pointer = std::addressof(int_storage);
        }

        for (const DstForm form : forms)
        {
            SCOPED_TRACE(std::format("{} / {}", info.name, static_cast<int>(form)));

            speculo::Status status = speculo::None{};
            switch (form)
            {
                case DstForm::Pointer:
                    status = info.method.invoke_into(pointer, counter, info.needs_argument ? speculo::Args{ argument }
                                                                                          : speculo::Args{});
                    break;
                case DstForm::Object:
                {
                    speculo::Object destination = info.returns_text
                        ? speculo::Object::from_value(std::string("before")).unwrap()
                        : speculo::Object::from_value(0).unwrap();
                    status = info.method.invoke_into(destination, counter,
                                                     info.needs_argument ? speculo::Args{ argument } : speculo::Args{});
                    break;
                }
                case DstForm::EmptyObject:
                {
                    speculo::Object destination;
                    status = info.method.invoke_into(destination, counter,
                                                     info.needs_argument ? speculo::Args{ argument } : speculo::Args{});
                    break;
                }
                case DstForm::WrongObject:
                {
                    speculo::Object destination = info.returns_text
                        ? speculo::Object::from_value(0).unwrap()
                        : speculo::Object::from_value(std::string("wrong")).unwrap();
                    status = info.method.invoke_into(destination, counter,
                                                     info.needs_argument ? speculo::Args{ argument } : speculo::Args{});
                    break;
                }
            }

            const std::optional<speculo::ErrorKind> expected = expected_kind(callee, form);
            if (expected.has_value())
            {
                ASSERT_TRUE(status.is_err());
                EXPECT_EQ(status.unwrap_err().kind, *expected);
            }
            else
            {
                EXPECT_TRUE(status.is_ok());
            }
        }
    }
}

TEST_F(InvokeIntoTest, WritesScalarResultIntoStorage)
{
    const speculo::Class& cls = speculo::class_of<P12Counter>();
    const speculo::Method get = cls.get_method("get").unwrap();

    P12Counter counter(41);
    int result = 0;

    const speculo::Status status = get.invoke_into(&result, counter);
    ASSERT_TRUE(status.is_ok());
    EXPECT_EQ(result, 41);

    counter.set(7);
    EXPECT_TRUE(get.invoke_into(&result, counter).is_ok());
    EXPECT_EQ(result, 7);
}

TEST_F(InvokeIntoTest, ConstructsNonTrivialResult)
{
    const speculo::Class& cls = speculo::class_of<P12Counter>();
    const speculo::Method label = cls.get_method("label").unwrap();

    P12Counter counter;
    std::string result;

    ASSERT_TRUE(label.invoke_into(&result, counter).is_ok());
    EXPECT_EQ(result, "P12Counter");
}

TEST_F(InvokeIntoTest, ObjectOverloadWritesInPlace)
{
    const speculo::Class& cls = speculo::class_of<P12Counter>();
    const speculo::Method get = cls.get_method("get").unwrap();

    P12Counter counter(3);
    speculo::Object destination = speculo::Object::from_value(0).unwrap();

    ASSERT_TRUE(get.invoke_into(destination, counter).is_ok());
    EXPECT_EQ(destination.as<int>().unwrap(), 3);

    counter.set(4);
    ASSERT_TRUE(get.invoke_into(destination, counter).is_ok());
    EXPECT_EQ(destination.as<int>().unwrap(), 4);
}

TEST_F(InvokeIntoTest, ReceiverValueClasses)
{
    const speculo::Class& cls = speculo::class_of<P12Counter>();
    const speculo::Method get = cls.get_method("get").unwrap();
    int result = 0;

    // V3: null receiver.
    EXPECT_EQ(get.invoke_into(&result, speculo::ObjectRef{}).unwrap_err().kind,
              speculo::ErrorKind::NullInstance);

    // V2: wrong static type.
    P12Aggregate other{ 1, 2 };
    EXPECT_EQ(get.invoke_into(&result, other).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    // V4: const receiver + non-const method (get is const, so use set).
    const speculo::Method set = cls.get_method("set").unwrap();
    const P12Counter const_counter(1);
    int argument = 2;
    EXPECT_EQ(set.invoke_into(nullptr, const_counter, { argument }).unwrap_err().kind,
              speculo::ErrorKind::ReadOnly);

    // V4: volatile receiver + non-volatile method.
    volatile P12Counter volatile_counter(1);
    EXPECT_EQ(get.invoke_into(&result, volatile_counter).unwrap_err().kind,
              speculo::ErrorKind::NotSupported);
}

TEST_F(InvokeIntoTest, ArgumentBindingAndPromotion)
{
    const speculo::Class& cls = speculo::class_of<P12Counter>();
    const speculo::Method bump = cls.get_method("bump").unwrap();
    P12Counter counter;

    // Exact lvalue binds to `int&` and the callee writes through it.
    int value = 1;
    ASSERT_TRUE(bump.invoke_into(nullptr, counter, { value }).is_ok());
    EXPECT_EQ(value, 2);

    // A const argument cannot bind to a non-const reference.
    const int constant = 1;
    EXPECT_EQ(bump.invoke_into(nullptr, counter, { constant }).unwrap_err().kind,
              speculo::ErrorKind::TypeMismatch);

    // No numeric promotion: `short` is not `int`.
    const speculo::Method set = cls.get_method("set").unwrap();
    short small = 1;
    EXPECT_EQ(set.invoke_into(nullptr, counter, { small }).unwrap_err().kind,
              speculo::ErrorKind::TypeMismatch);
}

TEST_F(InvokeIntoTest, DeletedAndVariadicAreNotSupported)
{
    const speculo::Class& cls = speculo::class_of<P12Uninvokable>();
    P12Uninvokable value;
    int argument = 1;

    const speculo::Method removed = cls.get_method("removed").unwrap();
    EXPECT_TRUE(removed.is_deleted());
    EXPECT_EQ(removed.invoke_into(nullptr, value, { argument }).unwrap_err().kind,
              speculo::ErrorKind::NotSupported);

    const speculo::Method varargs = cls.get_method("varargs").unwrap();
    EXPECT_TRUE(varargs.is_variadic());
    int result = 0;
    EXPECT_EQ(varargs.invoke_into(&result, value, { argument }).unwrap_err().kind,
              speculo::ErrorKind::NotSupported);
}

TEST_F(InvokeIntoTest, ConversionOperatorGoesThroughInvokeInto)
{
    const speculo::Class& cls = speculo::class_of<P12Uninvokable>();
    P12Uninvokable value;

    speculo::Method conversion;
    for (const speculo::Method& method : cls.get_methods())
    {
        if (method.is_conversion_operator())
        {
            conversion = method;
        }
    }

    ASSERT_FALSE(conversion.get_name().empty());
    int result = 0;
    ASSERT_TRUE(conversion.invoke_into(&result, value).is_ok());
    EXPECT_EQ(result, 7);
}

TEST_F(InvokeIntoTest, ManyArgumentsUseTheHeapPath)
{
    const speculo::Class& cls = speculo::class_of<P12ManyArgs>();
    const speculo::Method sum = cls.get_method("sum10").unwrap();
    P12ManyArgs value;

    int a1 = 1, a2 = 2, a3 = 3, a4 = 4, a5 = 5, a6 = 6, a7 = 7, a8 = 8, a9 = 9, a10 = 10;
    int result = 0;

    ASSERT_TRUE(sum.invoke_into(&result, value, { a1, a2, a3, a4, a5, a6, a7, a8, a9, a10 }).is_ok());
    EXPECT_EQ(result, 55);
}

TEST_F(InvokeIntoTest, NullAndMisalignedDestinationsArePreconditionFailures)
{
    const speculo::Class& cls = speculo::class_of<P12Counter>();
    const speculo::Method get = cls.get_method("get").unwrap();
    P12Counter counter;

    EXPECT_DEATH((void)get.invoke_into(nullptr, counter), "non-null storage");

    alignas(int) char buffer[2 * sizeof(int)]{};
    int* misaligned = reinterpret_cast<int*>(buffer + 1);
    EXPECT_DEATH((void)get.invoke_into(misaligned, counter), "not aligned");
}

TEST_F(InvokeIntoTest, DoesNotAllocateWhileInvokeDoes)
{
    const speculo::Class& cls = speculo::class_of<P12Counter>();
    const speculo::Method get = cls.get_method("get").unwrap();
    P12Counter counter(11);

    CountingAllocator allocator;
    speculo::Object::set_default_allocator(&allocator);

    int result = 0;
    ASSERT_TRUE(get.invoke_into(&result, counter).is_ok());
    EXPECT_EQ(result, 11);
    EXPECT_EQ(allocator.allocations, 0u);

    {
        speculo::Outcome<speculo::Object> boxed = get.invoke(counter);
        ASSERT_TRUE(boxed.is_ok());
        EXPECT_EQ(allocator.allocations, 1u);
        EXPECT_EQ(allocator.deallocations, 0u);
        EXPECT_EQ(std::move(boxed).unwrap().as<int>().unwrap(), 11);
    }

    EXPECT_EQ(allocator.deallocations, 1u);
}

// ── Function::invoke_into ──

namespace
{

enum class FunctionKind
{
    Scalar,
    Void,
    Reference,
    Variadic,
};

struct FunctionInfo
{
    const char*              name;
    const speculo::Function* function;
};

auto function_info(FunctionKind kind) -> FunctionInfo
{
    switch (kind)
    {
        case FunctionKind::Scalar:
            return { "add", &speculo::function_of<&p12_add>() };
        case FunctionKind::Void:
            return { "noop", &speculo::function_of<&p12_noop>() };
        case FunctionKind::Reference:
            return { "slot_ref", &speculo::function_of<&p12_slot_ref>() };
        case FunctionKind::Variadic:
            return { "varargs", &speculo::function_of<&p12_varargs>() };
    }

    return { "?", nullptr };
}

auto function_expected_kind(FunctionKind kind, DstForm form) -> std::optional<speculo::ErrorKind>
{
    if (kind == FunctionKind::Void)
    {
        return form == DstForm::Pointer ? std::nullopt : std::optional(speculo::ErrorKind::InvalidArgument);
    }

    if (kind == FunctionKind::Reference || kind == FunctionKind::Variadic)
    {
        switch (form)
        {
            case DstForm::Pointer:
            case DstForm::Object:
                return speculo::ErrorKind::NotSupported;
            case DstForm::EmptyObject:
                return speculo::ErrorKind::InvalidArgument;
            case DstForm::WrongObject:
                return speculo::ErrorKind::TypeMismatch;
        }
    }

    switch (form)
    {
        case DstForm::Pointer:
        case DstForm::Object:
            return std::nullopt;
        case DstForm::EmptyObject:
            return speculo::ErrorKind::InvalidArgument;
        case DstForm::WrongObject:
            return speculo::ErrorKind::TypeMismatch;
    }

    return std::nullopt;
}

} // namespace

TEST_F(InvokeIntoTest, FunctionDestinationByCalleeMatrix)
{
    constexpr std::array<FunctionKind, 4> kinds{ FunctionKind::Scalar, FunctionKind::Void,
                                                 FunctionKind::Reference, FunctionKind::Variadic };
    constexpr std::array<DstForm, 4> forms{ DstForm::Pointer, DstForm::Object, DstForm::EmptyObject,
                                            DstForm::WrongObject };

    P12Counter counter(5);
    int a = 20;
    int b = 22;

    for (const FunctionKind kind : kinds)
    {
        const FunctionInfo info = function_info(kind);

        auto args_for = [&]() -> speculo::Args
        {
            switch (kind)
            {
                case FunctionKind::Scalar:
                    return speculo::Args{ a, b };
                case FunctionKind::Void:
                    return speculo::Args{ a };
                case FunctionKind::Reference:
                    return speculo::Args{ counter };
                case FunctionKind::Variadic:
                    return speculo::Args{ a };
            }

            return speculo::Args{};
        };

        int         int_storage = 0;
        void*       pointer = std::addressof(int_storage);
        if (kind == FunctionKind::Void)
        {
            pointer = nullptr;
        }

        for (const DstForm form : forms)
        {
            SCOPED_TRACE(std::format("{} / {}", info.name, static_cast<int>(form)));

            speculo::Status status = speculo::None{};
            switch (form)
            {
                case DstForm::Pointer:
                    status = info.function->invoke_into(pointer, args_for());
                    break;
                case DstForm::Object:
                {
                    speculo::Object destination = speculo::Object::from_value(0).unwrap();
                    status = info.function->invoke_into(destination, args_for());
                    break;
                }
                case DstForm::EmptyObject:
                {
                    speculo::Object destination;
                    status = info.function->invoke_into(destination, args_for());
                    break;
                }
                case DstForm::WrongObject:
                {
                    speculo::Object destination = speculo::Object::from_value(std::string("wrong")).unwrap();
                    status = info.function->invoke_into(destination, args_for());
                    break;
                }
            }

            const std::optional<speculo::ErrorKind> expected = function_expected_kind(kind, form);
            if (expected.has_value())
            {
                ASSERT_TRUE(status.is_err());
                EXPECT_EQ(status.unwrap_err().kind, *expected);
            }
            else
            {
                EXPECT_TRUE(status.is_ok());
            }
        }
    }
}

TEST_F(InvokeIntoTest, FunctionFlagsAndArgumentErrors)
{
    const speculo::Function& add = speculo::function_of<&p12_add>();
    EXPECT_FALSE(add.is_variadic());
    EXPECT_TRUE(add.is_invokable());

    const speculo::Function& variadic = speculo::function_of<&p12_varargs>();
    EXPECT_TRUE(variadic.is_variadic());

    int a = 1;
    int b = 2;
    int result = 0;

    EXPECT_EQ(add.invoke_into(&result, { a }).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);

    double wrong = 1.5;
    EXPECT_EQ(add.invoke_into(&result, { a, wrong }).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
}
