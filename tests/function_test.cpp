#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

// Cross-TU support (defined in tests/cross_tu_support.cpp, except local_to_main).
namespace cross_tu
{

int local_to_main(int value);
int shared_fn(int value);

} // namespace cross_tu

void cross_tu_register();

namespace fn_tests
{

struct Tag
{
};

struct OtherTag
{
};

struct Box
{
    int value = 0;
};

struct Base
{
    int value = 1;
};

struct Derived : Base
{
    int extra = 2;
};

struct MoveOnly
{
    int value = 0;

    MoveOnly() = default;
    explicit MoveOnly(int v)
        : value(v)
    {
    }
    MoveOnly(const MoveOnly&) = delete;
    auto operator=(const MoveOnly&) -> MoveOnly& = delete;
    MoveOnly(MoveOnly&&) = default;
    auto operator=(MoveOnly&&) -> MoveOnly& = default;
};

enum class Color
{
    red = 1,
    green = 2,
};

// ── original surface ─────────────────────────────────────────────────────────

int add(int left, int right)
{
    return left + right;
}

int scale(int value, int factor)
{
    return value * factor;
}

double scale(double value)
{
    return value * 2.0;
}

int plain(int value) noexcept
{
    return value + 1;
}

int variadic(int fixed, ...)
{
    return fixed;
}

int read_const(const int& value)
{
    return value;
}

int write_ref(int& value)
{
    return ++value;
}

int sink(int&& value)
{
    return value;
}

int& slot()
{
    static int value = 0;
    return value;
}

void set_slot(int value)
{
    slot() = value;
}

const int& frozen()
{
    static const int value = 4;
    return value;
}

[[= Tag{}]] int marked(int value, [[= 3]] int factor)
{
    return value + factor;
}

int take(MoveOnly)
{
    return 1;
}

MoveOnly& move_slot()
{
    static MoveOnly held{7};
    return held;
}

Box operator+(Box left, int right)
{
    return Box{left.value + right};
}

Box operator-(Box value)
{
    value.value = -value.value;
    return value;
}

static int hidden(int value)
{
    return value + 1;
}

int only_by_name(int value)
{
    return value;
}

namespace inner
{

int add(int left, int right)
{
    return left + right;
}

} // namespace inner

// ── arity, values, and return kinds ──────────────────────────────────────────

int nullary()
{
    return 42;
}

int unary(int value)
{
    return value * 2;
}

void nothing()
{
}

bool flip(bool value)
{
    return !value;
}

char bump(char value)
{
    return static_cast<char>(value + 1);
}

unsigned widen(unsigned value)
{
    return value + 1;
}

long long big(long long value)
{
    return value;
}

float half(float value)
{
    return value / 2.0f;
}

double twice_double(double value)
{
    return value * 2.0;
}

std::string echo(std::string value)
{
    return value;
}

std::string concat(const std::string& left, const std::string& right)
{
    return left + right;
}

std::string& name_slot()
{
    static std::string value = "alpha";
    return value;
}

const std::string& frozen_name()
{
    static const std::string value = "beta";
    return value;
}

int* pointer_slot()
{
    static int value = 5;
    return &value;
}

int* null_pointer()
{
    return nullptr;
}

const char* greeting()
{
    return "hello";
}

std::string_view view_of(const std::string& text)
{
    return text;
}

Box make_box(int value)
{
    return Box{value};
}

Color favorite()
{
    return Color::green;
}

std::unique_ptr<int> make_unique(int value)
{
    return std::make_unique<int>(value);
}

const int frozen_value()
{
    return 12;
}

// ── parameter kinds ──────────────────────────────────────────────────────────

int int_value(int value)
{
    return value;
}

unsigned unsigned_value(unsigned value)
{
    return value;
}

long long_value(long value)
{
    return value;
}

long long long_long_value(long long value)
{
    return value;
}

double double_value(double value)
{
    return value;
}

float float_value(float value)
{
    return value;
}

bool bool_value(bool value)
{
    return value;
}

char char_value(char value)
{
    return value;
}

std::size_t size_value(std::size_t value)
{
    return value;
}

short short_value(short value)
{
    return value;
}

int read_volatile(const volatile int& value)
{
    return value;
}

int read_pointer(const int* pointer)
{
    return pointer == nullptr ? -1 : *pointer;
}

int write_pointer(int* pointer)
{
    *pointer += 1;
    return *pointer;
}

int pointer_ref(int*& pointer)
{
    return pointer == nullptr ? -1 : *pointer;
}

int pointer_to_pointer(int** pointer)
{
    return pointer == nullptr || *pointer == nullptr ? -1 : **pointer;
}

int take_int_pointer(int* pointer)
{
    return pointer == nullptr ? 0 : 1;
}

int take_const_int_pointer(const int* pointer)
{
    return pointer == nullptr ? 0 : 1;
}

int take_volatile_int_pointer(volatile int* pointer)
{
    return pointer == nullptr ? 0 : 1;
}

int take_cv_int_pointer(const volatile int* pointer)
{
    return pointer == nullptr ? 0 : 1;
}

int take_void_pointer(void* pointer)
{
    return pointer == nullptr ? 0 : 1;
}

int take_const_void_pointer(const void* pointer)
{
    return pointer == nullptr ? 0 : 1;
}

int take_const_char_pointer(const char* pointer)
{
    return pointer == nullptr ? 0 : 1;
}

int first_of(int values[4])
{
    return values[0];
}

int text_size(const char* text)
{
    return text == nullptr ? -1 : static_cast<int>(std::string_view(text).size());
}

int first_char(char* text)
{
    return text == nullptr ? -1 : text[0];
}

std::size_t view_size(std::string_view text)
{
    return text.size();
}

int read_base(Base value)
{
    return value.value;
}

using IntCallback = int (*)(int);

int callback_a(int value)
{
    return value + 100;
}

int callback_b(int value)
{
    return value + 200;
}

int apply_callback(IntCallback callback, int value)
{
    return callback(value);
}

IntCallback choose_callback(bool first)
{
    return first ? &callback_a : &callback_b;
}

// ── extended fundamental types ───────────────────────────────────────────────

unsigned long unsigned_long_value(unsigned long value)
{
    return value;
}

unsigned long long unsigned_long_long_value(unsigned long long value)
{
    return value;
}

signed char signed_char_value(signed char value)
{
    return value;
}

unsigned char unsigned_char_value(unsigned char value)
{
    return value;
}

unsigned short unsigned_short_value(unsigned short value)
{
    return value;
}

long double long_double_value(long double value)
{
    return value;
}

wchar_t wchar_value(wchar_t value)
{
    return value;
}

char8_t char8_value(char8_t value)
{
    return value;
}

char16_t char16_value(char16_t value)
{
    return value;
}

char32_t char32_value(char32_t value)
{
    return value;
}

std::nullptr_t null_value(std::nullptr_t value)
{
    return value;
}

// ── complex parameter types ──────────────────────────────────────────────────

int sum_array(int (&values)[4])
{
    return values[0] + values[1] + values[2] + values[3];
}

using NoexceptCallback = int (*)(int) noexcept;

int noise_callback(int value) noexcept
{
    return value + 50;
}

int quiet_callback(int value) noexcept
{
    return value - 50;
}

// A function *reference* parameter (`int (&)(int)`) cannot be materialized on
// this toolchain: ObjectRef cannot view a function lvalue and the thunk casts
// void* to a function pointer, which static_cast rejects. Function pointers by
// value (including noexcept ones) are supported and covered below.
int apply_noexcept_callback(NoexceptCallback callback, int value)
{
    return callback(value);
}

struct Calculator
{
    int base = 10;

    int add(int value) const
    {
        return base + value;
    }

    int offset(int value)
    {
        return base + value;
    }
};

using ConstMember = int (Calculator::*)(int) const;
using MutableMember = int (Calculator::*)(int);

int apply_member(const Calculator& object, ConstMember member, int value)
{
    return (object.*member)(value);
}

int color_code(Color color)
{
    return static_cast<int>(color);
}

int consume_string(std::string&& text)
{
    return static_cast<int>(text.size());
}

int consume_box(Box&& box)
{
    return box.value;
}

int consume_const_box(const Box&& box)
{
    return box.value;
}

int span_sum(std::span<int> values)
{
    int total = 0;
    for (int value : values)
    {
        total += value;
    }

    return total;
}

// ── class-value binding matrix ───────────────────────────────────────────────

int take_box(Box box)
{
    return box.value;
}

int take_box_ref(const Box& box)
{
    return box.value;
}

int take_box_rvalue(Box&& box)
{
    return box.value;
}

int take_box_const_rvalue(const Box&& box)
{
    return box.value;
}

// ── extended reference returns ───────────────────────────────────────────────

const volatile int& cv_slot()
{
    static const volatile int value = 33;
    return value;
}

std::string&& movable_name()
{
    static std::string value = "moved";
    return std::move(value);
}

NoexceptCallback choose_noexcept(bool first)
{
    return first ? &noise_callback : &quiet_callback;
}

auto array_slot() -> int (&)[3]
{
    static int values[3] = {1, 2, 3};
    return values;
}

// ── reference return combined with other parameter styles ────────────────────

int& rvalue_slot_storage()
{
    static int slot = 0;
    return slot;
}

int& rvalue_slot(int&& value)
{
    int& slot = rvalue_slot_storage();
    slot = value;
    return slot;
}

int& noexcept_slot() noexcept
{
    static int value = 21;
    return value;
}

int mixed_refs(int a, int& b, int&& c, const int& d, int* e, const std::string& f, bool g, char h,
               std::string&& i)
{
    b += 1;
    return a + b + c + d + *e + static_cast<int>(f.size()) + (g ? 1 : 0) + (h - 'a')
         + static_cast<int>(i.size());
}

int variadic_ref(int& fixed, ...)
{
    return ++fixed;
}

// ── concurrency and reentrancy ───────────────────────────────────────────────

template<int N>
int concurrent_marker(int value)
{
    return value + N;
}

int concurrent_same(int value)
{
    return value + 1;
}

int reentrant_inner(int value)
{
    return value + 1;
}

int reentrant_outer(int value)
{
    const speculo::Function& inner = speculo::function_of<&reentrant_inner>();
    speculo::Outcome<speculo::Object> nested = inner.invoke({value});
    if (nested.is_err())
    {
        return -1;
    }

    return std::move(nested).unwrap().as<int>().unwrap() * 2;
}

int recursive_reflect(int depth)
{
    if (depth <= 0)
    {
        return 0;
    }

    const speculo::Function& self = speculo::function_of<&recursive_reflect>();
    speculo::Outcome<speculo::Object> nested = self.invoke({depth - 1});
    if (nested.is_err())
    {
        return -1;
    }

    return 1 + std::move(nested).unwrap().as<int>().unwrap();
}

int reentrant_lookup(int value)
{
    const speculo::Function& self = speculo::function_of<&reentrant_lookup>();
    return speculo::Function::for_name(self.get_signature()).is_some() ? value : -1;
}

// ── namespaces and linkage ───────────────────────────────────────────────────

inline namespace v1
{

int inline_ns_fn(int value)
{
    return value + 3;
}

} // namespace v1

namespace deep
{

namespace
{

int deep_hidden(int value)
{
    return value + 5;
}

} // namespace

} // namespace deep

int sum8(int a, int b, int c, int d, int e, int f, int g, int h)
{
    return a + b + c + d + e + f + g + h;
}

int sum9(int a, int b, int c, int d, int e, int f, int g, int h, int i)
{
    return a + b + c + d + e + f + g + h + i;
}

int sum12(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j, int k, int l)
{
    return a + b + c + d + e + f + g + h + i + j + k + l;
}

long long mixed9(int a, double b, const std::string& c, int* d, bool e, char f, long long g, float h,
                 std::string_view i)
{
    return a
         + static_cast<long long>(b)
         + static_cast<long long>(c.size())
         + *d
         + (e ? 1 : 0)
         + (f - 'a')
         + g
         + static_cast<long long>(h)
         + static_cast<long long>(i.size());
}

int defaulted(int value, int factor = 3)
{
    return value * factor;
}

template<typename T>
T twice(T value)
{
    return value + value;
}

int& echo_ref(int& value)
{
    return value;
}

int&& movable_slot()
{
    static int value = 9;
    return std::move(value);
}

volatile int& volatile_slot()
{
    static volatile int value = 3;
    return value;
}

MoveOnly& take_and_return(MoveOnly value)
{
    static MoveOnly held;
    held = std::move(value);
    return held;
}

int rewritten_arg(int value, int& slot_ref, MoveOnly&& donor)
{
    slot_ref = value + donor.value;
    return slot_ref;
}

int decorated([[= 1]] int first, [[= 2]] int second)
{
    return first + second;
}

// Annotation placement matrix (function / parameter / multiples / unreadable).

struct PointerTag
{
    const int* pointer = nullptr;
};

[[= Tag{}]] int function_only()
{
    return 1;
}

int parameter_only([[= 1]] int value)
{
    return value;
}

[[= Tag{}]] int both_sides([[= 2]] int value)
{
    return value;
}

[[= Tag{}, = OtherTag{}]] int multi_function()
{
    return 2;
}

int multi_parameter([[= 1, = 2]] int value)
{
    return value;
}

[[= PointerTag{}]] int unreadable_annotation(int value)
{
    return value;
}

// Registry tests.
int never_materialized(int value)
{
    return value;
}

int register_probe(int value)
{
    return value;
}

extern "C" int c_link(int value)
{
    return value + 7;
}

namespace
{

int anonymous_fn(int value)
{
    return value + 2;
}

} // namespace

// ── overload sets ────────────────────────────────────────────────────────────

namespace over
{

int pick(int value)
{
    return value * 10;
}

int pick(int& value)
{
    value += 1;
    return value * 10;
}

const int& pick(const int& value)
{
    return value;
}

int pick(int&& value)
{
    return value * 10 + 1;
}

int pick(int* value)
{
    return value == nullptr ? -1 : *value * 10;
}

int pick(double value)
{
    return static_cast<int>(value * 10);
}

int pick(const char* value)
{
    return value == nullptr ? -1 : static_cast<int>(std::string_view(value).size());
}

int pick(const std::string& value)
{
    return static_cast<int>(value.size()) * 10;
}

int multiply(int left, int right)
{
    return left * right;
}

int arity()
{
    return 0;
}

int arity(int value)
{
    return value;
}

int arity(int left, int right)
{
    return left + right;
}

int arity(int a, int b, int c)
{
    return a + b + c;
}

int noise(int value) noexcept
{
    return value;
}

// A variadic declaration overloaded with a normal one: overload_of must choose
// its second template form for the former.
int variadic_pick(int value, ...)
{
    return value * 2;
}

double variadic_pick(double value)
{
    return value * 3.0;
}

} // namespace over

} // namespace fn_tests

int speculo_fn_global(int value)
{
    return value + 1;
}

// Defined here but materialized from tests/cross_tu_support.cpp.
namespace cross_tu
{

int local_to_main(int value)
{
    return value * 2;
}

} // namespace cross_tu

static int speculo_global_hidden(int value)
{
    return value + 6;
}

namespace
{

using speculo::Args;
using speculo::class_of;
using speculo::ErrorKind;
using speculo::Function;
using speculo::function_of;
using speculo::Object;
using speculo::ObjectRef;
using speculo::Outcome;
using speculo::overload_of;

/// Checks get_parameter_count() and every get_parameter_types() entry.
template<auto Token, typename... Expected>
void expect_parameters()
{
    const Function& fn = function_of<Token>();
    constexpr std::size_t count = sizeof...(Expected);
    ASSERT_EQ(fn.get_parameter_count(), count) << fn.get_signature();

    const std::array<const speculo::Class*, count> expected{
        std::addressof(class_of<Expected>())...
    };
    for (std::size_t i = 0; i < count; ++i)
    {
        EXPECT_EQ(*fn.get_parameter_types()[i], *expected[i]) << fn.get_signature() << " parameter " << i;
    }
}

/// Checks get_return_type() preserves the declared type (references included).
template<auto Token, typename R>
void expect_return()
{
    EXPECT_EQ(function_of<Token>().get_return_type(), class_of<R>()) << function_of<Token>().get_signature();
}

/// Moves the boxed value out and returns it, failing the test on an error.
template<typename T>
auto take_value(Outcome<Object>&& outcome) -> T
{
    Object object = std::move(outcome).unwrap();
    return std::move(object).take<T>().unwrap();
}

void expect_invoke_error(const Outcome<Object>& outcome, ErrorKind kind)
{
    ASSERT_TRUE(outcome.is_err());
    EXPECT_EQ(outcome.unwrap_err().kind, kind);
}

void expect_ref_error(const Outcome<ObjectRef>& outcome, ErrorKind kind)
{
    ASSERT_TRUE(outcome.is_err());
    EXPECT_EQ(outcome.unwrap_err().kind, kind);
}

/// Both public views of an Option's state, so a test can assert "Some" and
/// "None" together instead of only unwrapping the happy path.
template<typename T>
[[nodiscard]] auto option_state(const speculo::Option<T>& option) -> std::array<bool, 2>
{
    return {option.is_some(), option.is_none()};
}

/// Eight distinct function-template specializations, materialized from worker
/// threads. The switch keeps each function_of<> token compile-time constant.
[[nodiscard]] auto concurrent_handle(std::size_t index) -> const Function*
{
    switch (index)
    {
        case 0: return &function_of<&fn_tests::concurrent_marker<0>>();
        case 1: return &function_of<&fn_tests::concurrent_marker<1>>();
        case 2: return &function_of<&fn_tests::concurrent_marker<2>>();
        case 3: return &function_of<&fn_tests::concurrent_marker<3>>();
        case 4: return &function_of<&fn_tests::concurrent_marker<4>>();
        case 5: return &function_of<&fn_tests::concurrent_marker<5>>();
        case 6: return &function_of<&fn_tests::concurrent_marker<6>>();
        case 7: return &function_of<&fn_tests::concurrent_marker<7>>();
        default: break;
    }

    return nullptr;
}

} // namespace

namespace
{

TEST(FunctionTest, InvokeUniqueFunction)
{
    const speculo::Function& add = speculo::function_of<fn_tests::add>();
    EXPECT_EQ(add.get_name(), "add");
    EXPECT_EQ(add.get_namespace(), "fn_tests");
    EXPECT_EQ(add.get_qualified_name(), "fn_tests::add");
    EXPECT_EQ(add.get_parameter_count(), 2u);
    EXPECT_EQ(*add.get_parameter_types()[0], speculo::class_of<int>());
    EXPECT_EQ(add.get_return_type(), speculo::class_of<int>());
    EXPECT_FALSE(add.is_noexcept());
    EXPECT_TRUE(add.is_invokable());
    EXPECT_EQ(add.get_signature(), "int fn_tests::add(int, int)");

    int left = 2;
    int right = 3;
    auto sum = add.invoke({left, right});
    ASSERT_TRUE(sum.is_ok());
    EXPECT_EQ(std::move(sum).unwrap().as<int>().unwrap(), 5);

    EXPECT_EQ(add, speculo::function_of<&fn_tests::add>());
    EXPECT_EQ(add, speculo::function_of<^^fn_tests::add>());
}

TEST(FunctionTest, OverloadSelection)
{
    const speculo::Function& by_int = speculo::function_of<speculo::overload_of<int, int>(fn_tests::scale)>();
    const speculo::Function& by_double = speculo::function_of<speculo::overload_of<double>(fn_tests::scale)>();

    EXPECT_NE(by_int, by_double);
    EXPECT_EQ(by_int.get_return_type(), speculo::class_of<int>());
    EXPECT_EQ(by_double.get_return_type(), speculo::class_of<double>());

    int value = 6;
    int factor = 7;
    EXPECT_EQ(std::move(by_int.invoke({value, factor})).unwrap().as<int>().unwrap(), 42);

    double width = 1.5;
    EXPECT_DOUBLE_EQ(std::move(by_double.invoke({width})).unwrap().as<double>().unwrap(), 3.0);
}

TEST(FunctionTest, RejectsBadArguments)
{
    const speculo::Function& add = speculo::function_of<fn_tests::add>();
    int left = 1;
    auto missing = add.invoke({left});
    EXPECT_EQ(missing.unwrap_err().kind, speculo::ErrorKind::InvalidArgument);

    double right = 2.0;
    auto mismatch = add.invoke({left, right});
    EXPECT_EQ(mismatch.unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
}

TEST(FunctionTest, NoexceptSignatureAndVoidCall)
{
    const speculo::Function& plain = speculo::function_of<fn_tests::plain>();
    EXPECT_TRUE(plain.is_noexcept());
    EXPECT_TRUE(plain.modifiers().is_noexcept());
    EXPECT_EQ(plain.get_signature(), "int fn_tests::plain(int) noexcept");

    int value = 10;
    EXPECT_EQ(std::move(plain.invoke({value})).unwrap().as<int>().unwrap(), 11);

    const speculo::Function& set_slot = speculo::function_of<fn_tests::set_slot>();
    EXPECT_EQ(set_slot.get_return_type(), speculo::class_of<void>());
    int next = 8;
    auto wrote = set_slot.invoke({next});
    ASSERT_TRUE(wrote.is_ok());
    EXPECT_TRUE(std::move(wrote).unwrap().is_null());
    EXPECT_EQ(fn_tests::slot(), 8);
}

TEST(FunctionTest, ReferenceArgumentsAndRvalueForwarding)
{
    const speculo::Function& read_const = speculo::function_of<fn_tests::read_const>();
    const int frozen_value = 5;
    EXPECT_EQ(std::move(read_const.invoke({frozen_value})).unwrap().as<int>().unwrap(), 5);

    const speculo::Function& write_ref = speculo::function_of<fn_tests::write_ref>();
    EXPECT_EQ(write_ref.invoke({frozen_value}).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    int mutable_value = 5;
    EXPECT_EQ(std::move(write_ref.invoke({mutable_value})).unwrap().as<int>().unwrap(), 6);
    EXPECT_EQ(mutable_value, 6);

    const speculo::Function& sink = speculo::function_of<fn_tests::sink>();
    int donor = 9;
    EXPECT_EQ(std::move(sink.invoke({donor})).unwrap().as<int>().unwrap(), 9);
}

TEST(FunctionTest, ReferenceReturnsCopyOrView)
{
    const speculo::Function& slot = speculo::function_of<fn_tests::slot>();
    EXPECT_TRUE(slot.returns_reference());
    EXPECT_TRUE(slot.is_ref_invokable());

    fn_tests::slot() = 3;
    speculo::Object copied = std::move(slot.invoke({})).unwrap();
    EXPECT_EQ(copied.as<int>().unwrap(), 3);

    speculo::ObjectRef viewed = std::move(slot.invoke_ref({})).unwrap();
    viewed.as<int>().unwrap() = 11;
    EXPECT_EQ(fn_tests::slot(), 11);

    const speculo::Function& frozen = speculo::function_of<fn_tests::frozen>();
    speculo::ObjectRef const_view = std::move(frozen.invoke_ref({})).unwrap();
    EXPECT_TRUE(const_view.is_const());
    EXPECT_EQ(const_view.as<const int>().unwrap(), 4);

    const speculo::Function& add = speculo::function_of<fn_tests::add>();
    int left = 1;
    int right = 1;
    EXPECT_EQ(add.invoke_ref({left, right}).unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(FunctionTest, VariadicIsListedButNotInvokable)
{
    int value = 1;
    const speculo::Function& variadic = speculo::function_of<fn_tests::variadic>();
    EXPECT_TRUE(variadic.is_variadic());
    EXPECT_FALSE(variadic.is_invokable());
    EXPECT_FALSE(variadic.is_ref_invokable());
    EXPECT_EQ(variadic.get_parameter_count(), 1u);
    speculo::Error error = std::move(variadic.invoke({value})).unwrap_err();
    EXPECT_EQ(error.kind, speculo::ErrorKind::NotSupported);
    EXPECT_NE(error.message.find("variadic"), std::string::npos);

    expect_ref_error(variadic.invoke_ref({value}), speculo::ErrorKind::NotSupported);
}

TEST(FunctionTest, AnnotationsOnTheFunctionAndAParameter)
{
    const speculo::Function& marked = speculo::function_of<fn_tests::marked>();
    EXPECT_TRUE(marked.get_annotation<fn_tests::Tag>().is_some());
    EXPECT_TRUE(marked.get_annotation<fn_tests::OtherTag>().is_none());
    EXPECT_TRUE(marked.get_parameter_annotations(0).empty());
    EXPECT_EQ(marked.get_parameter_annotation<int>(1).unwrap(), 3);

    int value = 4;
    int factor = 5;
    EXPECT_EQ(std::move(marked.invoke({value, factor})).unwrap().as<int>().unwrap(), 9);

    const speculo::Function& decorated = speculo::function_of<fn_tests::decorated>();
    ASSERT_EQ(decorated.get_parameter_annotations(0).size(), 1u);
    ASSERT_EQ(decorated.get_parameter_annotations(1).size(), 1u);
    EXPECT_EQ(decorated.get_parameter_annotation<int>(0).unwrap(), 1);
    EXPECT_EQ(decorated.get_parameter_annotation<int>(1).unwrap(), 2);
    EXPECT_EQ(decorated.get_parameter_annotations(0)[0].get_type(), speculo::class_of<int>());
    EXPECT_TRUE(decorated.get_parameter_annotations(0)[0].is_readable());
    EXPECT_EQ(decorated.get_parameter_annotations(0)[0].value().unwrap().as<int>().unwrap(), 1);
    EXPECT_TRUE(decorated.get_parameter_annotation<fn_tests::Tag>(0).is_none());
    EXPECT_TRUE(decorated.get_annotations().empty());
}

TEST(FunctionTest, MoveOnlySignaturesDoNotBreakMaterialization)
{
    const speculo::Function& take = speculo::function_of<fn_tests::take>();
    EXPECT_FALSE(take.is_invokable());
    fn_tests::MoveOnly donor;
    donor.value = 3;
    EXPECT_EQ(take.invoke({donor}).unwrap_err().kind, speculo::ErrorKind::NotSupported);

    const speculo::Function& slot = speculo::function_of<fn_tests::move_slot>();
    EXPECT_FALSE(slot.is_invokable());
    EXPECT_TRUE(slot.is_ref_invokable());
    speculo::ObjectRef viewed = std::move(slot.invoke_ref({})).unwrap();
    EXPECT_EQ(viewed.as<fn_tests::MoveOnly>().unwrap().value, 7);

    const speculo::Function& take_and_return = speculo::function_of<fn_tests::take_and_return>();
    EXPECT_TRUE(take_and_return.returns_reference());
    EXPECT_FALSE(take_and_return.is_invokable());
    EXPECT_FALSE(take_and_return.is_ref_invokable());
    fn_tests::MoveOnly second;
    second.value = 11;
    EXPECT_EQ(take_and_return.invoke({second}).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    expect_ref_error(take_and_return.invoke_ref({second}), speculo::ErrorKind::NotSupported);
}

TEST(FunctionTest, OperatorAndNestedNamespaceAndGlobal)
{
    const speculo::Function& plus = speculo::function_of<&fn_tests::operator+>();
    EXPECT_TRUE(plus.is_operator());
    // GCC gives operator functions no identifier: get_name() is the display string.
    EXPECT_NE(plus.get_name().find("operator+"), std::string_view::npos);
    fn_tests::Box box{4};
    int right = 3;
    EXPECT_EQ(std::move(plus.invoke({box, right})).unwrap().as<fn_tests::Box>().unwrap().value, 7);

    const speculo::Function& minus = speculo::function_of<&fn_tests::operator - >();
    EXPECT_TRUE(minus.is_operator());
    EXPECT_NE(minus.get_name().find("operator-"), std::string_view::npos);
    fn_tests::Box negative{5};
    EXPECT_EQ(std::move(minus.invoke({negative})).unwrap().as<fn_tests::Box>().unwrap().value, -5);

    const speculo::Function& inner = speculo::function_of<fn_tests::inner::add>();
    EXPECT_EQ(inner.get_namespace(), "fn_tests::inner");
    EXPECT_EQ(inner.get_qualified_name(), "fn_tests::inner::add");
    EXPECT_EQ(inner.get_signature(), "int fn_tests::inner::add(int, int)");

    const speculo::Function& global = speculo::function_of<speculo_fn_global>();
    EXPECT_TRUE(global.get_namespace().empty());
    EXPECT_EQ(global.get_qualified_name(), "speculo_fn_global");
    int value = 4;
    EXPECT_EQ(std::move(global.invoke({value})).unwrap().as<int>().unwrap(), 5);

    const speculo::Function& c_link = speculo::function_of<&fn_tests::c_link>();
    EXPECT_TRUE(c_link.get_namespace().empty());
    EXPECT_EQ(c_link.get_qualified_name(), "c_link");
    EXPECT_TRUE(c_link.has_canonical_name());
    EXPECT_EQ(std::move(c_link.invoke({value})).unwrap().as<int>().unwrap(), 11);
}

TEST(FunctionTest, RegistryUsesTheSignatureAndSkipsInternalLinkage)
{
    const speculo::Function& only = speculo::function_of<fn_tests::only_by_name>();
    // for_name before any other check, but function_of above already registered it.
    // A name nobody materializes stays absent; this one is present under its signature.
    EXPECT_TRUE(speculo::Function::for_name("int fn_tests::missing(int)").is_none());
    ASSERT_TRUE(speculo::Function::for_name(only.get_signature()).is_some());
    EXPECT_EQ(speculo::Function::for_name(only.get_signature()).unwrap(), only);

    const std::vector<std::string_view> signatures = speculo::Function::registered_signatures();
    EXPECT_NE(std::ranges::find(signatures, only.get_signature()), signatures.end());

    const speculo::Function& hidden = speculo::function_of<fn_tests::hidden>();
    EXPECT_FALSE(hidden.has_canonical_name());
    EXPECT_TRUE(hidden.is_invokable());
    int value = 10;
    EXPECT_EQ(std::move(hidden.invoke({value})).unwrap().as<int>().unwrap(), 11);
    EXPECT_TRUE(speculo::Function::for_name(hidden.get_signature()).is_none());
}

// ── argument counts ──────────────────────────────────────────────────────────

TEST(FunctionTest, ArgumentCountMatrix)
{
    const Function& nullary = function_of<&fn_tests::nullary>();
    EXPECT_EQ(nullary.get_parameter_count(), 0u);
    EXPECT_EQ(take_value<int>(nullary.invoke()), 42);       // default Args
    EXPECT_EQ(take_value<int>(nullary.invoke({})), 42);     // explicit empty list
    int extra = 1;
    expect_invoke_error(nullary.invoke({extra}), ErrorKind::InvalidArgument);

    const Function& unary = function_of<&fn_tests::unary>();
    EXPECT_EQ(take_value<int>(unary.invoke({4})), 8);
    expect_invoke_error(unary.invoke({}), ErrorKind::InvalidArgument);
    expect_invoke_error(unary.invoke({4, 5}), ErrorKind::InvalidArgument);

    const Function& add = function_of<&fn_tests::add>();
    expect_invoke_error(add.invoke({1}), ErrorKind::InvalidArgument);
    expect_invoke_error(add.invoke({1, 2, 3}), ErrorKind::InvalidArgument);

    const Function& variadic = function_of<&fn_tests::variadic>();
    expect_invoke_error(variadic.invoke({}), ErrorKind::InvalidArgument);
    // Default arguments are not filled in: the declared parameter list is required.
    const Function& defaulted = function_of<&fn_tests::defaulted>();
    EXPECT_EQ(defaulted.get_parameter_count(), 2u);
    expect_invoke_error(defaulted.invoke({4}), ErrorKind::InvalidArgument);
    EXPECT_EQ(take_value<int>(defaulted.invoke({4, 5})), 20);
}

TEST(FunctionTest, ArgumentCountErrorsNameTheMismatch)
{
    const Function& add = function_of<&fn_tests::add>();
    int value = 1;

    speculo::Error too_few = add.invoke({value}).unwrap_err();
    EXPECT_EQ(too_few.kind, ErrorKind::InvalidArgument);
    EXPECT_NE(too_few.message.find("expected 2"), std::string::npos);
    EXPECT_NE(too_few.message.find("got 1"), std::string::npos);
    EXPECT_NE(too_few.message.find("int fn_tests::add(int, int)"), std::string::npos);

    speculo::Error too_many = add.invoke({value, value, value}).unwrap_err();
    EXPECT_NE(too_many.message.find("got 3"), std::string::npos);
}

// ── by-value copies ──────────────────────────────────────────────────────────

TEST(FunctionTest, ByValueParametersCopyTheirArgument)
{
    const Function& echo = function_of<&fn_tests::echo>();
    std::string text = "hello";
    EXPECT_EQ(take_value<std::string>(echo.invoke({text})), "hello");
    EXPECT_EQ(text, "hello"); // the callee got a copy

    EXPECT_EQ(take_value<std::string>(echo.invoke({std::string("temporary")})), "temporary");

    const Function& concat = function_of<&fn_tests::concat>();
    std::string left = "ab";
    std::string right = "cd";
    EXPECT_EQ(take_value<std::string>(concat.invoke({left, right})), "abcd");
    EXPECT_EQ(left, "ab");
    EXPECT_EQ(right, "cd");

    // A boxed Object is viewed, not moved: a by-value parameter copies it.
    Object boxed = Object::from_value(5).unwrap();
    const Function& unary = function_of<&fn_tests::unary>();
    EXPECT_EQ(take_value<int>(unary.invoke({boxed})), 10);
    EXPECT_EQ(boxed.as<int>().unwrap(), 5);
}

TEST(FunctionTest, ReferenceParametersAliasTheirArgument)
{
    const Function& write_ref = function_of<&fn_tests::write_ref>();
    int value = 1;
    EXPECT_EQ(take_value<int>(write_ref.invoke({value})), 2);
    EXPECT_EQ(value, 2);

    // A temporary can bind to `int&` through the library (Arg views a non-const object).
    EXPECT_EQ(take_value<int>(write_ref.invoke({7})), 8);

    // A boxed Object is the referent, so writes reach the box.
    Object boxed = Object::from_value(10).unwrap();
    EXPECT_EQ(take_value<int>(write_ref.invoke({boxed})), 11);
    EXPECT_EQ(boxed.as<int>().unwrap(), 11);

    // An explicit ObjectRef view aliases its referent too.
    int viewed_value = 20;
    ObjectRef view{viewed_value};
    EXPECT_EQ(take_value<int>(write_ref.invoke({view})), 21);
    EXPECT_EQ(viewed_value, 21);

    int left = 3;
    int right = 4;
    const Function& rewritten = function_of<&fn_tests::rewritten_arg>();
    fn_tests::MoveOnly donor{5};
    EXPECT_EQ(take_value<int>(rewritten.invoke({left, right, std::move(donor)})), 8);
    EXPECT_EQ(right, 8);
}

TEST(FunctionTest, RvalueReferenceParametersForwardTheArgument)
{
    const Function& sink = function_of<&fn_tests::sink>();

    int moved = 9;
    EXPECT_EQ(take_value<int>(sink.invoke({moved})), 9); // explicit choice -> moved from
    EXPECT_EQ(take_value<int>(sink.invoke({11})), 11);   // literal

    const int frozen = 4;
    expect_invoke_error(sink.invoke({frozen}), ErrorKind::TypeMismatch);

    // The error explains that a const object cannot bind to a non-const reference.
    speculo::Error error = sink.invoke({frozen}).unwrap_err();
    EXPECT_NE(error.message.find("const object"), std::string::npos);

    Object boxed = Object::from_value(6).unwrap();
    EXPECT_EQ(take_value<int>(sink.invoke({boxed})), 6);
}

// ── pointers, strings, and exact numeric matching ────────────────────────────

TEST(FunctionTest, PointerArguments)
{
    int number = 5;
    int* pointer = &number;
    const int frozen = 9;
    const int* const_pointer = &frozen;

    const Function& read_pointer = function_of<&fn_tests::read_pointer>();
    EXPECT_EQ(take_value<int>(read_pointer.invoke({const_pointer})), 9);
    // Exact match: `int*` is not `const int*`, even though C++ would convert it.
    expect_invoke_error(read_pointer.invoke({pointer}), ErrorKind::TypeMismatch);

    const Function& write_pointer = function_of<&fn_tests::write_pointer>();
    EXPECT_EQ(take_value<int>(write_pointer.invoke({pointer})), 6);
    EXPECT_EQ(number, 6);
    expect_invoke_error(write_pointer.invoke({const_pointer}), ErrorKind::TypeMismatch);

    const Function& pointer_ref = function_of<&fn_tests::pointer_ref>();
    EXPECT_EQ(take_value<int>(pointer_ref.invoke({pointer})), 6);
    expect_invoke_error(pointer_ref.invoke({const_pointer}), ErrorKind::TypeMismatch);
    // A const pointer object cannot bind to `int*&`.
    int* const immutable_pointer = &number;
    expect_invoke_error(pointer_ref.invoke({immutable_pointer}), ErrorKind::TypeMismatch);

    const Function& pointer_to_pointer = function_of<&fn_tests::pointer_to_pointer>();
    int** double_pointer = &pointer;
    EXPECT_EQ(take_value<int>(pointer_to_pointer.invoke({double_pointer})), 6);
    expect_invoke_error(pointer_to_pointer.invoke({pointer}), ErrorKind::TypeMismatch);

    const int* null_pointer = nullptr;
    EXPECT_EQ(take_value<int>(read_pointer.invoke({null_pointer})), -1);

    // Array parameters are reported (and matched) as pointers.
    const Function& first_of = function_of<&fn_tests::first_of>();
    int values[4] = {7, 6, 5, 4};
    int* first = values;
    EXPECT_EQ(take_value<int>(first_of.invoke({first})), 7);
    expect_invoke_error(first_of.invoke({values}), ErrorKind::TypeMismatch);
}

TEST(FunctionTest, StringAndStringViewArguments)
{
    const Function& text_size = function_of<&fn_tests::text_size>();
    const char* text = "hello";
    EXPECT_EQ(take_value<int>(text_size.invoke({text})), 5);
    EXPECT_EQ(take_value<int>(text_size.invoke({"hello"})), 5); // string literal -> const char*
    EXPECT_EQ(take_value<int>(text_size.invoke({static_cast<const char*>(nullptr)})), -1);

    std::string owned = "hello";
    expect_invoke_error(text_size.invoke({owned}), ErrorKind::TypeMismatch);
    expect_invoke_error(text_size.invoke({std::string("hello")}), ErrorKind::TypeMismatch);
    // `nullptr` itself is std::nullptr_t, not const char*.
    expect_invoke_error(text_size.invoke({nullptr}), ErrorKind::TypeMismatch);

    const Function& first_char = function_of<&fn_tests::first_char>();
    char buffer[6] = "hello";
    char* writable = buffer;
    EXPECT_EQ(take_value<int>(first_char.invoke({writable})), 'h');
    // A string literal is const char*, which does not match char*.
    expect_invoke_error(first_char.invoke({"hello"}), ErrorKind::TypeMismatch);
    // An array lvalue is `char[6]`, not `char*`.
    expect_invoke_error(first_char.invoke({buffer}), ErrorKind::TypeMismatch);

    const Function& view_size = function_of<&fn_tests::view_size>();
    std::string_view view = "hello";
    EXPECT_EQ(take_value<std::size_t>(view_size.invoke({view})), 5u);
    // A string literal decays to const char*, so it does not match std::string_view.
    expect_invoke_error(view_size.invoke({"hello"}), ErrorKind::TypeMismatch);
    expect_invoke_error(view_size.invoke({owned}), ErrorKind::TypeMismatch);
}

enum class ArgForm
{
    MutableLvalue,
    ConstLvalue,
    Literal,
    MovedLvalue,
    Boxed,
    View,
    VolatileLvalue,
    ConstVolatileLvalue,
    Count,
};

constexpr std::size_t kArgFormCount = static_cast<std::size_t>(ArgForm::Count);

constexpr auto arg_form_name(ArgForm form) -> std::string_view
{
    switch (form)
    {
        case ArgForm::MutableLvalue:       return "mutable lvalue";
        case ArgForm::ConstLvalue:         return "const lvalue";
        case ArgForm::Literal:             return "literal";
        case ArgForm::MovedLvalue:         return "moved lvalue";
        case ArgForm::Boxed:               return "boxed Object";
        case ArgForm::View:                return "ObjectRef view";
        case ArgForm::VolatileLvalue:      return "volatile lvalue";
        case ArgForm::ConstVolatileLvalue: return "const volatile lvalue";
        case ArgForm::Count:               break;
    }

    return "?";
}

/// Cartesian product of the int parameter styles and the int argument forms.
class IntBindingMatrix : public ::testing::Test
{
protected:
    int mutable_value = 7;
    const int const_value = 7;
    int moved_value = 7;
    volatile int volatile_value = 7;
    const volatile int const_volatile_value = 7;
    Object boxed = Object::from_value(7).unwrap();
    ObjectRef view{mutable_value};

    [[nodiscard]] auto call(const Function& fn, ArgForm form) -> Outcome<Object>
    {
        switch (form)
        {
            case ArgForm::MutableLvalue:       return fn.invoke({mutable_value});
            case ArgForm::ConstLvalue:         return fn.invoke({const_value});
            case ArgForm::Literal:             return fn.invoke({7});
            case ArgForm::MovedLvalue:         return fn.invoke({std::move(moved_value)});
            case ArgForm::Boxed:               return fn.invoke({boxed});
            case ArgForm::View:                return fn.invoke({view});
            case ArgForm::VolatileLvalue:      return fn.invoke({volatile_value});
            case ArgForm::ConstVolatileLvalue: return fn.invoke({const_volatile_value});
            case ArgForm::Count:               break;
        }

        std::abort();
    }
};

TEST_F(IntBindingMatrix, ParameterStyleByArgumentForm)
{
    struct Row
    {
        std::string_view label;
        const Function* fn = nullptr;
        std::array<bool, kArgFormCount> accepts{};
    };

    const std::array<Row, 5> rows{{
        //                    mutable const  literal moved  boxed  view   volatile const-volatile
        {"int(int)",                 &function_of<&fn_tests::int_value>(),     {true, true, true, true, true, true, false, false}},
        {"int(const int&)",          &function_of<&fn_tests::read_const>(),    {true, true, true, true, true, true, false, false}},
        {"int(int&)",                &function_of<&fn_tests::write_ref>(),     {true, false, true, true, true, true, false, false}},
        {"int(int&&)",               &function_of<&fn_tests::sink>(),          {true, false, true, true, true, true, false, false}},
        {"int(const volatile int&)", &function_of<&fn_tests::read_volatile>(), {true, true, true, true, true, true, true, true}},
    }};

    for (const Row& row : rows)
    {
        for (std::size_t index = 0; index < kArgFormCount; ++index)
        {
            const ArgForm form = static_cast<ArgForm>(index);
            const Outcome<Object> outcome = call(*row.fn, form);

            if (row.accepts[index])
            {
                EXPECT_TRUE(outcome.is_ok()) << row.label << " should accept a " << arg_form_name(form);
            }
            else
            {
                EXPECT_TRUE(outcome.is_err()) << row.label << " should reject a " << arg_form_name(form);
                if (outcome.is_err())
                {
                    EXPECT_EQ(outcome.unwrap_err().kind, ErrorKind::TypeMismatch)
                        << row.label << " / " << arg_form_name(form);
                }
            }
        }
    }
}

enum class NumForm
{
    Int,
    Unsigned,
    Long,
    UnsignedLong,
    LongLong,
    UnsignedLongLong,
    Double,
    LongDouble,
    Float,
    Bool,
    Char,
    SignedChar,
    UnsignedChar,
    Short,
    UnsignedShort,
    WChar,
    Char8,
    Char16,
    Char32,
    Nullptr,
    Count,
};

constexpr std::size_t kNumFormCount = static_cast<std::size_t>(NumForm::Count);

constexpr auto num_form_name(NumForm form) -> std::string_view
{
    switch (form)
    {
        case NumForm::Int:              return "int";
        case NumForm::Unsigned:         return "unsigned";
        case NumForm::Long:             return "long";
        case NumForm::UnsignedLong:     return "unsigned long";
        case NumForm::LongLong:         return "long long";
        case NumForm::UnsignedLongLong: return "unsigned long long";
        case NumForm::Double:           return "double";
        case NumForm::LongDouble:       return "long double";
        case NumForm::Float:            return "float";
        case NumForm::Bool:             return "bool";
        case NumForm::Char:             return "char";
        case NumForm::SignedChar:       return "signed char";
        case NumForm::UnsignedChar:     return "unsigned char";
        case NumForm::Short:            return "short";
        case NumForm::UnsignedShort:    return "unsigned short";
        case NumForm::WChar:            return "wchar_t";
        case NumForm::Char8:            return "char8_t";
        case NumForm::Char16:           return "char16_t";
        case NumForm::Char32:           return "char32_t";
        case NumForm::Nullptr:          return "std::nullptr_t";
        case NumForm::Count:            break;
    }

    return "?";
}

/// Cartesian product of twenty fundamental parameter types and the same twenty
/// argument types. Only the identical type may match: there are no integral
/// promotions or conversions (a type alias such as std::size_t is the same type).
class NumericExactMatchMatrix : public ::testing::Test
{
protected:
    int form_int = 3;
    unsigned form_unsigned = 3u;
    long form_long = 3L;
    unsigned long form_unsigned_long = 3UL;
    long long form_long_long = 3LL;
    unsigned long long form_unsigned_long_long = 3ULL;
    double form_double = 3.0;
    long double form_long_double = 3.0L;
    float form_float = 3.0f;
    bool form_bool = true;
    char form_char = 'c';
    signed char form_signed_char = 3;
    unsigned char form_unsigned_char = 3;
    short form_short = 3;
    unsigned short form_unsigned_short = 3;
    wchar_t form_wchar = L'c';
    char8_t form_char8 = u8'c';
    char16_t form_char16 = u'c';
    char32_t form_char32 = U'c';
    std::nullptr_t form_null = nullptr;

    [[nodiscard]] auto call(const Function& fn, NumForm form) -> Outcome<Object>
    {
        switch (form)
        {
            case NumForm::Int:              return fn.invoke({form_int});
            case NumForm::Unsigned:         return fn.invoke({form_unsigned});
            case NumForm::Long:             return fn.invoke({form_long});
            case NumForm::UnsignedLong:     return fn.invoke({form_unsigned_long});
            case NumForm::LongLong:         return fn.invoke({form_long_long});
            case NumForm::UnsignedLongLong: return fn.invoke({form_unsigned_long_long});
            case NumForm::Double:           return fn.invoke({form_double});
            case NumForm::LongDouble:       return fn.invoke({form_long_double});
            case NumForm::Float:            return fn.invoke({form_float});
            case NumForm::Bool:             return fn.invoke({form_bool});
            case NumForm::Char:             return fn.invoke({form_char});
            case NumForm::SignedChar:       return fn.invoke({form_signed_char});
            case NumForm::UnsignedChar:     return fn.invoke({form_unsigned_char});
            case NumForm::Short:            return fn.invoke({form_short});
            case NumForm::UnsignedShort:    return fn.invoke({form_unsigned_short});
            case NumForm::WChar:            return fn.invoke({form_wchar});
            case NumForm::Char8:            return fn.invoke({form_char8});
            case NumForm::Char16:           return fn.invoke({form_char16});
            case NumForm::Char32:           return fn.invoke({form_char32});
            case NumForm::Nullptr:          return fn.invoke({form_null});
            case NumForm::Count:            break;
        }

        std::abort();
    }
};

TEST_F(NumericExactMatchMatrix, OnlyTheIdenticalTypeMatches)
{
    struct Row
    {
        std::string_view label;
        const Function* fn = nullptr;
    };

    const std::array<Row, kNumFormCount> rows{{
        {"int",                 &function_of<&fn_tests::int_value>()},
        {"unsigned",            &function_of<&fn_tests::unsigned_value>()},
        {"long",                &function_of<&fn_tests::long_value>()},
        {"unsigned long",       &function_of<&fn_tests::unsigned_long_value>()},
        {"long long",           &function_of<&fn_tests::long_long_value>()},
        {"unsigned long long",  &function_of<&fn_tests::unsigned_long_long_value>()},
        {"double",              &function_of<&fn_tests::double_value>()},
        {"long double",         &function_of<&fn_tests::long_double_value>()},
        {"float",               &function_of<&fn_tests::float_value>()},
        {"bool",                &function_of<&fn_tests::bool_value>()},
        {"char",                &function_of<&fn_tests::char_value>()},
        {"signed char",         &function_of<&fn_tests::signed_char_value>()},
        {"unsigned char",       &function_of<&fn_tests::unsigned_char_value>()},
        {"short",               &function_of<&fn_tests::short_value>()},
        {"unsigned short",      &function_of<&fn_tests::unsigned_short_value>()},
        {"wchar_t",             &function_of<&fn_tests::wchar_value>()},
        {"char8_t",             &function_of<&fn_tests::char8_value>()},
        {"char16_t",            &function_of<&fn_tests::char16_value>()},
        {"char32_t",            &function_of<&fn_tests::char32_value>()},
        {"std::nullptr_t",      &function_of<&fn_tests::null_value>()},
    }};

    for (std::size_t expected = 0; expected < kNumFormCount; ++expected)
    {
        for (std::size_t provided = 0; provided < kNumFormCount; ++provided)
        {
            const Outcome<Object> outcome = call(*rows[expected].fn, static_cast<NumForm>(provided));
            const bool matches = expected == provided;

            SCOPED_TRACE(std::format("{} <- {}", rows[expected].label, num_form_name(static_cast<NumForm>(provided))));
            EXPECT_EQ(outcome.is_ok(), matches);

            if (!matches && outcome.is_err())
            {
                EXPECT_EQ(outcome.unwrap_err().kind, ErrorKind::TypeMismatch);
            }
        }
    }
}

TEST(FunctionTest, NumericValuesRoundTrip)
{
    int i = -3;
    unsigned u = 4u;
    long l = 5L;
    unsigned long ul = 6UL;
    long long ll = 7LL;
    unsigned long long ull = 8ULL;
    double d = 1.5;
    long double ld = 2.5L;
    float f = 3.5f;
    bool b = true;
    char c = 'z';
    signed char sc = -3;
    unsigned char uc = 3;
    short h = 9;
    unsigned short uh = 10;
    wchar_t wc = L'w';
    char8_t c8 = u8'a';
    char16_t c16 = u'b';
    char32_t c32 = U'c';
    std::nullptr_t np = nullptr;

    EXPECT_EQ(take_value<int>(function_of<&fn_tests::int_value>().invoke({i})), -3);
    EXPECT_EQ(take_value<unsigned>(function_of<&fn_tests::unsigned_value>().invoke({u})), 4u);
    EXPECT_EQ(take_value<long>(function_of<&fn_tests::long_value>().invoke({l})), 5L);
    EXPECT_EQ(take_value<unsigned long>(function_of<&fn_tests::unsigned_long_value>().invoke({ul})), 6UL);
    EXPECT_EQ(take_value<long long>(function_of<&fn_tests::long_long_value>().invoke({ll})), 7LL);
    EXPECT_EQ(take_value<unsigned long long>(
                  function_of<&fn_tests::unsigned_long_long_value>().invoke({ull})),
              8ULL);
    EXPECT_DOUBLE_EQ(take_value<double>(function_of<&fn_tests::double_value>().invoke({d})), 1.5);
    EXPECT_EQ(take_value<long double>(function_of<&fn_tests::long_double_value>().invoke({ld})), 2.5L);
    EXPECT_FLOAT_EQ(take_value<float>(function_of<&fn_tests::float_value>().invoke({f})), 3.5f);
    EXPECT_TRUE(take_value<bool>(function_of<&fn_tests::bool_value>().invoke({b})));
    EXPECT_EQ(take_value<char>(function_of<&fn_tests::char_value>().invoke({c})), 'z');
    EXPECT_EQ(take_value<signed char>(function_of<&fn_tests::signed_char_value>().invoke({sc})), -3);
    EXPECT_EQ(take_value<unsigned char>(function_of<&fn_tests::unsigned_char_value>().invoke({uc})), 3);
    EXPECT_EQ(take_value<short>(function_of<&fn_tests::short_value>().invoke({h})), 9);
    EXPECT_EQ(take_value<unsigned short>(function_of<&fn_tests::unsigned_short_value>().invoke({uh})), 10);
    EXPECT_EQ(take_value<wchar_t>(function_of<&fn_tests::wchar_value>().invoke({wc})), L'w');
    EXPECT_EQ(take_value<char8_t>(function_of<&fn_tests::char8_value>().invoke({c8})), u8'a');
    EXPECT_EQ(take_value<char16_t>(function_of<&fn_tests::char16_value>().invoke({c16})), u'b');
    EXPECT_EQ(take_value<char32_t>(function_of<&fn_tests::char32_value>().invoke({c32})), U'c');
    EXPECT_EQ(take_value<std::nullptr_t>(function_of<&fn_tests::null_value>().invoke({np})), nullptr);

    // std::size_t is an alias on this target, so it is the very same Class.
    EXPECT_EQ(class_of<std::size_t>(), class_of<unsigned long long>());
    std::size_t s = 8;
    EXPECT_EQ(take_value<std::size_t>(function_of<&fn_tests::size_value>().invoke({s})), 8u);
}

TEST(FunctionTest, ExactMatchRejectsDerivedAndPromotedTypes)
{
    fn_tests::Base base;
    fn_tests::Derived derived;
    const Function& read_base = function_of<&fn_tests::read_base>();
    EXPECT_EQ(take_value<int>(read_base.invoke({base})), 1);
    // No derived-to-base slicing.
    expect_invoke_error(read_base.invoke({derived}), ErrorKind::TypeMismatch);

    // No numeric conversions in either direction.
    const Function& int_value = function_of<&fn_tests::int_value>();
    long wide = 3L;
    expect_invoke_error(int_value.invoke({wide}), ErrorKind::TypeMismatch);
    const Function& double_value = function_of<&fn_tests::double_value>();
    float narrow = 3.0f;
    expect_invoke_error(double_value.invoke({narrow}), ErrorKind::TypeMismatch);
    const Function& bool_value = function_of<&fn_tests::bool_value>();
    int one = 1;
    expect_invoke_error(bool_value.invoke({one}), ErrorKind::TypeMismatch);
}

// ── pointer cv / void pointer matrix ─────────────────────────────────────────

enum class PointerForm
{
    Int,
    ConstPointerObject,
    VolatilePointerObject,
    ConstInt,
    VolatileInt,
    ConstVolatileInt,
    Void,
    ConstVoid,
    ConstChar,
    Null,
    Count,
};

constexpr std::size_t kPointerFormCount = static_cast<std::size_t>(PointerForm::Count);

constexpr auto pointer_form_name(PointerForm form) -> std::string_view
{
    switch (form)
    {
        case PointerForm::Int:                  return "int*";
        case PointerForm::ConstPointerObject:   return "int* const";
        case PointerForm::VolatilePointerObject:return "int* volatile";
        case PointerForm::ConstInt:             return "const int*";
        case PointerForm::VolatileInt:          return "volatile int*";
        case PointerForm::ConstVolatileInt:     return "const volatile int*";
        case PointerForm::Void:                 return "void*";
        case PointerForm::ConstVoid:            return "const void*";
        case PointerForm::ConstChar:            return "const char*";
        case PointerForm::Null:                 return "std::nullptr_t";
        case PointerForm::Count:                break;
    }

    return "?";
}

/// Cartesian product of seven pointer parameter types and ten pointer argument
/// forms. Exact match only: no pointer qualification conversion is applied.
class PointerBindingMatrix : public ::testing::Test
{
protected:
    int number = 1;
    int* int_pointer = &number;
    int* const const_pointer_object = &number;
    int* volatile volatile_pointer_object = &number;
    const int* const_int_pointer = &number;
    volatile int* volatile_int_pointer = &number;
    const volatile int* cv_int_pointer = &number;
    void* void_pointer = &number;
    const void* const_void_pointer = &number;
    const char* char_pointer = "x";
    std::nullptr_t null_pointer = nullptr;

    [[nodiscard]] auto call(const Function& fn, PointerForm form) -> Outcome<Object>
    {
        switch (form)
        {
            case PointerForm::Int:                   return fn.invoke({int_pointer});
            case PointerForm::ConstPointerObject:    return fn.invoke({const_pointer_object});
            case PointerForm::VolatilePointerObject: return fn.invoke({volatile_pointer_object});
            case PointerForm::ConstInt:              return fn.invoke({const_int_pointer});
            case PointerForm::VolatileInt:           return fn.invoke({volatile_int_pointer});
            case PointerForm::ConstVolatileInt:      return fn.invoke({cv_int_pointer});
            case PointerForm::Void:                  return fn.invoke({void_pointer});
            case PointerForm::ConstVoid:             return fn.invoke({const_void_pointer});
            case PointerForm::ConstChar:             return fn.invoke({char_pointer});
            case PointerForm::Null:                  return fn.invoke({null_pointer});
            case PointerForm::Count:                 break;
        }

        std::abort();
    }
};

TEST_F(PointerBindingMatrix, OnlyTheIdenticalPointerTypeMatches)
{
    struct Row
    {
        std::string_view label;
        const Function* fn = nullptr;
        std::array<bool, kPointerFormCount> accepts{};
    };

    const std::array<Row, 7> rows{{
        //                             int*  int*const int*vol const int* vol int* cv int* void* const void* const char* nullptr
        {"int*",                       &function_of<&fn_tests::take_int_pointer>(),
         {true,  true,  false, false, false, false, false, false, false, false}},
        {"const int*",                 &function_of<&fn_tests::take_const_int_pointer>(),
         {false, false, false, true,  false, false, false, false, false, false}},
        {"volatile int*",              &function_of<&fn_tests::take_volatile_int_pointer>(),
         {false, false, false, false, true,  false, false, false, false, false}},
        {"const volatile int*",        &function_of<&fn_tests::take_cv_int_pointer>(),
         {false, false, false, false, false, true,  false, false, false, false}},
        {"void*",                      &function_of<&fn_tests::take_void_pointer>(),
         {false, false, false, false, false, false, true,  false, false, false}},
        {"const void*",                &function_of<&fn_tests::take_const_void_pointer>(),
         {false, false, false, false, false, false, false, true,  false, false}},
        {"const char*",                &function_of<&fn_tests::take_const_char_pointer>(),
         {false, false, false, false, false, false, false, false, true,  false}},
    }};

    for (const Row& row : rows)
    {
        for (std::size_t index = 0; index < kPointerFormCount; ++index)
        {
            const PointerForm form = static_cast<PointerForm>(index);
            const Outcome<Object> outcome = call(*row.fn, form);
            SCOPED_TRACE(std::format("{} <- {}", row.label, pointer_form_name(form)));

            EXPECT_EQ(outcome.is_ok(), row.accepts[index]);
            if (!row.accepts[index] && outcome.is_err())
            {
                EXPECT_EQ(outcome.unwrap_err().kind, ErrorKind::TypeMismatch);
            }
        }
    }

    // A null value is fine as long as the pointer type is exact.
    int* null_int = nullptr;
    EXPECT_EQ(take_value<int>(function_of<&fn_tests::take_int_pointer>().invoke({null_int})), 0);
}

// ── class-value binding matrix (by value / const& / && / const&&) ────────────

enum class BoxForm
{
    MutableLvalue,
    ConstLvalue,
    Temporary,
    MovedLvalue,
    Boxed,
    Count,
};

constexpr std::size_t kBoxFormCount = static_cast<std::size_t>(BoxForm::Count);

constexpr auto box_form_name(BoxForm form) -> std::string_view
{
    switch (form)
    {
        case BoxForm::MutableLvalue: return "mutable lvalue";
        case BoxForm::ConstLvalue:   return "const lvalue";
        case BoxForm::Temporary:     return "temporary";
        case BoxForm::MovedLvalue:   return "moved lvalue";
        case BoxForm::Boxed:         return "boxed Object";
        case BoxForm::Count:         break;
    }

    return "?";
}

/// Cartesian product of four class parameter styles and five class argument forms.
class BoxBindingMatrix : public ::testing::Test
{
protected:
    fn_tests::Box mutable_box{7};
    const fn_tests::Box const_box{7};
    fn_tests::Box moved_box{7};
    Object boxed_box = Object::make<fn_tests::Box>(7).unwrap();

    [[nodiscard]] auto call(const Function& fn, BoxForm form) -> Outcome<Object>
    {
        switch (form)
        {
            case BoxForm::MutableLvalue: return fn.invoke({mutable_box});
            case BoxForm::ConstLvalue:   return fn.invoke({const_box});
            case BoxForm::Temporary:     return fn.invoke({fn_tests::Box{7}});
            case BoxForm::MovedLvalue:   return fn.invoke({std::move(moved_box)});
            case BoxForm::Boxed:         return fn.invoke({boxed_box});
            case BoxForm::Count:         break;
        }

        std::abort();
    }
};

TEST_F(BoxBindingMatrix, ParameterStyleByArgumentForm)
{
    struct Row
    {
        std::string_view label;
        const Function* fn = nullptr;
        std::array<bool, kBoxFormCount> accepts{};
    };

    const std::array<Row, 4> rows{{
        //                     mutable const  temp  moved  boxed
        {"Box",                &function_of<&fn_tests::take_box>(),             {true, true, true, true, true}},
        {"const Box&",         &function_of<&fn_tests::take_box_ref>(),         {true, true, true, true, true}},
        {"Box&&",              &function_of<&fn_tests::take_box_rvalue>(),      {true, false, true, true, true}},
        {"const Box&&",        &function_of<&fn_tests::take_box_const_rvalue>(),{true, true, true, true, true}},
    }};

    for (const Row& row : rows)
    {
        for (std::size_t index = 0; index < kBoxFormCount; ++index)
        {
            const BoxForm form = static_cast<BoxForm>(index);
            Outcome<Object> outcome = call(*row.fn, form);
            SCOPED_TRACE(std::format("{} <- {}", row.label, box_form_name(form)));

            EXPECT_EQ(outcome.is_ok(), row.accepts[index]);
            if (row.accepts[index])
            {
                EXPECT_EQ(take_value<int>(std::move(outcome)), 7);
            }
            else if (outcome.is_err())
            {
                EXPECT_EQ(outcome.unwrap_err().kind, ErrorKind::TypeMismatch);
            }
        }
    }
}

// ── complex parameter types ──────────────────────────────────────────────────

TEST(FunctionTest, ComplexParameterTypes)
{
    // Reference to array.
    int values[4] = {1, 2, 3, 4};
    const Function& sum_array = function_of<&fn_tests::sum_array>();
    expect_parameters<&fn_tests::sum_array, int (&)[4]>();
    EXPECT_EQ(take_value<int>(sum_array.invoke({values})), 10);
    int* value_pointer = values;
    expect_invoke_error(sum_array.invoke({value_pointer}), ErrorKind::TypeMismatch);

    // Function pointer carrying noexcept, by value.
    const Function& apply_noexcept = function_of<&fn_tests::apply_noexcept_callback>();
    expect_parameters<&fn_tests::apply_noexcept_callback, fn_tests::NoexceptCallback, int>();
    EXPECT_EQ(take_value<int>(apply_noexcept.invoke({&fn_tests::noise_callback, 3})), 53);
    expect_invoke_error(apply_noexcept.invoke({&fn_tests::callback_a, 3}), ErrorKind::TypeMismatch);
    fn_tests::IntCallback plain_callback = &fn_tests::callback_a;
    expect_invoke_error(apply_noexcept.invoke({plain_callback, 3}), ErrorKind::TypeMismatch);

    // Pointer to member function, const and non-const.
    fn_tests::Calculator calculator;
    const Function& apply_member = function_of<&fn_tests::apply_member>();
    expect_parameters<&fn_tests::apply_member, const fn_tests::Calculator&, fn_tests::ConstMember, int>();
    const fn_tests::ConstMember member = &fn_tests::Calculator::add;
    EXPECT_EQ(take_value<int>(apply_member.invoke({calculator, member, 5})), 15);
    const fn_tests::MutableMember mutable_member = &fn_tests::Calculator::offset;
    expect_invoke_error(apply_member.invoke({calculator, mutable_member, 5}), ErrorKind::TypeMismatch);

    // Enum by value.
    const Function& color_code = function_of<&fn_tests::color_code>();
    expect_parameters<&fn_tests::color_code, fn_tests::Color>();
    fn_tests::Color green = fn_tests::Color::green;
    EXPECT_EQ(take_value<int>(color_code.invoke({green})), 2);
    int raw_color = 2;
    expect_invoke_error(color_code.invoke({raw_color}), ErrorKind::TypeMismatch);

    // Class rvalue reference and const rvalue reference.
    const Function& consume_string = function_of<&fn_tests::consume_string>();
    expect_parameters<&fn_tests::consume_string, std::string&&>();
    std::string text = "abcd";
    EXPECT_EQ(take_value<int>(consume_string.invoke({text})), 4);
    EXPECT_EQ(text, "abcd");
    EXPECT_EQ(take_value<int>(consume_string.invoke({std::string("xy")})), 2);
    const std::string frozen_text = "zzz";
    expect_invoke_error(consume_string.invoke({frozen_text}), ErrorKind::TypeMismatch);

    const Function& consume_box = function_of<&fn_tests::consume_box>();
    expect_parameters<&fn_tests::consume_box, fn_tests::Box&&>();
    fn_tests::Box box{7};
    EXPECT_EQ(take_value<int>(consume_box.invoke({box})), 7);
    EXPECT_EQ(take_value<int>(consume_box.invoke({fn_tests::Box{8}})), 8);
    const fn_tests::Box frozen_box{9};
    expect_invoke_error(consume_box.invoke({frozen_box}), ErrorKind::TypeMismatch);

    const Function& consume_const_box = function_of<&fn_tests::consume_const_box>();
    expect_parameters<&fn_tests::consume_const_box, const fn_tests::Box&&>();
    EXPECT_EQ(take_value<int>(consume_const_box.invoke({frozen_box})), 9);
    EXPECT_EQ(take_value<int>(consume_const_box.invoke({box})), 7);

    // Standard view type by value.
    const Function& span_sum = function_of<&fn_tests::span_sum>();
    expect_parameters<&fn_tests::span_sum, std::span<int>>();
    int data[3] = {1, 2, 3};
    std::span<int> span(data);
    EXPECT_EQ(take_value<int>(span_sum.invoke({span})), 6);
    expect_invoke_error(span_sum.invoke({data}), ErrorKind::TypeMismatch);
}

// ── extended return kinds ────────────────────────────────────────────────────

TEST(FunctionTest, ExtendedReturnKinds)
{
    expect_return<&fn_tests::cv_slot, const volatile int&>();
    expect_return<&fn_tests::movable_name, std::string&&>();
    expect_return<&fn_tests::choose_noexcept, fn_tests::NoexceptCallback>();
    expect_return<&fn_tests::array_slot, int (&)[3]>();

    // const volatile referent: the view carries both qualifiers.
    const Function& cv_slot = function_of<&fn_tests::cv_slot>();
    ObjectRef cv_view = std::move(cv_slot.invoke_ref({})).unwrap();
    EXPECT_TRUE(cv_view.is_const());
    EXPECT_TRUE(cv_view.is_volatile());
    EXPECT_TRUE(cv_view.as<int>().is_none());
    EXPECT_EQ(cv_view.as<const volatile int>().unwrap(), 33);
    EXPECT_EQ(take_value<int>(cv_slot.invoke({})), 33);

    // Rvalue-reference return: invoke moves the referent out, invoke_ref views it.
    const Function& movable_name = function_of<&fn_tests::movable_name>();
    EXPECT_TRUE(movable_name.returns_reference());
    ObjectRef name_view = std::move(movable_name.invoke_ref({})).unwrap();
    EXPECT_FALSE(name_view.is_const());
    name_view.as<std::string>().unwrap() = "moved!";
    EXPECT_EQ(take_value<std::string>(movable_name.invoke({})), "moved!");

    // Function pointer return whose type includes noexcept.
    fn_tests::NoexceptCallback callback = take_value<fn_tests::NoexceptCallback>(
        function_of<&fn_tests::choose_noexcept>().invoke({true}));
    EXPECT_EQ(callback(1), 51);
    EXPECT_EQ(take_value<fn_tests::NoexceptCallback>(
                  function_of<&fn_tests::choose_noexcept>().invoke({false}))(1),
              -49);

    // Reference to array return: not boxable, but viewable.
    const Function& array_slot = function_of<&fn_tests::array_slot>();
    EXPECT_TRUE(array_slot.returns_reference());
    EXPECT_FALSE(array_slot.is_invokable());
    EXPECT_TRUE(array_slot.is_ref_invokable());
    ObjectRef array_view = std::move(array_slot.invoke_ref({})).unwrap();
    EXPECT_EQ(array_view.type(), class_of<int[3]>());
    auto elements = array_view.as<int[3]>();
    ASSERT_TRUE(elements.is_some());
    int (&viewed)[3] = elements.unwrap();
    EXPECT_EQ(viewed[0], 1);
    EXPECT_EQ(viewed[1], 2);
    EXPECT_EQ(viewed[2], 3);
}

// ── reference returns combined with other parameter styles ───────────────────

TEST(FunctionTest, ReferenceReturnWithRvalueParameter)
{
    const Function& rvalue_slot = function_of<&fn_tests::rvalue_slot>();
    EXPECT_TRUE(rvalue_slot.returns_reference());
    EXPECT_TRUE(rvalue_slot.is_ref_invokable());
    EXPECT_EQ(take_value<int>(rvalue_slot.invoke({5})), 5);

    ObjectRef view = std::move(rvalue_slot.invoke_ref({7})).unwrap();
    EXPECT_EQ(view.as<int>().unwrap(), 7);
    view.as<int>().unwrap() = 8;
    EXPECT_EQ(fn_tests::rvalue_slot_storage(), 8);

    const Function& noexcept_slot = function_of<&fn_tests::noexcept_slot>();
    EXPECT_TRUE(noexcept_slot.is_noexcept());
    EXPECT_TRUE(noexcept_slot.returns_reference());
    EXPECT_TRUE(noexcept_slot.is_ref_invokable());
    EXPECT_TRUE(noexcept_slot.get_signature().ends_with(" noexcept"));

    ObjectRef noexcept_view = std::move(noexcept_slot.invoke_ref({})).unwrap();
    noexcept_view.as<int>().unwrap() = 22;
    EXPECT_EQ(fn_tests::noexcept_slot(), 22);
    EXPECT_EQ(take_value<int>(noexcept_slot.invoke({})), 22);
}

TEST(FunctionTest, ManyArgumentsWithLvalueAndRvalueReferences)
{
    const Function& mixed = function_of<&fn_tests::mixed_refs>();
    expect_parameters<&fn_tests::mixed_refs, int, int&, int&&, const int&, int*, const std::string&, bool,
                      char, std::string&&>();

    int a = 1;
    int b = 2;
    int c = 4;
    int d = 5;
    int e = 6;
    std::string f = "abc";
    std::string i = "xy";
    int* e_pointer = &e;
    EXPECT_EQ(take_value<int>(mixed.invoke({a, b, c, d, e_pointer, f, true, 'a', std::move(i)})), 25);
    EXPECT_EQ(b, 3);
}

TEST(FunctionTest, VariadicWithReferenceParameter)
{
    const Function& fn = function_of<overload_of<int&>(fn_tests::variadic_ref)>();
    EXPECT_TRUE(fn.is_variadic());
    EXPECT_FALSE(fn.is_invokable());
    EXPECT_FALSE(fn.is_ref_invokable());
    EXPECT_EQ(fn.get_parameter_count(), 1u);
    EXPECT_EQ(*fn.get_parameter_types()[0], class_of<int&>());
    EXPECT_EQ(fn, function_of<&fn_tests::variadic_ref>());

    int value = 1;
    expect_invoke_error(fn.invoke({value}), ErrorKind::NotSupported);
    expect_ref_error(fn.invoke_ref({value}), ErrorKind::NotSupported);
}

// ── Args construction boundaries and descriptor views ────────────────────────

TEST(FunctionTest, ArgsBoundariesAndDescriptorViews)
{
    int a = 1;
    int b = 2;
    int c = 3;
    int d = 4;
    int e = 5;
    int f = 6;
    int g = 7;
    int h = 8;

    // Exactly eight arguments through the variadic Args constructor (inline path).
    Args eight{a, b, c, d, e, f, g, h};
    EXPECT_EQ(take_value<int>(function_of<&fn_tests::sum8>().invoke(std::move(eight))), 36);

    // ObjectRef(void*, const Class&) and ObjectRef(const void*, const Class&).
    int value = 7;
    ObjectRef mutable_view(&value, class_of<int>());
    ObjectRef const_view(static_cast<const void*>(&value), class_of<int>());
    EXPECT_FALSE(mutable_view.is_const());
    EXPECT_TRUE(const_view.is_const());

    const Function& int_value = function_of<&fn_tests::int_value>();
    EXPECT_EQ(take_value<int>(int_value.invoke({mutable_view})), 7);
    EXPECT_EQ(take_value<int>(int_value.invoke({const_view})), 7);

    const Function& write_ref = function_of<&fn_tests::write_ref>();
    EXPECT_EQ(take_value<int>(write_ref.invoke({mutable_view})), 8);
    expect_invoke_error(write_ref.invoke({const_view}), ErrorKind::TypeMismatch);
    EXPECT_EQ(value, 8);

    // Object::make<T> boxing used as an argument.
    Object boxed = Object::make<int>(5).unwrap();
    EXPECT_EQ(take_value<int>(function_of<&fn_tests::unary>().invoke({boxed})), 10);
    EXPECT_EQ(boxed.as<int>().unwrap(), 5);

    Object text = Object::make<std::string>(3, 'x').unwrap();
    EXPECT_EQ(take_value<std::string>(function_of<&fn_tests::echo>().invoke({text})), "xxx");
    EXPECT_EQ(text.as<std::string>().unwrap(), "xxx");
}

// ── namespaces and linkage ───────────────────────────────────────────────────

TEST(FunctionTest, NamespaceAndLinkageMatrix)
{
    const Function& inline_fn = function_of<fn_tests::inline_ns_fn>();
    EXPECT_EQ(inline_fn.get_namespace(), "fn_tests::v1");
    EXPECT_EQ(inline_fn.get_qualified_name(), "fn_tests::v1::inline_ns_fn");
    EXPECT_TRUE(inline_fn.has_canonical_name());
    ASSERT_TRUE(Function::for_name(inline_fn.get_signature()).is_some());
    EXPECT_EQ(take_value<int>(inline_fn.invoke({1})), 4);

    const Function& deep = function_of<&fn_tests::deep::deep_hidden>();
    EXPECT_FALSE(deep.has_canonical_name());
    EXPECT_TRUE(deep.is_invokable());
    EXPECT_TRUE(Function::for_name(deep.get_signature()).is_none());
    EXPECT_EQ(take_value<int>(deep.invoke({1})), 6);

    const Function& global_hidden = function_of<&speculo_global_hidden>();
    EXPECT_FALSE(global_hidden.has_canonical_name());
    EXPECT_TRUE(global_hidden.is_invokable());
    EXPECT_TRUE(Function::for_name(global_hidden.get_signature()).is_none());
    EXPECT_EQ(take_value<int>(global_hidden.invoke({1})), 7);
}

// ── many arguments (Args inline / heap boundary) ─────────────────────────────

TEST(FunctionTest, ManyArgumentsAcrossTheInlineBufferBoundary)
{
    int a = 1;
    int b = 2;
    int c = 3;
    int d = 4;
    int e = 5;
    int f = 6;
    int g = 7;
    int h = 8;

    const Function& sum8 = function_of<&fn_tests::sum8>();
    EXPECT_EQ(take_value<int>(sum8.invoke({a, b, c, d, e, f, g, h})), 36);
    EXPECT_EQ(take_value<int>(sum8.invoke({1, 2, 3, 4, 5, 6, 7, 8})), 36);

    int i = 9;
    const Function& sum9 = function_of<&fn_tests::sum9>();
    EXPECT_EQ(take_value<int>(sum9.invoke({a, b, c, d, e, f, g, h, i})), 45);

    // The explicit Args object goes through the variadic constructor (not initializer_list).
    Args args{a, b, c, d, e, f, g, h, i};
    EXPECT_EQ(take_value<int>(sum9.invoke(std::move(args))), 45);

    const Function& sum12 = function_of<&fn_tests::sum12>();
    int j = 10;
    int k = 11;
    int l = 12;
    EXPECT_EQ(take_value<int>(sum12.invoke({a, b, c, d, e, f, g, h, i, j, k, l})), 78);

    // Mixed value / reference / pointer / bool / char / string/view loads.
    std::string text = "abc";
    int number = 4;
    std::string_view view_text = "xyz";
    const Function& mixed9 = function_of<&fn_tests::mixed9>();
    EXPECT_EQ(take_value<long long>(
                  mixed9.invoke({1, 2.0, text, &number, true, 'a', 5LL, 6.0f, view_text})),
              25);

    // Wrong count and a wrong argument beyond the inline buffer keep their diagnostics.
    expect_invoke_error(sum9.invoke({a, b, c, d, e, f, g, h}), ErrorKind::InvalidArgument);
    expect_invoke_error(sum9.invoke({a, b, c, d, e, f, g, h, i, j}), ErrorKind::InvalidArgument);
    speculo::Error late = sum9.invoke({a, b, c, d, e, f, g, h, 9.0}).unwrap_err();
    EXPECT_EQ(late.kind, ErrorKind::TypeMismatch);
    EXPECT_NE(late.message.find("argument 8"), std::string::npos);
}

// ── return kinds ─────────────────────────────────────────────────────────────

TEST(FunctionTest, ReturnKindMatrix)
{
    EXPECT_EQ(take_value<int>(function_of<&fn_tests::nullary>().invoke({})), 42);

    auto nothing = function_of<&fn_tests::nothing>().invoke();
    ASSERT_TRUE(nothing.is_ok());
    EXPECT_TRUE(std::move(nothing).unwrap().is_null());

    EXPECT_FALSE(take_value<bool>(function_of<&fn_tests::flip>().invoke({true})));
    EXPECT_EQ(take_value<char>(function_of<&fn_tests::bump>().invoke({'a'})), 'b');
    EXPECT_EQ(take_value<unsigned>(function_of<&fn_tests::widen>().invoke({3u})), 4u);
    EXPECT_EQ(take_value<long long>(function_of<&fn_tests::big>().invoke({4LL})), 4LL);
    EXPECT_FLOAT_EQ(take_value<float>(function_of<&fn_tests::half>().invoke({3.0f})), 1.5f);
    EXPECT_DOUBLE_EQ(take_value<double>(function_of<&fn_tests::twice_double>().invoke({1.5})), 3.0);
    EXPECT_EQ(take_value<std::string>(function_of<&fn_tests::echo>().invoke({std::string("hi")})), "hi");
    EXPECT_EQ(take_value<int>(function_of<&fn_tests::frozen_value>().invoke({})), 12); // const int -> int

    const fn_tests::Box box = take_value<fn_tests::Box>(function_of<&fn_tests::make_box>().invoke({6}));
    EXPECT_EQ(box.value, 6);
    EXPECT_EQ(take_value<fn_tests::Color>(function_of<&fn_tests::favorite>().invoke({})), fn_tests::Color::green);

    int* pointer = take_value<int*>(function_of<&fn_tests::pointer_slot>().invoke({}));
    ASSERT_NE(pointer, nullptr);
    EXPECT_EQ(*pointer, 5);
    EXPECT_EQ(take_value<int*>(function_of<&fn_tests::null_pointer>().invoke({})), nullptr);

    const char* text = take_value<const char*>(function_of<&fn_tests::greeting>().invoke({}));
    EXPECT_STREQ(text, "hello");

    std::string owner = "viewed";
    EXPECT_EQ(take_value<std::string_view>(function_of<&fn_tests::view_of>().invoke({owner})), "viewed");

    std::unique_ptr<int> unique = take_value<std::unique_ptr<int>>(function_of<&fn_tests::make_unique>().invoke({5}));
    ASSERT_NE(unique, nullptr);
    EXPECT_EQ(*unique, 5);
}

TEST(FunctionTest, ReferenceReturnMatrix)
{
    const Function& name_slot = function_of<&fn_tests::name_slot>();
    EXPECT_TRUE(name_slot.returns_reference());
    EXPECT_TRUE(name_slot.is_ref_invokable());
    EXPECT_EQ(take_value<std::string>(name_slot.invoke({})), "alpha");
    ObjectRef name_view = std::move(name_slot.invoke_ref({})).unwrap();
    EXPECT_FALSE(name_view.is_const());
    name_view.as<std::string>().unwrap() = "gamma";
    EXPECT_EQ(fn_tests::name_slot(), "gamma");

    const Function& frozen_name = function_of<&fn_tests::frozen_name>();
    ObjectRef frozen_view = std::move(frozen_name.invoke_ref({})).unwrap();
    EXPECT_TRUE(frozen_view.is_const());
    EXPECT_EQ(frozen_view.as<const std::string>().unwrap(), "beta");
    EXPECT_EQ(take_value<std::string>(frozen_name.invoke({})), "beta");

    const Function& movable = function_of<&fn_tests::movable_slot>();
    EXPECT_TRUE(movable.returns_reference());
    EXPECT_TRUE(movable.is_ref_invokable());
    EXPECT_EQ(take_value<int>(movable.invoke({})), 9);
    ObjectRef movable_view = std::move(movable.invoke_ref({})).unwrap();
    EXPECT_EQ(movable_view.as<int>().unwrap(), 9);

    const Function& volatile_fn = function_of<&fn_tests::volatile_slot>();
    EXPECT_TRUE(volatile_fn.returns_reference());
    ObjectRef volatile_view = std::move(volatile_fn.invoke_ref({})).unwrap();
    EXPECT_TRUE(volatile_view.is_volatile());
    EXPECT_EQ(volatile_view.as<volatile int>().unwrap(), 3);
    EXPECT_TRUE(volatile_view.as<int>().is_none());
    EXPECT_EQ(take_value<int>(volatile_fn.invoke({})), 3);

    int value = 5;
    const Function& echo_ref = function_of<&fn_tests::echo_ref>();
    EXPECT_EQ(take_value<int>(echo_ref.invoke({value})), 5);
    ObjectRef ref_view = std::move(echo_ref.invoke_ref({value})).unwrap();
    ref_view.as<int>().unwrap() = 8;
    EXPECT_EQ(value, 8);
}

TEST(FunctionTest, ReferenceReturnFlagsAndErrorOrdering)
{
    EXPECT_FALSE(function_of<&fn_tests::add>().returns_reference());
    EXPECT_FALSE(function_of<&fn_tests::add>().is_ref_invokable());
    EXPECT_FALSE(function_of<&fn_tests::pointer_slot>().returns_reference());
    EXPECT_TRUE(function_of<&fn_tests::slot>().returns_reference());
    EXPECT_TRUE(function_of<&fn_tests::movable_slot>().returns_reference());

    const Function& echo_ref = function_of<&fn_tests::echo_ref>();
    int value = 1;
    // check_call runs first: argument errors win over "does not return a reference".
    expect_ref_error(echo_ref.invoke_ref({}), ErrorKind::InvalidArgument);
    expect_ref_error(echo_ref.invoke_ref({value, value}), ErrorKind::InvalidArgument);
    const int frozen = 1;
    expect_ref_error(echo_ref.invoke_ref({frozen}), ErrorKind::TypeMismatch);

    // A non-reference result is rejected before any invocation.
    expect_ref_error(function_of<&fn_tests::add>().invoke_ref({value, value}), ErrorKind::NotSupported);
}

/// Cartesian product of the return category and the two call entry points.
TEST(FunctionTest, InvokeVersusInvokeRefMatrix)
{
    struct Row
    {
        std::string_view label;
        const Function* fn = nullptr;
        bool invoke_ok = false;
        bool ref_ok = false;
    };

    const std::array<Row, 9> rows{{
        {"int()",                   &function_of<&fn_tests::nullary>(),     true, false},
        {"void()",                  &function_of<&fn_tests::nothing>(),     true, false},
        {"int&()",                  &function_of<&fn_tests::slot>(),        true, true},
        {"const int&()",            &function_of<&fn_tests::frozen>(),      true, true},
        {"int&&()",                 &function_of<&fn_tests::movable_slot>(), true, true},
        {"volatile int&()",         &function_of<&fn_tests::volatile_slot>(), true, true},
        {"std::string&()",          &function_of<&fn_tests::name_slot>(),   true, true},
        {"const std::string&()",    &function_of<&fn_tests::frozen_name>(), true, true},
        {"MoveOnly&()",             &function_of<&fn_tests::move_slot>(),   false, true},
    }};

    for (const Row& row : rows)
    {
        SCOPED_TRACE(row.label);
        EXPECT_EQ(row.fn->returns_reference(), row.ref_ok);
        EXPECT_EQ(row.fn->is_ref_invokable(), row.ref_ok);

        const Outcome<Object> invoked = row.fn->invoke({});
        EXPECT_EQ(invoked.is_ok(), row.invoke_ok);
        if (!row.invoke_ok && invoked.is_err())
        {
            EXPECT_EQ(invoked.unwrap_err().kind, ErrorKind::NotSupported);
        }

        const Outcome<ObjectRef> referenced = row.fn->invoke_ref({});
        EXPECT_EQ(referenced.is_ok(), row.ref_ok);
        if (!row.ref_ok && referenced.is_err())
        {
            EXPECT_EQ(referenced.unwrap_err().kind, ErrorKind::NotSupported);
        }
    }
}

// ── nulls, volatile, and diagnostics ─────────────────────────────────────────

TEST(FunctionTest, NullArgumentsAreRejected)
{
    const Function& unary = function_of<&fn_tests::unary>();
    ObjectRef null_ref;
    speculo::Error error = unary.invoke({null_ref}).unwrap_err();
    EXPECT_EQ(error.kind, ErrorKind::TypeMismatch);
    EXPECT_NE(error.message.find("argument 0 is null"), std::string::npos);

    // An empty Object views as a null ObjectRef.
    Object empty;
    EXPECT_EQ(unary.invoke({empty}).unwrap_err().kind, ErrorKind::TypeMismatch);

    // One null in a multi-argument call reports the right position.
    int value = 1;
    speculo::Error second = function_of<&fn_tests::add>().invoke({value, ObjectRef{}}).unwrap_err();
    EXPECT_NE(second.message.find("argument 1 is null"), std::string::npos);
}

TEST(FunctionTest, TypeMismatchDiagnosticsNamePositionAndTypes)
{
    const Function& add = function_of<&fn_tests::add>();
    int left = 1;
    double wrong = 2.0;
    speculo::Error error = add.invoke({left, wrong}).unwrap_err();
    EXPECT_EQ(error.kind, ErrorKind::TypeMismatch);
    EXPECT_NE(error.message.find("argument 1"), std::string::npos);
    EXPECT_NE(error.message.find("'double'"), std::string::npos);
    EXPECT_NE(error.message.find("'int'"), std::string::npos);

    const int frozen = 1;
    speculo::Error ref_error = function_of<&fn_tests::write_ref>().invoke({frozen}).unwrap_err();
    EXPECT_NE(ref_error.message.find("const object"), std::string::npos);
    EXPECT_NE(ref_error.message.find("non-const reference"), std::string::npos);
}

// ── introspection matrix ─────────────────────────────────────────────────────

TEST(FunctionTest, ParameterAndReturnTypeMatrix)
{
    expect_parameters<&fn_tests::nullary>();
    expect_parameters<&fn_tests::add, int, int>();
    expect_parameters<&fn_tests::read_const, const int&>();
    expect_parameters<&fn_tests::read_volatile, const volatile int&>();
    expect_parameters<&fn_tests::write_ref, int&>();
    expect_parameters<&fn_tests::sink, int&&>();
    expect_parameters<&fn_tests::echo, std::string>();
    expect_parameters<&fn_tests::concat, const std::string&, const std::string&>();
    expect_parameters<&fn_tests::text_size, const char*>();
    expect_parameters<&fn_tests::first_char, char*>();
    expect_parameters<&fn_tests::view_size, std::string_view>();
    expect_parameters<&fn_tests::read_pointer, const int*>();
    expect_parameters<&fn_tests::write_pointer, int*>();
    expect_parameters<&fn_tests::pointer_ref, int*&>();
    expect_parameters<&fn_tests::pointer_to_pointer, int**>();
    expect_parameters<&fn_tests::first_of, int*>();
    expect_parameters<&fn_tests::apply_callback, fn_tests::IntCallback, int>();
    expect_parameters<&fn_tests::read_base, fn_tests::Base>();
    expect_parameters<&fn_tests::int_value, int>();
    expect_parameters<&fn_tests::unsigned_value, unsigned>();
    expect_parameters<&fn_tests::long_value, long>();
    expect_parameters<&fn_tests::long_long_value, long long>();
    expect_parameters<&fn_tests::double_value, double>();
    expect_parameters<&fn_tests::float_value, float>();
    expect_parameters<&fn_tests::bool_value, bool>();
    expect_parameters<&fn_tests::char_value, char>();
    expect_parameters<&fn_tests::size_value, std::size_t>();
    expect_parameters<&fn_tests::short_value, short>();
    expect_parameters<&fn_tests::take, fn_tests::MoveOnly>();

    expect_return<&fn_tests::nullary, int>();
    expect_return<&fn_tests::nothing, void>();
    expect_return<&fn_tests::slot, int&>();
    expect_return<&fn_tests::frozen, const int&>();
    expect_return<&fn_tests::movable_slot, int&&>();
    expect_return<&fn_tests::volatile_slot, volatile int&>();
    expect_return<&fn_tests::name_slot, std::string&>();
    expect_return<&fn_tests::frozen_name, const std::string&>();
    expect_return<&fn_tests::move_slot, fn_tests::MoveOnly&>();
    expect_return<&fn_tests::pointer_slot, int*>();
    expect_return<&fn_tests::null_pointer, int*>();
    expect_return<&fn_tests::greeting, const char*>();
    expect_return<&fn_tests::view_of, std::string_view>();
    expect_return<&fn_tests::make_box, fn_tests::Box>();
    expect_return<&fn_tests::favorite, fn_tests::Color>();
    expect_return<&fn_tests::make_unique, std::unique_ptr<int>>();
    expect_return<&fn_tests::frozen_value, const int>();
    expect_return<&fn_tests::apply_callback, int>();
    expect_return<&fn_tests::choose_callback, fn_tests::IntCallback>();
}

TEST(FunctionTest, SignatureStrings)
{
    EXPECT_EQ(function_of<&fn_tests::nullary>().get_signature(), "int fn_tests::nullary()");
    EXPECT_EQ(function_of<&fn_tests::add>().get_signature(), "int fn_tests::add(int, int)");
    EXPECT_EQ(function_of<&fn_tests::read_const>().get_signature(), "int fn_tests::read_const(const int&)");
    EXPECT_EQ(function_of<&fn_tests::sink>().get_signature(), "int fn_tests::sink(int&&)");
    EXPECT_EQ(function_of<&fn_tests::pointer_slot>().get_signature(), "int* fn_tests::pointer_slot()");
    EXPECT_EQ(function_of<&fn_tests::plain>().get_signature(), "int fn_tests::plain(int) noexcept");
    EXPECT_EQ(function_of<&fn_tests::variadic>().get_signature(), "int fn_tests::variadic(int, ...)");
    EXPECT_EQ(function_of<&fn_tests::inner::add>().get_signature(), "int fn_tests::inner::add(int, int)");
    EXPECT_EQ(function_of<speculo_fn_global>().get_signature(), "int speculo_fn_global(int)");

    // get_name / get_namespace / get_qualified_name agree with the signature.
    const Function& plain = function_of<&fn_tests::plain>();
    EXPECT_EQ(plain.get_name(), "plain");
    EXPECT_EQ(plain.get_namespace(), "fn_tests");
    EXPECT_EQ(plain.get_qualified_name(), "fn_tests::plain");
    EXPECT_FALSE(plain.is_deleted());

    const Function& nullary = function_of<&fn_tests::nullary>();
    EXPECT_EQ(nullary.get_parameter_count(), 0u);
    EXPECT_TRUE(nullary.get_parameter_types().empty());
}

// ── entry point forms ────────────────────────────────────────────────────────

TEST(FunctionTest, AllTokenFormsResolveToOneHandle)
{
    const Function& by_name = function_of<fn_tests::add>();
    const Function& by_address = function_of<&fn_tests::add>();
    const Function& by_reflection = function_of<^^fn_tests::add>();

    constexpr auto pointer = &fn_tests::add;
    const Function& by_pointer = function_of<pointer>();
    const Function& by_static_cast = function_of<static_cast<int (*)(int, int)>(fn_tests::add)>();

    EXPECT_EQ(std::addressof(by_name), std::addressof(by_address));
    EXPECT_EQ(std::addressof(by_name), std::addressof(by_reflection));
    EXPECT_EQ(std::addressof(by_name), std::addressof(by_pointer));
    EXPECT_EQ(std::addressof(by_name), std::addressof(by_static_cast));
    EXPECT_EQ(by_name, by_static_cast);
}

TEST(FunctionTest, OverloadSetMatrix)
{
    const Function& pick_int = function_of<overload_of<int>(fn_tests::over::pick)>();
    const Function& pick_ref = function_of<overload_of<int&>(fn_tests::over::pick)>();
    const Function& pick_const_ref = function_of<overload_of<const int&>(fn_tests::over::pick)>();
    const Function& pick_rvalue = function_of<overload_of<int&&>(fn_tests::over::pick)>();
    const Function& pick_pointer = function_of<overload_of<int*>(fn_tests::over::pick)>();
    const Function& pick_double = function_of<overload_of<double>(fn_tests::over::pick)>();
    const Function& pick_text = function_of<overload_of<const char*>(fn_tests::over::pick)>();
    const Function& pick_string = function_of<overload_of<const std::string&>(fn_tests::over::pick)>();
    const Function& multiply = function_of<overload_of<int, int>(fn_tests::over::multiply)>();

    const std::array<const Function*, 8> overloads{
        &pick_int, &pick_ref, &pick_const_ref, &pick_rvalue,
        &pick_pointer, &pick_double, &pick_text, &pick_string,
    };
    for (std::size_t i = 0; i < overloads.size(); ++i)
    {
        EXPECT_EQ(overloads[i]->get_name(), "pick") << i;
        EXPECT_EQ(overloads[i]->get_namespace(), "fn_tests::over") << i;
        EXPECT_TRUE(overloads[i]->has_canonical_name()) << i;
        for (std::size_t j = i + 1; j < overloads.size(); ++j)
        {
            EXPECT_NE(*overloads[i], *overloads[j]) << i << " vs " << j;
        }
    }

    expect_parameters<overload_of<int>(fn_tests::over::pick), int>();
    expect_parameters<overload_of<int&>(fn_tests::over::pick), int&>();
    expect_parameters<overload_of<const int&>(fn_tests::over::pick), const int&>();
    expect_parameters<overload_of<int&&>(fn_tests::over::pick), int&&>();
    expect_parameters<overload_of<int*>(fn_tests::over::pick), int*>();
    expect_parameters<overload_of<double>(fn_tests::over::pick), double>();
    expect_parameters<overload_of<const char*>(fn_tests::over::pick), const char*>();
    expect_parameters<overload_of<const std::string&>(fn_tests::over::pick), const std::string&>();
    expect_parameters<overload_of<int, int>(fn_tests::over::multiply), int, int>();

    // Every overload is callable through its own handle.
    int value = 3;
    EXPECT_EQ(take_value<int>(pick_int.invoke({value})), 30);
    EXPECT_EQ(take_value<int>(pick_ref.invoke({value})), 40);
    EXPECT_EQ(value, 4);
    EXPECT_EQ(take_value<int>(pick_rvalue.invoke({3})), 31);
    EXPECT_EQ(take_value<int>(pick_pointer.invoke({&value})), 40);
    EXPECT_EQ(take_value<int>(pick_double.invoke({1.5})), 15);
    EXPECT_EQ(take_value<int>(pick_text.invoke({"abcd"})), 4);
    EXPECT_EQ(take_value<int>(pick_string.invoke({std::string("abc")})), 30);
    EXPECT_EQ(take_value<int>(multiply.invoke({3, 4})), 12);

    const int const_value = 8;
    EXPECT_TRUE(pick_const_ref.returns_reference());
    EXPECT_EQ(take_value<int>(pick_const_ref.invoke({const_value})), 8);
    ObjectRef referenced = std::move(pick_const_ref.invoke_ref({const_value})).unwrap();
    EXPECT_TRUE(referenced.is_const());
    EXPECT_EQ(referenced.as<const int>().unwrap(), 8);

    // Cross-overload mismatches are rejected exactly.
    expect_invoke_error(pick_int.invoke({1.5}), ErrorKind::TypeMismatch);
    expect_invoke_error(pick_double.invoke({1}), ErrorKind::TypeMismatch);
    expect_invoke_error(pick_pointer.invoke({value}), ErrorKind::TypeMismatch);
    expect_invoke_error(pick_int.invoke({&value}), ErrorKind::TypeMismatch);
    expect_invoke_error(pick_text.invoke({std::string("abc")}), ErrorKind::TypeMismatch);
    expect_invoke_error(pick_string.invoke({"abc"}), ErrorKind::TypeMismatch);
    expect_invoke_error(pick_ref.invoke({const_value}), ErrorKind::TypeMismatch);
    expect_invoke_error(pick_rvalue.invoke({const_value}), ErrorKind::TypeMismatch);
    expect_invoke_error(multiply.invoke({1}), ErrorKind::InvalidArgument);
    expect_invoke_error(pick_int.invoke({1, 2}), ErrorKind::InvalidArgument);

    // A function pointer cast and overload_of pick the same declaration.
    const Function& pick_int_by_cast = function_of<static_cast<int (*)(int)>(fn_tests::over::pick)>();
    EXPECT_EQ(pick_int, pick_int_by_cast);

    const Function& scale_two = function_of<overload_of<int, int>(fn_tests::scale)>();
    EXPECT_EQ(*scale_two.get_parameter_types()[0], class_of<int>());
    EXPECT_EQ(*scale_two.get_parameter_types()[1], class_of<int>());
}

TEST(FunctionTest, ArityOverloadSet)
{
    // GCC 16.2 cannot deduce the return type when overload_of names a lower
    // arity than some other overload in the set (an "unresolved overload set"
    // error), so a function pointer cast is the token here. The highest arity
    // also works through overload_of.
    const Function& none = function_of<static_cast<int (*)()>(fn_tests::over::arity)>();
    const Function& one = function_of<static_cast<int (*)(int)>(fn_tests::over::arity)>();
    const Function& two = function_of<static_cast<int (*)(int, int)>(fn_tests::over::arity)>();
    const Function& three = function_of<static_cast<int (*)(int, int, int)>(fn_tests::over::arity)>();
    EXPECT_EQ(three, (function_of<overload_of<int, int, int>(fn_tests::over::arity)>()));

    EXPECT_EQ(none.get_parameter_count(), 0u);
    EXPECT_EQ(one.get_parameter_count(), 1u);
    EXPECT_EQ(two.get_parameter_count(), 2u);
    EXPECT_EQ(three.get_parameter_count(), 3u);
    EXPECT_EQ(take_value<int>(none.invoke({})), 0);
    EXPECT_EQ(take_value<int>(one.invoke({4})), 4);
    EXPECT_EQ(take_value<int>(two.invoke({4, 5})), 9);
    EXPECT_EQ(take_value<int>(three.invoke({4, 5, 6})), 15);

    expect_invoke_error(one.invoke({}), ErrorKind::InvalidArgument);
    expect_invoke_error(two.invoke({1}), ErrorKind::InvalidArgument);
    expect_invoke_error(three.invoke({1, 2}), ErrorKind::InvalidArgument);
}

TEST(FunctionTest, FunctionTemplateInstantiationSelection)
{
    const Function& int_twice = function_of<fn_tests::twice<int>>();
    const Function& double_twice = function_of<&fn_tests::twice<double>>();

    EXPECT_NE(int_twice, double_twice);
    EXPECT_EQ(int_twice.get_return_type(), class_of<int>());
    EXPECT_EQ(double_twice.get_return_type(), class_of<double>());
    EXPECT_EQ(take_value<int>(int_twice.invoke({4})), 8);
    EXPECT_DOUBLE_EQ(take_value<double>(double_twice.invoke({1.25})), 2.5);
    expect_invoke_error(int_twice.invoke({1.25}), ErrorKind::TypeMismatch);
}

TEST(FunctionTest, NoexceptSelectionAndLookup)
{
    const Function& noise = function_of<overload_of<int>(fn_tests::over::noise)>();
    EXPECT_TRUE(noise.is_noexcept());
    EXPECT_TRUE(noise.get_signature().ends_with(" noexcept"));
    EXPECT_EQ(take_value<int>(noise.invoke({3})), 3);

    ASSERT_TRUE(Function::for_name(noise.get_signature()).is_some());
    EXPECT_EQ(Function::for_name(noise.get_signature()).unwrap(), noise);

    // for_name is exact: dropping the noexcept suffix is a different key.
    std::string without_suffix(noise.get_signature().substr(0, noise.get_signature().size() - 9));
    EXPECT_TRUE(Function::for_name(without_suffix).is_none());
}

// ── registry ─────────────────────────────────────────────────────────────────

TEST(FunctionTest, RegistryLookupMatrix)
{
    const Function& add = function_of<&fn_tests::add>();
    const Function& pick_int = function_of<overload_of<int>(fn_tests::over::pick)>();
    const Function& multiply = function_of<overload_of<int, int>(fn_tests::over::multiply)>();

    for (const Function* fn : {&add, &pick_int, &multiply})
    {
        ASSERT_TRUE(Function::for_name(fn->get_signature()).is_some()) << fn->get_signature();
        EXPECT_EQ(Function::for_name(fn->get_signature()).unwrap(), *fn);
    }

    // Whitespace, casing, and arity are all part of the key.
    EXPECT_TRUE(Function::for_name("int  fn_tests::add(int, int)").is_none());
    EXPECT_TRUE(Function::for_name("int fn_tests::add(int,int)").is_none());
    EXPECT_TRUE(Function::for_name("int fn_tests::add(int)").is_none());
    EXPECT_TRUE(Function::for_name("").is_none());

    // Idempotent registration: materializing again yields the same handle and one entry.
    const Function& again = function_of<&fn_tests::add>();
    EXPECT_EQ(std::addressof(add), std::addressof(again));

    const std::vector<std::string_view> signatures = Function::registered_signatures();
    EXPECT_TRUE(std::ranges::is_sorted(signatures));
    EXPECT_EQ(std::ranges::adjacent_find(signatures), signatures.end());
    EXPECT_NE(std::ranges::find(signatures, add.get_signature()), signatures.end());
    EXPECT_NE(std::ranges::find(signatures, pick_int.get_signature()), signatures.end());
    EXPECT_NE(std::ranges::find(signatures, multiply.get_signature()), signatures.end());
}

TEST(FunctionTest, InternalLinkageFunctionsAreInvokableButUnregistered)
{
    for (const Function* fn : {&function_of<&fn_tests::hidden>(), &function_of<&fn_tests::anonymous_fn>()})
    {
        EXPECT_FALSE(fn->has_canonical_name()) << fn->get_signature();
        EXPECT_TRUE(fn->is_invokable()) << fn->get_signature();
        EXPECT_TRUE(Function::for_name(fn->get_signature()).is_none()) << fn->get_signature();

        const std::vector<std::string_view> signatures = Function::registered_signatures();
        EXPECT_EQ(std::ranges::find(signatures, fn->get_signature()), signatures.end());
    }

    int value = 1;
    EXPECT_EQ(take_value<int>(function_of<&fn_tests::hidden>().invoke({value})), 2);
    EXPECT_EQ(take_value<int>(function_of<&fn_tests::anonymous_fn>().invoke({value})), 3);
}

// ── callbacks and handle semantics ───────────────────────────────────────────

TEST(FunctionTest, FunctionPointerParametersAndReturns)
{
    const Function& apply = function_of<&fn_tests::apply_callback>();
    fn_tests::IntCallback callback = &fn_tests::callback_a;
    EXPECT_EQ(take_value<int>(apply.invoke({callback, 5})), 105);

    int not_a_callback = 5;
    expect_invoke_error(apply.invoke({not_a_callback, 5}), ErrorKind::TypeMismatch);
    expect_invoke_error(apply.invoke({callback}), ErrorKind::InvalidArgument);

    const Function& choose = function_of<&fn_tests::choose_callback>();
    fn_tests::IntCallback chosen = take_value<fn_tests::IntCallback>(choose.invoke({false}));
    EXPECT_EQ(chosen(1), 201);
    bool flag = true;
    EXPECT_EQ(take_value<fn_tests::IntCallback>(choose.invoke({flag}))(1), 101);
}

TEST(FunctionTest, HandleCopyAndComparison)
{
    const Function& add = function_of<&fn_tests::add>();
    Function copy = add;
    EXPECT_EQ(copy, add);

    int left = 1;
    int right = 2;
    EXPECT_EQ(take_value<int>(copy.invoke({left, right})), 3);

    const Function& inner = function_of<&fn_tests::inner::add>();
    EXPECT_NE(add, inner);
    EXPECT_EQ(add.get_parameter_count(), inner.get_parameter_count());
    EXPECT_NE(add.get_qualified_name(), inner.get_qualified_name());
}

// ── gap coverage: overload_of variadic / registry / panic / annotations ──────

TEST(FunctionTest, VariadicOverloadOfUsesTheSecondTemplate)
{
    // `fn_tests::variadic` is unique, so overload_of must take its second form.
    const Function& unique = function_of<overload_of<int>(fn_tests::variadic)>();
    EXPECT_TRUE(unique.is_variadic());
    EXPECT_EQ(unique.get_parameter_count(), 1u);
    EXPECT_EQ(*unique.get_parameter_types()[0], class_of<int>());
    EXPECT_FALSE(unique.is_invokable());
    EXPECT_EQ(unique, function_of<&fn_tests::variadic>());
    EXPECT_EQ(unique, function_of<^^fn_tests::variadic>());

    // The same in an overload set: the variadic and the plain declaration are
    // selected through the two different overload_of templates.
    const Function& variadic = function_of<overload_of<int>(fn_tests::over::variadic_pick)>();
    const Function& plain = function_of<overload_of<double>(fn_tests::over::variadic_pick)>();
    EXPECT_NE(variadic, plain);
    EXPECT_TRUE(variadic.is_variadic());
    EXPECT_FALSE(variadic.is_invokable());
    EXPECT_FALSE(plain.is_variadic());
    EXPECT_TRUE(plain.is_invokable());
    EXPECT_DOUBLE_EQ(take_value<double>(plain.invoke({2.0})), 6.0);

    ASSERT_TRUE(Function::for_name(variadic.get_signature()).is_some());
    EXPECT_EQ(Function::for_name(variadic.get_signature()).unwrap(), variadic);
}

TEST(FunctionTest, DirectRegisterFunctionCallIsIdempotent)
{
    const Function& fn = function_of<&fn_tests::register_probe>();
    EXPECT_EQ(std::ranges::count(Function::registered_signatures(), fn.get_signature()), 1);

    speculo::register_function(fn);
    speculo::register_function(fn);

    const std::vector<std::string_view> signatures = Function::registered_signatures();
    EXPECT_EQ(std::ranges::count(signatures, fn.get_signature()), 1);
    ASSERT_TRUE(Function::for_name(fn.get_signature()).is_some());
    EXPECT_EQ(Function::for_name(fn.get_signature()).unwrap(), fn);
}

TEST(FunctionTest, PanicsOnEmptyHandleAndBadAnnotationIndex)
{
    EXPECT_DEATH(
        {
            const speculo::Function empty;
            (void)empty.get_name();
        },
        "empty");

    const Function& decorated = function_of<&fn_tests::decorated>();
    EXPECT_DEATH((void)decorated.get_parameter_annotations(2), "out of range");

    // A free function cannot be `= delete`d and named on GCC 16.2 (api_design
    // §6.1), so is_deleted()/Modifier::DELETED have no positive free-function
    // case. The negative case is covered here.
    EXPECT_FALSE(function_of<&fn_tests::add>().is_deleted());
    EXPECT_FALSE(function_of<&fn_tests::add>().modifiers().is_deleted());
}

TEST(FunctionTest, AnnotationPlacementMatrix)
{
    struct Row
    {
        std::string_view label;
        const Function* fn = nullptr;
        std::size_t function_annotations = 0;
        bool has_tag = false;
        bool has_other = false;
        std::size_t parameter_count = 0;
        std::size_t parameter_annotations = 0;
        int first_parameter_value = 0;
        bool readable = true;
    };

    const std::array<Row, 6> rows{{
        {"function only",      &function_of<&fn_tests::function_only>(),         1, true,  false, 0, 0, 0, true},
        {"parameter only",     &function_of<&fn_tests::parameter_only>(),        0, false, false, 1, 1, 1, true},
        {"both sides",         &function_of<&fn_tests::both_sides>(),            1, true,  false, 1, 1, 2, true},
        {"multiple function",  &function_of<&fn_tests::multi_function>(),        2, true,  true,  0, 0, 0, true},
        {"multiple parameter", &function_of<&fn_tests::multi_parameter>(),       0, false, false, 1, 2, 1, true},
        {"unreadable",         &function_of<&fn_tests::unreadable_annotation>(), 1, false, false, 1, 0, 0, false},
    }};

    for (const Row& row : rows)
    {
        SCOPED_TRACE(row.label);
        const std::span<const speculo::Annotation> function_annotations = row.fn->get_annotations();
        EXPECT_EQ(function_annotations.size(), row.function_annotations);
        EXPECT_EQ(row.fn->get_annotation<fn_tests::Tag>().is_some(), row.has_tag);
        EXPECT_EQ(row.fn->get_annotation<fn_tests::OtherTag>().is_some(), row.has_other);
        EXPECT_EQ(row.fn->get_parameter_count(), row.parameter_count);

        for (const speculo::Annotation& annotation : function_annotations)
        {
            EXPECT_EQ(annotation.is_readable(), row.readable);
            EXPECT_EQ(annotation.value().is_ok(), row.readable);
        }

        if (row.function_annotations > 0)
        {
            EXPECT_EQ(function_annotations[0], row.fn->get_annotations()[0]);
        }
        if (row.function_annotations > 1)
        {
            EXPECT_NE(function_annotations[0], function_annotations[1]);
        }

        if (row.parameter_count > 0)
        {
            const std::span<const speculo::Annotation> parameter_annotations =
                row.fn->get_parameter_annotations(0);
            EXPECT_EQ(parameter_annotations.size(), row.parameter_annotations);

            if (row.parameter_annotations > 0)
            {
                EXPECT_EQ(row.fn->get_parameter_annotation<int>(0).unwrap(), row.first_parameter_value);
                EXPECT_EQ(parameter_annotations[0].get<int>().unwrap(), row.first_parameter_value);
                EXPECT_TRUE(parameter_annotations[0].value().is_ok());
            }
        }
    }

    const speculo::Annotation& unreadable =
        function_of<&fn_tests::unreadable_annotation>().get_annotations()[0];
    EXPECT_EQ(unreadable.get_type(), class_of<fn_tests::PointerTag>());
    EXPECT_FALSE(unreadable.is_readable());
    EXPECT_TRUE(unreadable.get<fn_tests::PointerTag>().is_none());
    expect_invoke_error(unreadable.value(), ErrorKind::NotSupported);

    // Annotations keep their source order, and first_annotation picks the first.
    const auto multiple = function_of<&fn_tests::multi_parameter>().get_parameter_annotations(0);
    ASSERT_EQ(multiple.size(), 2u);
    EXPECT_EQ(multiple[0].get<int>().unwrap(), 1);
    EXPECT_EQ(multiple[1].get<int>().unwrap(), 2);
}

TEST(FunctionTest, NoexceptFunctionTokenForms)
{
    const Function& by_name = function_of<fn_tests::over::noise>();
    const Function& by_address = function_of<&fn_tests::over::noise>();
    const Function& by_reflection = function_of<^^fn_tests::over::noise>();

    constexpr auto pointer = &fn_tests::over::noise; // int (*)(int) noexcept
    const Function& by_pointer = function_of<pointer>();
    const Function& by_noexcept_cast =
        function_of<static_cast<int (*)(int) noexcept>(fn_tests::over::noise)>();

    for (const Function* fn : {&by_name, &by_address, &by_reflection, &by_pointer, &by_noexcept_cast})
    {
        EXPECT_EQ(std::addressof(*fn), std::addressof(by_name));
        EXPECT_TRUE(fn->is_noexcept());
    }
}

TEST(FunctionTest, ComplexSignatureStrings)
{
    const std::array<const Function*, 8> functions{{
        &function_of<&fn_tests::echo>(),
        &function_of<&fn_tests::name_slot>(),
        &function_of<&fn_tests::frozen_name>(),
        &function_of<&fn_tests::make_unique>(),
        &function_of<&fn_tests::apply_callback>(),
        &function_of<&fn_tests::make_box>(),
        &function_of<&fn_tests::read_base>(),
        &function_of<&fn_tests::mixed9>(),
    }};

    for (const Function* fn : functions)
    {
        SCOPED_TRACE(fn->get_signature());
        ASSERT_TRUE(Function::for_name(fn->get_signature()).is_some());
        EXPECT_EQ(Function::for_name(fn->get_signature()).unwrap(), *fn);
    }

    EXPECT_EQ(function_of<&fn_tests::make_box>().get_signature(), "fn_tests::Box fn_tests::make_box(int)");
    EXPECT_EQ(function_of<&fn_tests::read_base>().get_signature(), "int fn_tests::read_base(Base)");
    EXPECT_EQ(function_of<&fn_tests::apply_callback>().get_signature(),
              "int fn_tests::apply_callback(IntCallback, int)");
    EXPECT_EQ(function_of<&fn_tests::first_of>().get_signature(), "int fn_tests::first_of(int*)");
    EXPECT_EQ(function_of<&fn_tests::echo>().get_signature(),
              "std::string fn_tests::echo(std::string)");

    const std::string_view unique_signature = function_of<&fn_tests::make_unique>().get_signature();
    EXPECT_NE(unique_signature.find("fn_tests::make_unique"), std::string_view::npos);
    EXPECT_NE(unique_signature.find("unique_ptr<int"), std::string_view::npos);
}

TEST(FunctionTest, InvokeFailureDiagnosticsMatrix)
{
    struct Row
    {
        std::string_view label;
        std::function<speculo::Error()> failure;
        ErrorKind kind = ErrorKind::NotSupported;
        std::string_view fragment;
    };

    int value = 1;
    const int frozen = 2;
    double wrong = 3.0;
    fn_tests::MoveOnly donor;
    ObjectRef null_ref;

    const std::array<Row, 12> rows{{
        {"invoke non-copyable referent",
         [&] { return function_of<&fn_tests::move_slot>().invoke({}).unwrap_err(); },
         ErrorKind::NotSupported, "not copyable"},
        {"invoke non-loadable parameter",
         [&] { return function_of<&fn_tests::take_and_return>().invoke({donor}).unwrap_err(); },
         ErrorKind::NotSupported, "not available"},
        {"invoke_ref non-loadable parameter",
         [&] { return function_of<&fn_tests::take_and_return>().invoke_ref({donor}).unwrap_err(); },
         ErrorKind::NotSupported, "not available"},
        {"invoke move-only by value",
         [&] { return function_of<&fn_tests::take>().invoke({donor}).unwrap_err(); },
         ErrorKind::NotSupported, "not available"},
        {"invoke_ref non-reference result",
         [&] { return function_of<&fn_tests::add>().invoke_ref({value, value}).unwrap_err(); },
         ErrorKind::NotSupported, "does not return a reference"},
        {"invoke_ref move-only by value",
         [&] { return function_of<&fn_tests::take>().invoke_ref({donor}).unwrap_err(); },
         ErrorKind::NotSupported, "does not return a reference"},
        {"invoke variadic",
         [&] { return function_of<&fn_tests::variadic>().invoke({value}).unwrap_err(); },
         ErrorKind::NotSupported, "variadic"},
        {"invoke const to int&",
         [&] { return function_of<&fn_tests::write_ref>().invoke({frozen}).unwrap_err(); },
         ErrorKind::TypeMismatch, "const object"},
        {"invoke const to int&&",
         [&] { return function_of<&fn_tests::sink>().invoke({frozen}).unwrap_err(); },
         ErrorKind::TypeMismatch, "const object"},
        {"invoke null argument",
         [&] { return function_of<&fn_tests::unary>().invoke({null_ref}).unwrap_err(); },
         ErrorKind::TypeMismatch, "is null"},
        {"invoke wrong type",
         [&] { return function_of<&fn_tests::add>().invoke({value, wrong}).unwrap_err(); },
         ErrorKind::TypeMismatch, "argument 1"},
        {"invoke wrong count",
         [&] { return function_of<&fn_tests::add>().invoke({value}).unwrap_err(); },
         ErrorKind::InvalidArgument, "expected 2"},
    }};

    for (const Row& row : rows)
    {
        SCOPED_TRACE(row.label);
        const speculo::Error error = row.failure();
        EXPECT_EQ(error.kind, row.kind);
        EXPECT_NE(error.message.find(row.fragment), std::string::npos) << error.message;
    }
}

TEST(FunctionTest, UnmaterializedFunctionIsAbsentFromTheRegistry)
{
    constexpr std::string_view signature = "int fn_tests::never_materialized(int)";
    EXPECT_TRUE(Function::for_name(signature).is_none());

    const Function& fn = function_of<&fn_tests::never_materialized>();
    ASSERT_EQ(fn.get_signature(), signature);
    ASSERT_TRUE(Function::for_name(signature).is_some());
    EXPECT_EQ(Function::for_name(signature).unwrap(), fn);
}

// ── Option state matrix: every probe asserts Some and None together ──────────

TEST(FunctionTest, OptionStateMatrix)
{
    int value = 7;
    const int const_value = 7;
    volatile int volatile_value = 7;

    ObjectRef mutable_view{value};
    ObjectRef const_view{const_value};
    ObjectRef volatile_view{volatile_value};
    ObjectRef null_view;
    const ObjectRef frozen_view = mutable_view;

    Object boxed = Object::from_value(7).unwrap();
    const Object boxed_const = Object::from_value(7).unwrap();
    Object empty;

    const Function& add = function_of<&fn_tests::add>();
    const Function& hidden = function_of<&fn_tests::hidden>();
    const Function& function_only = function_of<&fn_tests::function_only>();
    const Function& parameter_only = function_of<&fn_tests::parameter_only>();
    const Function& unreadable_fn = function_of<&fn_tests::unreadable_annotation>();
    const auto parameter_annotations = parameter_only.get_parameter_annotations(0);

    struct Row
    {
        std::string_view label;
        std::function<std::array<bool, 2>()> state;
        bool expected = false;
    };

    const std::array<Row, 25> rows{{
        {"for_name registered", [&] { return option_state(Function::for_name(add.get_signature())); }, true},
        {"for_name missing", [&] { return option_state(Function::for_name("int fn_tests::missing(int)")); }, false},
        {"for_name internal", [&] { return option_state(Function::for_name(hidden.get_signature())); }, false},
        {"function annotation present",
         [&] { return option_state(function_only.get_annotation<fn_tests::Tag>()); }, true},
        {"function annotation wrong tag",
         [&] { return option_state(function_only.get_annotation<fn_tests::OtherTag>()); }, false},
        {"function annotation on parameter-only",
         [&] { return option_state(parameter_only.get_annotation<fn_tests::Tag>()); }, false},
        {"parameter annotation present",
         [&] { return option_state(parameter_only.get_parameter_annotation<int>(0)); }, true},
        {"parameter annotation wrong type",
         [&] { return option_state(parameter_only.get_parameter_annotation<fn_tests::Tag>(0)); }, false},
        {"Annotation::get matching",
         [&] { return option_state(parameter_annotations[0].get<int>()); }, true},
        {"Annotation::get wrong type",
         [&] { return option_state(parameter_annotations[0].get<fn_tests::Tag>()); }, false},
        {"Annotation::get unreadable",
         [&] { return option_state(unreadable_fn.get_annotations()[0].get<fn_tests::PointerTag>()); }, false},
        {"ObjectRef::as<int>", [&] { return option_state(mutable_view.as<int>()); }, true},
        {"ObjectRef::as<double>", [&] { return option_state(mutable_view.as<double>()); }, false},
        {"const view as<int>", [&] { return option_state(const_view.as<int>()); }, false},
        {"const view as<const int>", [&] { return option_state(const_view.as<const int>()); }, true},
        {"volatile view as<int>", [&] { return option_state(volatile_view.as<int>()); }, false},
        {"volatile view as<volatile int>", [&] { return option_state(volatile_view.as<volatile int>()); }, true},
        {"null view as<int>", [&] { return option_state(null_view.as<int>()); }, false},
        {"const handle as<int>", [&] { return option_state(frozen_view.as<int>()); }, true},
        {"const handle as<double>", [&] { return option_state(frozen_view.as<double>()); }, false},
        {"Object::as<int>", [&] { return option_state(boxed.as<int>()); }, true},
        {"Object::as<double>", [&] { return option_state(boxed.as<double>()); }, false},
        {"const Object as<int>", [&] { return option_state(boxed_const.as<int>()); }, true},
        {"const Object as<double>", [&] { return option_state(boxed_const.as<double>()); }, false},
        {"empty Object as<int>", [&] { return option_state(empty.as<int>()); }, false},
    }};

    for (const Row& row : rows)
    {
        SCOPED_TRACE(row.label);
        const std::array<bool, 2> state = row.state();
        const std::array<bool, 2> expected{row.expected, !row.expected};
        EXPECT_EQ(state, expected);
    }

    // A Some result hands out the referenced object itself.
    ASSERT_TRUE(mutable_view.as<int>().is_some());
    mutable_view.as<int>().unwrap() = 8;
    EXPECT_EQ(value, 8);
}

// ── Result state matrix: Ok and Err cases side by side ──────────────────────

TEST(FunctionTest, ResultStateMatrix)
{
    int value = 7;
    const int const_value = 7;
    volatile int volatile_value = 7;
    fn_tests::MoveOnly move_only{7};

    ObjectRef mutable_view{value};
    ObjectRef const_view{const_value};
    ObjectRef volatile_view{volatile_value};
    ObjectRef move_only_view{move_only};
    ObjectRef null_view;

    Object boxed = Object::from_value(7).unwrap();
    Object move_only_box = Object::from_value(fn_tests::MoveOnly{7}).unwrap();
    Object empty;

    int left = 1;
    int right = 2;
    double wrong = 2.0;
    fn_tests::MoveOnly donor;

    const auto parameter_annotations = function_of<&fn_tests::parameter_only>().get_parameter_annotations(0);
    const auto function_annotations = function_of<&fn_tests::function_only>().get_annotations();
    const auto unreadable_annotations = function_of<&fn_tests::unreadable_annotation>().get_annotations();

    struct Row
    {
        std::string_view label;
        std::function<Outcome<Object>()> operation;
        bool expected_ok = false;
        ErrorKind error = ErrorKind::NotSupported;
    };

    const std::array<Row, 16> rows{{
        {"ObjectRef::clone mutable", [&] { return mutable_view.clone(); }, true, ErrorKind::NotSupported},
        {"ObjectRef::clone const", [&] { return const_view.clone(); }, true, ErrorKind::NotSupported},
        {"ObjectRef::clone volatile", [&] { return volatile_view.clone(); }, false, ErrorKind::NotSupported},
        {"ObjectRef::clone move-only", [&] { return move_only_view.clone(); }, false, ErrorKind::NotSupported},
        {"ObjectRef::clone null", [&] { return null_view.clone(); }, false, ErrorKind::NullInstance},
        {"Object::clone", [&] { return boxed.clone(); }, true, ErrorKind::NotSupported},
        {"Object::clone empty", [&] { return empty.clone(); }, false, ErrorKind::NullInstance},
        {"Object::clone move-only", [&] { return move_only_box.clone(); }, false, ErrorKind::NotSupported},
        {"Annotation::value parameter",
         [&] { return parameter_annotations[0].value(); }, true, ErrorKind::NotSupported},
        {"Annotation::value function",
         [&] { return function_annotations[0].value(); }, true, ErrorKind::NotSupported},
        {"Annotation::value unreadable",
         [&] { return unreadable_annotations[0].value(); }, false, ErrorKind::NotSupported},
        {"invoke succeeds", [&] { return function_of<&fn_tests::add>().invoke({left, right}); }, true,
         ErrorKind::NotSupported},
        {"invoke wrong type",
         [&] { return function_of<&fn_tests::add>().invoke({left, wrong}); }, false, ErrorKind::TypeMismatch},
        {"invoke wrong count",
         [&] { return function_of<&fn_tests::add>().invoke({left}); }, false, ErrorKind::InvalidArgument},
        {"invoke variadic", [&] { return function_of<&fn_tests::variadic>().invoke({left}); }, false,
         ErrorKind::NotSupported},
        {"invoke non-copyable referent", [&] { return function_of<&fn_tests::move_slot>().invoke({}); }, false,
         ErrorKind::NotSupported},
    }};

    for (const Row& row : rows)
    {
        SCOPED_TRACE(row.label);
        Outcome<Object> outcome = row.operation();
        EXPECT_EQ(outcome.is_ok(), row.expected_ok);
        EXPECT_EQ(outcome.is_err(), !row.expected_ok);

        if (row.expected_ok)
        {
            ASSERT_TRUE(outcome.is_ok());
            EXPECT_FALSE(std::move(outcome).unwrap().is_null());
        }
        else
        {
            ASSERT_TRUE(outcome.is_err());
            EXPECT_EQ(outcome.unwrap_err().kind, row.error);
        }
    }

    // Ok results actually carry the value, and clones are independent copies.
    Object ref_clone = mutable_view.clone().unwrap();
    EXPECT_EQ(ref_clone.as<int>().unwrap(), 7);
    value = 9;
    EXPECT_EQ(ref_clone.as<int>().unwrap(), 7);
    value = 7;

    Object object_clone = boxed.clone().unwrap();
    EXPECT_EQ(object_clone.as<int>().unwrap(), 7);

    EXPECT_EQ(parameter_annotations[0].value().unwrap().as<int>().unwrap(), 1);

    // Object::take: Ok / TypeMismatch / NullInstance.
    Object taken = Object::from_value(7).unwrap();
    Outcome<int> taken_result = std::move(taken).take<int>();
    ASSERT_TRUE(taken_result.is_ok());
    EXPECT_EQ(taken_result.unwrap(), 7);
    EXPECT_TRUE(taken.is_null());

    Object wrong_take = Object::from_value(7).unwrap();
    auto wrong_result = std::move(wrong_take).take<double>();
    ASSERT_TRUE(wrong_result.is_err());
    EXPECT_EQ(wrong_result.unwrap_err().kind, ErrorKind::TypeMismatch);
    EXPECT_FALSE(wrong_take.is_null()); // a failed take leaves the object intact

    Object empty_take;
    Outcome<int> empty_result = std::move(empty_take).take<int>();
    ASSERT_TRUE(empty_result.is_err());
    EXPECT_EQ(empty_result.unwrap_err().kind, ErrorKind::NullInstance);
}

// ── concurrency: registration, lookup, invocation ───────────────────────────

TEST(FunctionTest, ConcurrentFirstMaterializationOfOneFunction)
{
    constexpr std::size_t kWorkers = 8;
    std::array<const Function*, kWorkers> handles{};
    std::array<bool, kWorkers> invoked{};
    std::array<int, kWorkers> results{};

    std::latch gate(kWorkers);
    std::vector<std::thread> workers;
    workers.reserve(kWorkers);

    for (std::size_t i = 0; i < kWorkers; ++i)
    {
        workers.emplace_back([&, i]
        {
            gate.arrive_and_wait();
            handles[i] = std::addressof(function_of<&fn_tests::concurrent_same>());

            Outcome<Object> outcome = function_of<&fn_tests::concurrent_same>().invoke({static_cast<int>(i)});
            invoked[i] = outcome.is_ok();
            if (outcome.is_ok())
            {
                results[i] = std::move(outcome).unwrap().as<int>().unwrap();
            }
        });
    }

    for (std::thread& worker : workers)
    {
        worker.join();
    }

    for (std::size_t i = 0; i < kWorkers; ++i)
    {
        EXPECT_EQ(handles[i], handles[0]) << i;
        EXPECT_TRUE(invoked[i]) << i;
        EXPECT_EQ(results[i], static_cast<int>(i) + 1) << i;
    }
}

TEST(FunctionTest, ConcurrentRegistrationAndLookup)
{
    constexpr std::size_t kWorkers = 8;
    constexpr int kRepeats = 50;

    const Function& stable = function_of<&fn_tests::add>();
    const std::string_view stable_signature = stable.get_signature();

    std::array<const Function*, kWorkers> handles{};
    std::array<bool, kWorkers> ok{};
    std::latch gate(kWorkers);
    std::vector<std::thread> workers;
    workers.reserve(kWorkers);

    for (std::size_t i = 0; i < kWorkers; ++i)
    {
        workers.emplace_back([&, i]
        {
            gate.arrive_and_wait();
            handles[i] = concurrent_handle(i);
            bool thread_ok = handles[i] != nullptr;

            for (int repeat = 0; repeat < kRepeats && thread_ok; ++repeat)
            {
                const auto own = Function::for_name(handles[i]->get_signature());
                const auto stable_lookup = Function::for_name(stable_signature);
                thread_ok = own.is_some() && stable_lookup.is_some()
                         && own.unwrap() == *handles[i] && stable_lookup.unwrap() == stable;

                if ((repeat % 10) == 0)
                {
                    const std::vector<std::string_view> signatures = Function::registered_signatures();
                    thread_ok = thread_ok
                             && std::ranges::find(signatures, stable_signature) != signatures.end();
                }
            }

            ok[i] = thread_ok;
        });
    }

    for (std::thread& worker : workers)
    {
        worker.join();
    }

    for (std::size_t i = 0; i < kWorkers; ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_TRUE(ok[i]);
        ASSERT_NE(handles[i], nullptr);

        const auto found = Function::for_name(handles[i]->get_signature());
        ASSERT_TRUE(found.is_some());
        EXPECT_EQ(found.unwrap(), *handles[i]);
        EXPECT_EQ(take_value<int>(handles[i]->invoke({1})), static_cast<int>(i) + 1);

        for (std::size_t j = i + 1; j < kWorkers; ++j)
        {
            EXPECT_NE(*handles[i], *handles[j]);
        }
    }

    const std::vector<std::string_view> signatures = Function::registered_signatures();
    EXPECT_TRUE(std::ranges::is_sorted(signatures));
    EXPECT_EQ(std::ranges::adjacent_find(signatures), signatures.end());
}

TEST(FunctionTest, ReentrantInvocationAndLookup)
{
    // The invoked function materializes and calls another function.
    const Function& outer = function_of<&fn_tests::reentrant_outer>();
    EXPECT_EQ(take_value<int>(outer.invoke({4})), 10);

    ASSERT_TRUE(Function::for_name("int fn_tests::reentrant_inner(int)").is_some());
    EXPECT_EQ(take_value<int>(Function::for_name("int fn_tests::reentrant_inner(int)").unwrap().invoke({4})), 5);

    // The invoked function recursively invokes itself through its own handle.
    const Function& recursive = function_of<&fn_tests::recursive_reflect>();
    EXPECT_EQ(take_value<int>(recursive.invoke({5})), 5);

    // for_name works from inside the invoked function as well.
    const Function& lookup = function_of<&fn_tests::reentrant_lookup>();
    EXPECT_EQ(take_value<int>(lookup.invoke({7})), 7);
}

// ── cross-TU registration: materialized in another translation unit ─────────

TEST(FunctionTest, CrossTranslationUnitRegistrationAndLookup)
{
    cross_tu_register();
    cross_tu_register(); // idempotent across TUs

    constexpr std::string_view shared_signature = "int cross_tu::shared_fn(int)";
    constexpr std::string_view local_signature = "int cross_tu::local_to_main(int)";

    for (std::string_view signature : {shared_signature, local_signature})
    {
        SCOPED_TRACE(signature);
        const auto found = Function::for_name(signature);
        ASSERT_TRUE(found.is_some());
        EXPECT_TRUE(found.unwrap().has_canonical_name());
        EXPECT_TRUE(found.unwrap().is_invokable());
    }

    // shared_fn lives in the other TU; local_to_main lives in this one but was
    // materialized (and therefore registered) from the other TU.
    const Function& shared = Function::for_name(shared_signature).unwrap();
    const Function& local = Function::for_name(local_signature).unwrap();
    EXPECT_EQ(take_value<int>(shared.invoke({4})), 12);
    EXPECT_EQ(take_value<int>(local.invoke({4})), 8);

    const std::vector<std::string_view> signatures = Function::registered_signatures();
    EXPECT_NE(std::ranges::find(signatures, shared_signature), signatures.end());
    EXPECT_NE(std::ranges::find(signatures, local_signature), signatures.end());
}

} // namespace
