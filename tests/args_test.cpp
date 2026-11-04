#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/instance_fixtures.h"
#include "support/registry_fixtures.h"

// Args from a run-time sequence (std::vector<ObjectRef> / std::span<const ObjectRef>)
//   —— 参数值类见 .agents/testing.md §2，组合见 §3
// refs \ 元素个数        | 0  | 1 | 8（内联边界） | 9（堆）
//  vector<ObjectRef> 左值 | 空 | 1 | 8            | 9
//  vector<ObjectRef> 临时 | 空 | 1 | 8            | 9
//  span<const ObjectRef>  | 空 | 1 | 8            | 9
//  span<ObjectRef>        | 空 | 1 | 8            | 9
//  额外来源（同属「实参列表」）：std::array<ObjectRef, N>、ObjectRef[N]
//
// 元素形态 × 调用：RuntimeSequenceMatchesBracedList —— 容器路径与花括号列表逐格同值，
//   覆盖 V2（错类型、窄化）/ V4（const、volatile 视图）/ V5（空视图）/ 装箱 Object。
// 调用错误：RuntimeSequenceCallErrors —— 个数不足/超出（InvalidArgument，先于逐参类型）、
//   逐参类型不符（TypeMismatch）、空视图（TypeMismatch，不是 NullInstance）。
// N/A：分配次数。Args 的内联/堆阈值不可从公开 API 观察（.agents/testing.md §8），故不断言。

// The run-time sources convert implicitly; Args itself stays move-only.
static_assert(std::is_convertible_v<const std::vector<speculo::ObjectRef>&, speculo::Args>);
static_assert(std::is_convertible_v<std::vector<speculo::ObjectRef>, speculo::Args>);
static_assert(std::is_convertible_v<std::span<const speculo::ObjectRef>, speculo::Args>);
static_assert(std::is_convertible_v<std::span<speculo::ObjectRef>, speculo::Args>);
static_assert(std::is_convertible_v<std::array<speculo::ObjectRef, 2>, speculo::Args>);
static_assert(!std::is_copy_constructible_v<speculo::Args>);
static_assert(std::is_move_constructible_v<speculo::Args>);

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

namespace
{

// ── run-time assembled arguments ──

enum class SequenceSource
{
    VectorLvalue,
    VectorTemporary,
    ConstSpan,
    MutableSpan,
};

/// Builds the same argument list through one of the run-time sources.
auto assemble_args(SequenceSource source, std::vector<speculo::ObjectRef>& refs) -> speculo::Args
{
    switch (source)
    {
    case SequenceSource::VectorLvalue:
        return speculo::Args(refs);
    case SequenceSource::VectorTemporary:
        return speculo::Args(std::vector<speculo::ObjectRef>(refs));
    case SequenceSource::ConstSpan:
    {
        std::span<const speculo::ObjectRef> view{ refs };
        return speculo::Args(view);
    }
    case SequenceSource::MutableSpan:
    {
        std::span<speculo::ObjectRef> view{ refs };
        return speculo::Args(view);
    }
    }

    return speculo::Args{};
}

/// Observable outcome of a call: the boxed int result, or the error kind.
struct CallResult
{
    bool               ok = false;
    int                value = 0;
    speculo::ErrorKind kind = speculo::ErrorKind::NotFound; // meaningless when ok
};

auto record(speculo::Outcome<speculo::Object> outcome) -> CallResult
{
    if (outcome.is_err())
    {
        return CallResult{ false, 0, std::move(outcome).unwrap_err().kind };
    }

    return CallResult{ true, std::move(outcome).unwrap().as<int>().unwrap(), speculo::ErrorKind::NotFound };
}

/// Element forms for the second argument of Greeter::scaled(Vec, int); the first
/// one is always a matching Vec, so each row isolates one argument check.
enum class ElementForm
{
    Matching,         // int lvalue                   -> Ok
    ConstMatching,    // const int lvalue             -> Ok (copied by value)
    VolatileMatching, // volatile int lvalue          -> TypeMismatch (a by-value read)
    WrongType,        // double                       -> TypeMismatch
    NarrowedType,     // short (no numeric promotion) -> TypeMismatch
    NullView,         // ObjectRef{}                  -> TypeMismatch
    Boxed,            // Object holding int           -> Ok
};

struct ElementFormRow
{
    ElementForm        form;
    bool               ok;
    int                value;
    speculo::ErrorKind kind;
};

constexpr ElementFormRow kElementForms[] = {
    { ElementForm::Matching, true, 9, speculo::ErrorKind::NotFound },
    { ElementForm::ConstMatching, true, 9, speculo::ErrorKind::NotFound },
    { ElementForm::VolatileMatching, false, 0, speculo::ErrorKind::TypeMismatch },
    { ElementForm::WrongType, false, 0, speculo::ErrorKind::TypeMismatch },
    { ElementForm::NarrowedType, false, 0, speculo::ErrorKind::TypeMismatch },
    { ElementForm::NullView, false, 0, speculo::ErrorKind::TypeMismatch },
    { ElementForm::Boxed, true, 9, speculo::ErrorKind::NotFound },
};

} // namespace

TEST(ArgsTest, RuntimeSequenceSources)
{
    constexpr std::size_t kSizes[] = { 0, 1, 8, 9 };
    int values[9] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };

    for (SequenceSource source : { SequenceSource::VectorLvalue, SequenceSource::VectorTemporary,
                                   SequenceSource::ConstSpan, SequenceSource::MutableSpan })
    {
        for (std::size_t count : kSizes)
        {
            SCOPED_TRACE(std::format("source={} count={}", static_cast<int>(source), count));

            std::vector<speculo::ObjectRef> refs;
            for (std::size_t index = 0; index < count; ++index)
            {
                refs.emplace_back(values[index]);
            }

            speculo::Args args = assemble_args(source, refs);

            EXPECT_EQ(args.size(), count);
            EXPECT_EQ(args.empty(), count == 0);
            EXPECT_EQ(args.span().size(), count);
            for (std::size_t index = 0; index < count; ++index)
            {
                EXPECT_EQ(args[index].raw_data(), static_cast<const void*>(&values[index]));
                EXPECT_EQ(args[index].type().get_name(), "int");
                EXPECT_EQ(args[index].as<int>().unwrap(), values[index]);
            }
        }
    }
}

TEST(ArgsTest, RuntimeSequenceAcceptsArraysOfObjectRef)
{
    int two = 2;
    int three = 3;

    std::array<speculo::ObjectRef, 2> packed{ speculo::ObjectRef(two), speculo::ObjectRef(three) };
    speculo::Args from_array(packed);
    ASSERT_EQ(from_array.size(), 2u);
    EXPECT_EQ(from_array[0].raw_data(), static_cast<const void*>(&two));
    EXPECT_EQ(from_array[1].raw_data(), static_cast<const void*>(&three));

    speculo::ObjectRef raw[3] = { speculo::ObjectRef(two), speculo::ObjectRef(three), speculo::ObjectRef(two) };
    speculo::Args from_c_array(raw);
    ASSERT_EQ(from_c_array.size(), 3u);
    EXPECT_EQ(from_c_array[2].raw_data(), static_cast<const void*>(&two));
}

TEST(ArgsTest, RuntimeSequenceCopiesHandlesOnly)
{
    int a = 1;
    int b = 2;

    speculo::Args args;
    {
        std::vector<speculo::ObjectRef> refs{ speculo::ObjectRef(a), speculo::ObjectRef(b) };
        args = speculo::Args(refs);
    } // the container is gone; only the handles were copied

    ASSERT_EQ(args.size(), 2u);
    EXPECT_EQ(args[0].raw_data(), static_cast<const void*>(&a));
    EXPECT_EQ(args[1].raw_data(), static_cast<const void*>(&b));

    // The elements are views: changing the referent is visible through Args.
    a = 42;
    EXPECT_EQ(args[0].as<int>().unwrap(), 42);
}

TEST(ArgsTest, SequenceOfObjectRefIsAnArgumentList)
{
    const speculo::Class& cls = speculo::class_of<Greeter>();
    Greeter greeter;
    Vec vec{ 2, 5 };

    speculo::Method weigh = cls.get_method("weigh").unwrap();

    // One element -> one argument: the array of ObjectRef is not itself the argument.
    std::array<speculo::ObjectRef, 1> one{ speculo::ObjectRef(vec) };
    EXPECT_EQ(weigh.invoke(greeter, one).unwrap().as<int>().unwrap(), 7);

    // Two elements -> two arguments, so the one-parameter method reports an arity error.
    std::array<speculo::ObjectRef, 2> two{ speculo::ObjectRef(vec), speculo::ObjectRef(vec) };
    EXPECT_EQ(weigh.invoke(greeter, two).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
}

TEST(ArgsTest, RuntimeSequenceMatchesBracedList)
{
    const speculo::Class& cls = speculo::class_of<Greeter>();
    Greeter greeter;
    speculo::Method scaled = cls.get_method("scaled").unwrap();

    for (const ElementFormRow& row : kElementForms)
    {
        SCOPED_TRACE(std::format("form={}", static_cast<int>(row.form)));

        Vec vec{ 1, 2 };
        int times = 3;
        const int const_times = 3;
        volatile int volatile_times = 3;
        double as_double = 3.0;
        short as_short = 3;
        speculo::ObjectRef null_view;
        speculo::Object boxed = std::move(speculo::Object::from_value(3).unwrap());

        speculo::ObjectRef second;
        switch (row.form)
        {
        case ElementForm::Matching:
            second = speculo::ObjectRef(times);
            break;
        case ElementForm::ConstMatching:
            second = speculo::ObjectRef(const_times);
            break;
        case ElementForm::VolatileMatching:
            second = speculo::ObjectRef(volatile_times);
            break;
        case ElementForm::WrongType:
            second = speculo::ObjectRef(as_double);
            break;
        case ElementForm::NarrowedType:
            second = speculo::ObjectRef(as_short);
            break;
        case ElementForm::NullView:
            second = null_view;
            break;
        case ElementForm::Boxed:
            second = speculo::ObjectRef(boxed);
            break;
        }

        speculo::Args braced{ speculo::ObjectRef(vec), second };
        speculo::Args assembled(std::vector<speculo::ObjectRef>{ speculo::ObjectRef(vec), second });

        const CallResult from_braced = record(scaled.invoke(greeter, std::move(braced)));
        const CallResult from_sequence = record(scaled.invoke(greeter, std::move(assembled)));

        EXPECT_EQ(from_braced.ok, row.ok);
        EXPECT_EQ(from_sequence.ok, row.ok);
        if (row.ok)
        {
            EXPECT_EQ(from_braced.value, row.value);
            EXPECT_EQ(from_sequence.value, row.value);
        }
        else
        {
            EXPECT_EQ(from_braced.kind, row.kind);
            EXPECT_EQ(from_sequence.kind, row.kind);
        }
    }
}

TEST(ArgsTest, RuntimeSequenceCallErrors)
{
    const speculo::Class& greeter_cls = speculo::class_of<Greeter>();
    Greeter greeter;
    Vec vec{ 1, 2 };
    int times = 3;
    double as_double = 3.0;

    speculo::Method scaled = greeter_cls.get_method("scaled").unwrap();

    // An empty sequence is an empty argument list: the nullary method succeeds.
    Widget widget;
    std::vector<speculo::ObjectRef> none;
    EXPECT_EQ(speculo::class_of<Widget>().get_method("get").unwrap().invoke(widget, none)
                  .unwrap().as<int>().unwrap(),
              1);

    // Too few arguments.
    std::vector<speculo::ObjectRef> one{ speculo::ObjectRef(vec) };
    EXPECT_EQ(scaled.invoke(greeter, one).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);

    // Too many: arity is checked before the per-argument types (api_design §0.1),
    // so the wrong third type never surfaces.
    std::vector<speculo::ObjectRef> three{ speculo::ObjectRef(vec), speculo::ObjectRef(times),
                                          speculo::ObjectRef(as_double) };
    EXPECT_EQ(scaled.invoke(greeter, three).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);

    // Arity fine, one argument type wrong.
    std::vector<speculo::ObjectRef> mismatched{ speculo::ObjectRef(vec), speculo::ObjectRef(as_double) };
    EXPECT_EQ(scaled.invoke(greeter, mismatched).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    // A null view in the list is an argument mismatch, not a null receiver.
    std::vector<speculo::ObjectRef> with_null{ speculo::ObjectRef(vec), speculo::ObjectRef{} };
    EXPECT_EQ(scaled.invoke(greeter, with_null).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    // The same sequence feeds new_instance, which takes the same Args.
    int x = 3;
    int y = 4;
    std::vector<speculo::ObjectRef> fields{ speculo::ObjectRef(x), speculo::ObjectRef(y) };
    auto made = speculo::class_of<Vec>().new_instance(fields);
    ASSERT_TRUE(made.is_ok());
    EXPECT_EQ(std::move(made).unwrap().as<Vec>().unwrap().y, 4);

    // 8 elements stay inline, 10 spill to the heap; both call through.
    const speculo::Class& wide_cls = speculo::class_of<p6b::Wide>();
    p6b::Wide wide;
    int values[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    std::vector<speculo::ObjectRef> eight;
    for (int index = 0; index < 8; ++index)
    {
        eight.emplace_back(values[index]);
    }
    EXPECT_EQ(wide_cls.get_method("sum8").unwrap().invoke(wide, eight).unwrap().as<int>().unwrap(), 36);

    std::vector<speculo::ObjectRef> ten;
    for (int index = 0; index < 10; ++index)
    {
        ten.emplace_back(values[index]); // the elements must outlive the call
    }
    EXPECT_EQ(wide_cls.get_method("sum").unwrap().invoke(wide, ten).unwrap().as<int>().unwrap(), 55);
}
