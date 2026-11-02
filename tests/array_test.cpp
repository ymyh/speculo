#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/type_fixtures.h"

TEST(ArrayTest, StaticArrayAccess)
{
    Arrays a;
    const speculo::Class& cls = speculo::class_of<Arrays>();

    speculo::ObjectRef arr = cls.get_field("v").unwrap().get_ref(a).unwrap();
    EXPECT_TRUE(arr.type().is_array());
    EXPECT_EQ(arr.type(), speculo::class_of<int[3]>());
    EXPECT_EQ(speculo::Array::get_length(arr).unwrap(), 3u);
    EXPECT_EQ(speculo::Array::get_element_type(arr).unwrap(), speculo::class_of<int>());

    EXPECT_EQ(speculo::Array::get(arr, 1).unwrap().as<int>().unwrap(), 2);

    int seven = 7;
    EXPECT_TRUE(speculo::Array::set(arr, 1, seven).is_ok());
    EXPECT_EQ(a.v[1], 7);

    auto element = speculo::Array::get_ref(arr, 2);
    ASSERT_TRUE(element.is_ok());
    EXPECT_EQ(element.unwrap().as<int>().unwrap(), 3);
    element.unwrap().as<int>().unwrap() = 9;
    EXPECT_EQ(a.v[2], 9);

    // Out of range / wrong element type.
    EXPECT_EQ(speculo::Array::get(arr, 5).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
    double wrong = 1.0;
    EXPECT_EQ(speculo::Array::set(arr, 0, wrong).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    // Non-array object.
    int scalar = 0;
    EXPECT_EQ(speculo::Array::get_length(speculo::ObjectRef(scalar)).unwrap_err().kind,
              speculo::ErrorKind::TypeMismatch);
    EXPECT_TRUE(speculo::Array::get_element_type(speculo::ObjectRef(scalar)).is_none());
    EXPECT_TRUE(speculo::Array::get_element_type(speculo::ObjectRef{}).is_none());
}

TEST(ArrayTest, MultiDimensionalRankExtent)
{
    Arrays a;
    const speculo::Class& cls = speculo::class_of<Arrays>();

    speculo::ObjectRef matrix = cls.get_field("m").unwrap().get_ref(a).unwrap();
    EXPECT_EQ(matrix.type().get_array_rank(), 2u);
    EXPECT_EQ(matrix.type().get_array_extent().unwrap(), 2u);
    EXPECT_EQ(speculo::Array::get_length(matrix).unwrap(), 2u);

    auto row = speculo::Array::get_ref(matrix, 1);
    ASSERT_TRUE(row.is_ok());
    EXPECT_EQ(row.unwrap().type().get_array_rank(), 1u);
    EXPECT_EQ(row.unwrap().type().get_array_extent().unwrap(), 2u);
    EXPECT_EQ(speculo::Array::get_length(row.unwrap()).unwrap(), 2u);
    EXPECT_EQ(speculo::Array::get_element_type(row.unwrap()).unwrap(), speculo::class_of<int>());

    EXPECT_EQ(speculo::Array::get(row.unwrap(), 0).unwrap().as<int>().unwrap(), 3);
    EXPECT_EQ(speculo::Array::get(row.unwrap(), 1).unwrap().as<int>().unwrap(), 4);

    int nine = 9;
    EXPECT_TRUE(speculo::Array::set(row.unwrap(), 0, nine).is_ok());
    EXPECT_EQ(a.m[1][0], 9);

    // Multi-dim arrays decompose one level at a time from the Class API too.
    EXPECT_EQ(speculo::class_of<int[2][3]>().get_array_rank(), 2u);
    EXPECT_EQ(speculo::class_of<int[2][3]>().get_element_type().unwrap(), speculo::class_of<int[3]>());
}

TEST(ArrayTest, ConstElementArraysAreReadableThroughUnqualifiedHandles)
{
    static const int values[3] = { 4, 5, 6 };
    static_assert(std::is_const_v<std::remove_extent_t<decltype(values)>>);

    // A handle typed by the cv-qualified array class (element type is `const int`).
    speculo::ObjectRef arr(static_cast<const void*>(values), speculo::class_of<const int[3]>());
    EXPECT_EQ(speculo::Array::get_length(arr).unwrap(), 3u);
    EXPECT_EQ(speculo::Array::get_element_type(arr).unwrap(), speculo::class_of<const int>());

    // Values can be read by copy...
    auto copy = speculo::Array::get(arr, 1);
    ASSERT_TRUE(copy.is_ok());
    EXPECT_EQ(std::move(copy).unwrap().as<int>().unwrap(), 5);

    // ...and viewed: the element handle is keyed by `int` and marked const.
    auto element = speculo::Array::get_ref(arr, 2);
    ASSERT_TRUE(element.is_ok());
    EXPECT_TRUE(element.unwrap().is_const());
    EXPECT_EQ(element.unwrap().type(), speculo::class_of<int>());
    ASSERT_TRUE(element.unwrap().as<const int>().is_some());
    EXPECT_EQ(element.unwrap().as<const int>().unwrap(), 6);
    EXPECT_TRUE(element.unwrap().as<int>().is_none());

    // Const elements are read-only, not "unsupported".
    int one = 1;
    EXPECT_EQ(speculo::Array::set(arr, 0, one).unwrap_err().kind, speculo::ErrorKind::ReadOnly);
}

TEST(ArrayTest, VolatileElementsAreOnlyAccessibleThroughVolatileViews)
{
    volatile int values[3] = { 1, 2, 3 };
    speculo::ObjectRef arr(values);
    EXPECT_TRUE(arr.is_volatile());
    EXPECT_EQ(speculo::Array::get_length(arr).unwrap(), 3u);

    int nine = 9;
    EXPECT_EQ(speculo::Array::get(arr, 0).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(speculo::Array::set(arr, 0, nine).unwrap_err().kind, speculo::ErrorKind::NotSupported);

    auto element = speculo::Array::get_ref(arr, 1);
    ASSERT_TRUE(element.is_ok());
    EXPECT_TRUE(element.unwrap().is_volatile());
    EXPECT_TRUE(element.unwrap().as<int>().is_none());
    EXPECT_EQ(element.unwrap().as<volatile int>().unwrap(), 2);

    // Same when only the element type carries the qualifier.
    static volatile int device[2] = { 5, 6 };
    speculo::ObjectRef qualified(static_cast<void*>(const_cast<int*>(device)), speculo::class_of<volatile int[2]>());
    auto qualified_element = speculo::Array::get_ref(qualified, 0);
    ASSERT_TRUE(qualified_element.is_ok());
    EXPECT_TRUE(qualified_element.unwrap().is_volatile());
    EXPECT_EQ(speculo::Array::get(qualified, 0).unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(ArrayTest, DynamicArrayNewInstance)
{
    auto made = speculo::Array::new_instance(speculo::class_of<int>(), 4);
    ASSERT_TRUE(made.is_ok());
    speculo::Object dyn = std::move(made).unwrap();

    speculo::ObjectRef ref = dyn.ref();
    EXPECT_TRUE(ref.type().is_array());
    EXPECT_EQ(speculo::Array::get_length(ref).unwrap(), 4u);
    EXPECT_EQ(speculo::Array::get_element_type(ref).unwrap(), speculo::class_of<int>());

    int forty_two = 42;
    EXPECT_TRUE(speculo::Array::set(ref, 2, forty_two).is_ok());
    EXPECT_EQ(speculo::Array::get(ref, 2).unwrap().as<int>().unwrap(), 42);

    auto element = speculo::Array::get_ref(ref, 3);
    ASSERT_TRUE(element.is_ok());
    element.unwrap().as<int>().unwrap() = 7;
    EXPECT_EQ(speculo::Array::get(ref, 3).unwrap().as<int>().unwrap(), 7);

    // Zero length is valid.
    auto empty = speculo::Array::new_instance(speculo::class_of<int>(), 0);
    ASSERT_TRUE(empty.is_ok());
    EXPECT_EQ(speculo::Array::get_length(std::move(empty).unwrap().ref()).unwrap(), 0u);

    // Element types that are not default-constructible cannot back a dynamic array.
    auto bad = speculo::Array::new_instance(speculo::class_of<NoDefault>(), 3);
    ASSERT_TRUE(bad.is_err());
    EXPECT_EQ(bad.unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(speculo::Array::get_ref(ref, 9).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
}

TEST(ArrayTest, DynamicArrayClassIsAFullDescriptor)
{
    auto ints = speculo::Array::new_instance(speculo::class_of<int>(), 3);
    auto smalls = speculo::Array::new_instance(speculo::class_of<SmallObject>(), 2);
    ASSERT_TRUE(ints.is_ok());
    ASSERT_TRUE(smalls.is_ok());
    speculo::Object int_array = std::move(ints).unwrap();
    speculo::Object small_array = std::move(smalls).unwrap();

    const speculo::Class& int_class = int_array.type();
    const speculo::Class& small_class = small_array.type();

    // A stable display name, not a registrable canonical one.
    EXPECT_EQ(int_class.get_name(), "int[]");
    EXPECT_EQ(int_class.get_qualified_name(), "int[]");
    EXPECT_NE(small_class.get_name(), "");
    EXPECT_TRUE(small_class.get_qualified_name().ends_with("SmallObject[]"));
    EXPECT_NE(int_class, small_class);
    EXPECT_FALSE(int_class.has_canonical_name());
    EXPECT_FALSE(small_class.has_canonical_name());

    // Nothing was registered under the empty (or any array) name.
    speculo::register_class(int_class);
    speculo::register_class(small_class);
    EXPECT_TRUE(speculo::Class::for_name("").is_none());
    // (`int[]` is also the spelling of the real unbounded array type, which may be
    // registered; the dynamic array class itself must not be.)
    auto by_array_name = speculo::Class::for_name("int[]");
    EXPECT_TRUE(by_array_name.is_none() || by_array_name.unwrap() != int_class);

    // It describes itself as an array of E.
    EXPECT_TRUE(int_class.is_array());
    EXPECT_EQ(int_class.get_element_type().unwrap(), speculo::class_of<int>());
    EXPECT_EQ(int_class.get_array_rank(), 1u);

    // The length is per object, never a property of the class.
    EXPECT_TRUE(int_class.get_array_extent().is_none());
    EXPECT_EQ(speculo::Array::get_length(int_array.ref()).unwrap(), 3u);
    EXPECT_EQ(speculo::class_of<int[3]>().get_array_extent().unwrap(), 3u);
}

TEST(ArrayTest, DynamicArrayCloneCopiesElements)
{
    auto made = speculo::Array::new_instance(speculo::class_of<SmallObject>(), 3);
    ASSERT_TRUE(made.is_ok());
    speculo::Object original = std::move(made).unwrap();
    speculo::ObjectRef original_ref = original.ref();
    SmallObject nine{ 9 };
    ASSERT_TRUE(speculo::Array::set(original_ref, 1, nine).is_ok());

    auto copied = original.clone();
    ASSERT_TRUE(copied.is_ok());
    speculo::Object copy = std::move(copied).unwrap();
    EXPECT_EQ(copy.type(), original.type());
    EXPECT_NE(copy.raw_data(), original.raw_data());
    EXPECT_EQ(speculo::Array::get_length(copy.ref()).unwrap(), 3u);
    EXPECT_EQ(std::move(speculo::Array::get(copy.ref(), 1)).unwrap().as<SmallObject>().unwrap().v, 9);
    EXPECT_EQ(std::move(speculo::Array::get(copy.ref(), 0)).unwrap().as<SmallObject>().unwrap().v, 1);

    // The copy is independent of the source.
    speculo::Array::get_ref(copy.ref(), 1).unwrap().as<SmallObject>().unwrap().v = 42;
    EXPECT_EQ(std::move(speculo::Array::get(original_ref, 1)).unwrap().as<SmallObject>().unwrap().v, 9);

    // ObjectRef::clone works the same way, including for empty arrays.
    EXPECT_TRUE(original_ref.clone().is_ok());
    auto empty = speculo::Array::new_instance(speculo::class_of<SmallObject>(), 0);
    ASSERT_TRUE(empty.is_ok());
    speculo::Object empty_array = std::move(empty).unwrap();
    auto empty_copy = empty_array.clone();
    ASSERT_TRUE(empty_copy.is_ok());
    EXPECT_EQ(speculo::Array::get_length(std::move(empty_copy).unwrap().ref()).unwrap(), 0u);

    // Elements that cannot be copied keep the array uncopyable.
    auto pinned = speculo::Array::new_instance(speculo::class_of<PinnedElement>(), 2);
    ASSERT_TRUE(pinned.is_ok());
    speculo::Object pinned_array = std::move(pinned).unwrap();
    EXPECT_EQ(pinned_array.clone().unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(ArrayTest, DynamicArrayOfTrackedElementsClonesAndDestroys)
{
    Tracked::alive = 0;
    {
        auto made = speculo::Array::new_instance(speculo::class_of<Tracked>(), 4);
        ASSERT_TRUE(made.is_ok());
        speculo::Object original = std::move(made).unwrap();
        EXPECT_EQ(Tracked::alive, 4);

        auto copied = original.clone();
        ASSERT_TRUE(copied.is_ok());
        speculo::Object copy = std::move(copied).unwrap();
        EXPECT_EQ(Tracked::alive, 8);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

TEST(ArrayTest, DynamicArrayHonorsOverAlignedElements)
{
    for (std::size_t count : { std::size_t{ 0 }, std::size_t{ 1 }, std::size_t{ 5 } })
    {
        auto made = speculo::Array::new_instance(speculo::class_of<OverAligned>(), count);
        ASSERT_TRUE(made.is_ok());
        speculo::Object dyn = std::move(made).unwrap();

        EXPECT_EQ(speculo::Array::get_length(dyn.ref()).unwrap(), count);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(dyn.raw_data()) % alignof(OverAligned), 0u);

        if (count > 0)
        {
            auto element = speculo::Array::get_ref(dyn.ref(), count - 1);
            ASSERT_TRUE(element.is_ok());
            EXPECT_EQ(element.unwrap().as<OverAligned>().unwrap().v, 3);
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(element.unwrap().raw_data()) % alignof(OverAligned), 0u);
        }
    }
}

TEST(ArrayTest, DynamicArrayRejectsOverflowingLength)
{
    constexpr std::size_t max = std::numeric_limits<std::size_t>::max();

    // count * sizeof(E) alone would wrap to a tiny allocation.
    auto wrapped = speculo::Array::new_instance(speculo::class_of<SmallObject>(), max / sizeof(SmallObject) + 1);
    ASSERT_TRUE(wrapped.is_err());
    EXPECT_EQ(wrapped.unwrap_err().kind, speculo::ErrorKind::InvalidArgument);

    // So would adding the header to an otherwise representable product.
    auto header = speculo::Array::new_instance(speculo::class_of<char>(), max);
    ASSERT_TRUE(header.is_err());
    EXPECT_EQ(header.unwrap_err().kind, speculo::ErrorKind::InvalidArgument);

    auto over_aligned = speculo::Array::new_instance(speculo::class_of<OverAligned>(), max / alignof(OverAligned));
    ASSERT_TRUE(over_aligned.is_err());
    EXPECT_EQ(over_aligned.unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
}

TEST(ArrayTest, DynamicArrayOfTrackedElements)
{
    // Verifies the header-based length and per-element destruction via a
    // non-trivial element type (leaks/double frees would surface here).
    Tracked::alive = 0;
    {
        auto made = speculo::Array::new_instance(speculo::class_of<Tracked>(), 3);
        ASSERT_TRUE(made.is_ok());
        speculo::Object dyn = std::move(made).unwrap();
        speculo::ObjectRef ref = dyn.ref();

        EXPECT_EQ(Tracked::alive, 3); // three default-constructed elements
        EXPECT_EQ(speculo::Array::get_length(ref).unwrap(), 3u);

        Tracked value(99);
        EXPECT_TRUE(speculo::Array::set(ref, 0, value).is_ok());
        EXPECT_EQ(speculo::Array::get(ref, 0).unwrap().as<Tracked>().unwrap().v, 99);
    }
    EXPECT_EQ(Tracked::alive, 0); // all elements destroyed by the dynamic array
}

// ─────────────────────────────────────────────────────────────────────────────
// operation matrix: object form × operation
//
// 5 rows × 4 Result-returning operations = 20 cells, plus the Option-returning
// `get_element_type` in the same loop. The interesting part is the order: `set`
// rejects a const array before any bounds check, every operation on a null view
// is NullInstance, and non-array objects are TypeMismatch everywhere.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{

enum class ObjectForm { Mutable, Const, Volatile, Scalar, Null };
enum class Operation { Length, Get, Set, GetRef };
enum class Expected { Ok, NullInstance, TypeMismatch, ReadOnly, NotSupported, InvalidArgument };

constexpr std::array<ObjectForm, 5> kObjectForms{
    ObjectForm::Mutable,
    ObjectForm::Const,
    ObjectForm::Volatile,
    ObjectForm::Scalar,
    ObjectForm::Null,
};

constexpr std::array<Operation, 4> kOperations{
    Operation::Length,
    Operation::Get,
    Operation::Set,
    Operation::GetRef,
};

// 5 x 4 = 20 cells.
//                  Length          Get              Set             GetRef
constexpr std::array<std::array<Expected, 4>, 5> kArrayMatrix{{
    /* Mutable  */ { Expected::Ok,           Expected::Ok,           Expected::Ok,           Expected::Ok },
    /* Const    */ { Expected::Ok,           Expected::Ok,           Expected::ReadOnly,     Expected::Ok },
    /* Volatile */ { Expected::Ok,           Expected::NotSupported, Expected::NotSupported, Expected::Ok },
    /* Scalar   */ { Expected::TypeMismatch, Expected::TypeMismatch, Expected::TypeMismatch, Expected::TypeMismatch },
    /* Null     */ { Expected::NullInstance, Expected::NullInstance, Expected::NullInstance, Expected::NullInstance },
}};

// `Array::get_element_type` for the same rows (it returns Option, not Result).
constexpr std::array<bool, 5> kElementTypeIsSome{ true, true, true, false, false };

[[nodiscard]] constexpr auto form_name(ObjectForm form) -> std::string_view
{
    switch (form)
    {
        case ObjectForm::Mutable:  return "mutable";
        case ObjectForm::Const:    return "const";
        case ObjectForm::Volatile: return "volatile";
        case ObjectForm::Scalar:   return "scalar";
        case ObjectForm::Null:     return "null";
    }

    return "?";
}

[[nodiscard]] constexpr auto form_name(Operation operation) -> std::string_view
{
    switch (operation)
    {
        case Operation::Length: return "length";
        case Operation::Get:    return "get";
        case Operation::Set:    return "set";
        case Operation::GetRef: return "get_ref";
    }

    return "?";
}

[[nodiscard]] constexpr auto expected_kind(Expected expected) -> speculo::ErrorKind
{
    switch (expected)
    {
        case Expected::Ok:              return speculo::ErrorKind::NotFound;
        case Expected::NullInstance:    return speculo::ErrorKind::NullInstance;
        case Expected::TypeMismatch:    return speculo::ErrorKind::TypeMismatch;
        case Expected::ReadOnly:        return speculo::ErrorKind::ReadOnly;
        case Expected::NotSupported:    return speculo::ErrorKind::NotSupported;
        case Expected::InvalidArgument: return speculo::ErrorKind::InvalidArgument;
    }

    return speculo::ErrorKind::NotFound;
}

/// Runs one operation and normalizes it to "success or the error it reported".
[[nodiscard]] auto run(Operation operation, const speculo::ObjectRef& object, int& nine) -> std::optional<speculo::Error>
{
    switch (operation)
    {
        case Operation::Length:
        {
            auto result = speculo::Array::get_length(object);
            if (result.is_ok())
            {
                return std::nullopt;
            }

            return result.unwrap_err();
        }
        case Operation::Get:
        {
            auto result = speculo::Array::get(object, 0);
            if (result.is_ok())
            {
                return std::nullopt;
            }

            return result.unwrap_err();
        }
        case Operation::Set:
        {
            auto result = speculo::Array::set(object, 0, speculo::ObjectRef(nine));
            if (result.is_ok())
            {
                return std::nullopt;
            }

            return result.unwrap_err();
        }
        case Operation::GetRef:
        {
            auto result = speculo::Array::get_ref(object, 0);
            if (result.is_ok())
            {
                return std::nullopt;
            }

            return result.unwrap_err();
        }
    }

    std::abort();
}

} // namespace

TEST(ArrayOperationMatrix, ObjectFormByOperation)
{
    int values[3] = { 1, 2, 3 };
    static const int const_values[3] = { 4, 5, 6 };
    volatile int volatile_values[3] = { 7, 8, 9 };
    int scalar = 0;
    int nine = 9;

    const std::array<speculo::ObjectRef, 5> objects{
        speculo::ObjectRef(values),
        speculo::ObjectRef(static_cast<const void*>(const_values), speculo::class_of<const int[3]>()),
        speculo::ObjectRef(volatile_values),
        speculo::ObjectRef(scalar),
        speculo::ObjectRef{},
    };

    for (std::size_t row = 0; row < kObjectForms.size(); ++row)
    {
        for (std::size_t column = 0; column < kOperations.size(); ++column)
        {
            SCOPED_TRACE(std::format("{}/{}", form_name(kObjectForms[row]), form_name(kOperations[column])));

            const Expected expected = kArrayMatrix[row][column];
            const std::optional<speculo::Error> error = run(kOperations[column], objects[row], nine);

            if (expected == Expected::Ok)
            {
                ASSERT_FALSE(error.has_value());
            }
            else
            {
                ASSERT_TRUE(error.has_value());
                EXPECT_EQ(error->kind, expected_kind(expected));
            }
        }

        SCOPED_TRACE(std::format("{}/get_element_type", form_name(kObjectForms[row])));
        EXPECT_EQ(speculo::Array::get_element_type(objects[row]).is_some(), kElementTypeIsSome[row]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// multi-index: operation × index form
//
// A mutable `int[2][3]`; `indices` is partitioned by .agents/testing.md §2
// (typical / boundary / empty / illegal), the expected kinds follow the check
// order recorded in .agents/testing.md §5.
//
//                 typical | partial      | empty           | too many        | outer OOR       | inner OOR
// get           | Ok      | NotSupported | InvalidArgument | InvalidArgument | InvalidArgument | InvalidArgument
// set           | Ok      | NotSupported | InvalidArgument | InvalidArgument | InvalidArgument | InvalidArgument
// get_ref       | Ok      | Ok           | InvalidArgument | InvalidArgument | InvalidArgument | InvalidArgument
//
// `set` passes the row itself for the partial form, so that cell reaches the
// "an array has no whole-array assignment" check instead of the type check.
//
// The three-way product is 3 operations × 6 index forms × 5 object forms = 90 >
// 64, so it is split into two full two-way matrices (18 + 15 cells) plus the
// three-way cells that pin the precedence: empty / too-many on scalar and null
// in `MultiIndexErrorsFollowTheDocumentedOrder`.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{

enum class IndexForm { Typical, Partial, Empty, TooMany, OuterOutOfRange, InnerOutOfRange };
enum class IndexedOperation { Get, Set, GetRef };

constexpr std::array<IndexForm, 6> kIndexForms{
    IndexForm::Typical,
    IndexForm::Partial,
    IndexForm::Empty,
    IndexForm::TooMany,
    IndexForm::OuterOutOfRange,
    IndexForm::InnerOutOfRange,
};

constexpr std::array<IndexedOperation, 3> kIndexedOperations{
    IndexedOperation::Get,
    IndexedOperation::Set,
    IndexedOperation::GetRef,
};

// 3 x 6 = 18 cells.
constexpr std::array<std::array<Expected, 6>, 3> kMultiIndexMatrix{{
    /* get     */ { Expected::Ok, Expected::NotSupported, Expected::InvalidArgument, Expected::InvalidArgument,
                    Expected::InvalidArgument, Expected::InvalidArgument },
    /* set     */ { Expected::Ok, Expected::NotSupported, Expected::InvalidArgument, Expected::InvalidArgument,
                    Expected::InvalidArgument, Expected::InvalidArgument },
    /* get_ref */ { Expected::Ok, Expected::Ok, Expected::InvalidArgument, Expected::InvalidArgument,
                    Expected::InvalidArgument, Expected::InvalidArgument },
}};

[[nodiscard]] constexpr auto form_name(IndexForm form) -> std::string_view
{
    switch (form)
    {
        case IndexForm::Typical:         return "typical";
        case IndexForm::Partial:         return "partial";
        case IndexForm::Empty:           return "empty";
        case IndexForm::TooMany:         return "too-many";
        case IndexForm::OuterOutOfRange: return "outer-oor";
        case IndexForm::InnerOutOfRange: return "inner-oor";
    }

    return "?";
}

[[nodiscard]] constexpr auto operation_name(IndexedOperation operation) -> std::string_view
{
    switch (operation)
    {
        case IndexedOperation::Get:    return "get";
        case IndexedOperation::Set:    return "set";
        case IndexedOperation::GetRef: return "get_ref";
    }

    return "?";
}

/// Runs one multi-index operation and normalizes it to "success or the error it reported".
[[nodiscard]] auto run_indexed(IndexedOperation operation, const speculo::ObjectRef& object,
                               std::initializer_list<std::size_t> indices, int& nine,
                               const speculo::ObjectRef& row) -> std::optional<speculo::Error>
{
    switch (operation)
    {
        case IndexedOperation::Get:
        {
            auto result = speculo::Array::get(object, indices);
            return result.is_ok() ? std::nullopt : std::optional<speculo::Error>(result.unwrap_err());
        }
        case IndexedOperation::Set:
        {
            const speculo::ObjectRef value = indices.size() == 1 ? row : speculo::ObjectRef(nine);
            auto result = speculo::Array::set(object, indices, value);
            return result.is_ok() ? std::nullopt : std::optional<speculo::Error>(result.unwrap_err());
        }
        case IndexedOperation::GetRef:
        {
            auto result = speculo::Array::get_ref(object, indices);
            return result.is_ok() ? std::nullopt : std::optional<speculo::Error>(result.unwrap_err());
        }
    }

    std::abort();
}

[[nodiscard]] auto run_form(IndexedOperation operation, const speculo::ObjectRef& object, IndexForm form, int& nine,
                            const speculo::ObjectRef& row) -> std::optional<speculo::Error>
{
    switch (form)
    {
        case IndexForm::Typical:         return run_indexed(operation, object, { 1, 2 }, nine, row);
        case IndexForm::Partial:         return run_indexed(operation, object, { 1 }, nine, row);
        case IndexForm::Empty:           return run_indexed(operation, object, {}, nine, row);
        case IndexForm::TooMany:         return run_indexed(operation, object, { 0, 0, 0 }, nine, row);
        case IndexForm::OuterOutOfRange: return run_indexed(operation, object, { 2, 0 }, nine, row);
        case IndexForm::InnerOutOfRange: return run_indexed(operation, object, { 0, 3 }, nine, row);
    }

    std::abort();
}

[[nodiscard]] auto same(const int (&left)[2][3], const int (&right)[2][3]) -> bool
{
    for (std::size_t i = 0; i < 2; ++i)
    {
        for (std::size_t j = 0; j < 3; ++j)
        {
            if (left[i][j] != right[i][j])
            {
                return false;
            }
        }
    }

    return true;
}

} // namespace

TEST(ArrayMultiIndexMatrix, OperationByIndexForm)
{
    int matrix[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    speculo::ObjectRef object(matrix);
    speculo::ObjectRef row(matrix[0]);
    int nine = 9;

    for (std::size_t r = 0; r < kIndexedOperations.size(); ++r)
    {
        for (std::size_t c = 0; c < kIndexForms.size(); ++c)
        {
            SCOPED_TRACE(std::format("{}/{}", operation_name(kIndexedOperations[r]), form_name(kIndexForms[c])));

            const Expected expected = kMultiIndexMatrix[r][c];
            int before[2][3] = {};
            for (std::size_t i = 0; i < 2; ++i)
            {
                for (std::size_t j = 0; j < 3; ++j)
                {
                    before[i][j] = matrix[i][j];
                }
            }

            const std::optional<speculo::Error> error =
                run_form(kIndexedOperations[r], object, kIndexForms[c], nine, row);

            if (expected == Expected::Ok)
            {
                ASSERT_FALSE(error.has_value());
                continue;
            }

            ASSERT_TRUE(error.has_value());
            EXPECT_EQ(error->kind, expected_kind(expected));

            // A rejected write must not have touched the array.
            if (kIndexedOperations[r] == IndexedOperation::Set)
            {
                EXPECT_TRUE(same(before, matrix));
            }
        }
    }

    EXPECT_EQ(matrix[1][2], 9); // the one successful write
}

// ─────────────────────────────────────────────────────────────────────────────
// multi-index: object form × operation (15 cells), indices {0, 1}
//
//                 get          | set          | get_ref
// mutable 2D    | Ok           | Ok           | Ok
// const 2D      | Ok           | ReadOnly     | Ok
// volatile 2D   | NotSupported | NotSupported | Ok
// scalar        | TypeMismatch | TypeMismatch | TypeMismatch
// null          | NullInstance | NullInstance | NullInstance
// ─────────────────────────────────────────────────────────────────────────────

TEST(ArrayMultiIndexMatrix, ObjectFormByOperation)
{
    int mutable_2d[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    static const int const_2d[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    volatile int volatile_2d[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    int scalar = 0;
    int nine = 9;

    constexpr std::array<std::array<Expected, 3>, 5> matrix{{
        /* mutable  */ { Expected::Ok, Expected::Ok, Expected::Ok },
        /* const    */ { Expected::Ok, Expected::ReadOnly, Expected::Ok },
        /* volatile */ { Expected::NotSupported, Expected::NotSupported, Expected::Ok },
        /* scalar   */ { Expected::TypeMismatch, Expected::TypeMismatch, Expected::TypeMismatch },
        /* null     */ { Expected::NullInstance, Expected::NullInstance, Expected::NullInstance },
    }};

    const std::array<speculo::ObjectRef, 5> objects{
        speculo::ObjectRef(mutable_2d),
        speculo::ObjectRef(static_cast<const void*>(const_2d), speculo::class_of<const int[2][3]>()),
        speculo::ObjectRef(volatile_2d),
        speculo::ObjectRef(scalar),
        speculo::ObjectRef{},
    };

    for (std::size_t r = 0; r < kObjectForms.size(); ++r)
    {
        for (std::size_t c = 0; c < kIndexedOperations.size(); ++c)
        {
            SCOPED_TRACE(std::format("{}/{}", form_name(kObjectForms[r]), operation_name(kIndexedOperations[c])));

            const std::optional<speculo::Error> error =
                run_indexed(kIndexedOperations[c], objects[r], { 0, 1 }, nine, speculo::ObjectRef{});
            const Expected expected = matrix[r][c];

            if (expected == Expected::Ok)
            {
                ASSERT_FALSE(error.has_value());
            }
            else
            {
                ASSERT_TRUE(error.has_value());
                EXPECT_EQ(error->kind, expected_kind(expected));
            }
        }
    }

    EXPECT_EQ(mutable_2d[0][1], 9); // the mutable row's successful write

    // The const row still reads the original value, and views carry the receiver's cv.
    EXPECT_EQ(speculo::Array::get(objects[1], { 0, 1 }).unwrap().as<int>().unwrap(), 2);
    EXPECT_TRUE(speculo::Array::get_ref(objects[1], { 0, 1 }).unwrap().is_const());
    EXPECT_TRUE(speculo::Array::get_ref(objects[2], { 0, 1 }).unwrap().is_volatile());
    EXPECT_FALSE(speculo::Array::get_ref(objects[0], { 0, 1 }).unwrap().is_const());
}

// ─────────────────────────────────────────────────────────────────────────────
// get_length(array, dimension): object form × dimension (6 × 4 = 24 cells)
//
//                 0              | 1              | 2               | 99
// 2D mutable    | Ok(2)          | Ok(3)          | InvalidArgument | InvalidArgument
// 2D const      | Ok(2)          | Ok(3)          | InvalidArgument | InvalidArgument
// 2D volatile   | Ok(2)          | Ok(3)          | InvalidArgument | InvalidArgument
// 1D mutable    | Ok(3)          | InvalidArgument | InvalidArgument | InvalidArgument
// scalar        | TypeMismatch   | TypeMismatch   | TypeMismatch    | TypeMismatch
// null          | NullInstance   | NullInstance   | NullInstance    | NullInstance
// ─────────────────────────────────────────────────────────────────────────────

namespace
{

enum class LengthForm { TwoDimMutable, TwoDimConst, TwoDimVolatile, OneDimMutable, Scalar, Null };

struct LengthCell
{
    Expected    expected = Expected::Ok;
    std::size_t length = 0; ///< meaningful only when expected == Ok
};

constexpr std::array<LengthForm, 6> kLengthForms{
    LengthForm::TwoDimMutable,
    LengthForm::TwoDimConst,
    LengthForm::TwoDimVolatile,
    LengthForm::OneDimMutable,
    LengthForm::Scalar,
    LengthForm::Null,
};

constexpr std::array<std::size_t, 4> kDimensions{ 0, 1, 2, 99 };

// 6 x 4 = 24 cells; columns are dimension 0 / 1 / 2 / 99.
constexpr LengthCell kLengthMatrix[6][4] = {
    /* 2D mutable  */ { { Expected::Ok, 2 }, { Expected::Ok, 3 }, { Expected::InvalidArgument, 0 },
                        { Expected::InvalidArgument, 0 } },
    /* 2D const    */ { { Expected::Ok, 2 }, { Expected::Ok, 3 }, { Expected::InvalidArgument, 0 },
                        { Expected::InvalidArgument, 0 } },
    /* 2D volatile */ { { Expected::Ok, 2 }, { Expected::Ok, 3 }, { Expected::InvalidArgument, 0 },
                        { Expected::InvalidArgument, 0 } },
    /* 1D mutable  */ { { Expected::Ok, 3 }, { Expected::InvalidArgument, 0 }, { Expected::InvalidArgument, 0 },
                        { Expected::InvalidArgument, 0 } },
    /* scalar      */ { { Expected::TypeMismatch, 0 }, { Expected::TypeMismatch, 0 },
                        { Expected::TypeMismatch, 0 }, { Expected::TypeMismatch, 0 } },
    /* null        */ { { Expected::NullInstance, 0 }, { Expected::NullInstance, 0 },
                        { Expected::NullInstance, 0 }, { Expected::NullInstance, 0 } },
};

[[nodiscard]] constexpr auto length_form_name(LengthForm form) -> std::string_view
{
    switch (form)
    {
        case LengthForm::TwoDimMutable:  return "2d-mutable";
        case LengthForm::TwoDimConst:    return "2d-const";
        case LengthForm::TwoDimVolatile: return "2d-volatile";
        case LengthForm::OneDimMutable:  return "1d-mutable";
        case LengthForm::Scalar:         return "scalar";
        case LengthForm::Null:           return "null";
    }

    return "?";
}

} // namespace

TEST(ArrayLengthMatrix, ObjectFormByDimension)
{
    int mutable_2d[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    static const int const_2d[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    volatile int volatile_2d[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    int one_dim[3] = { 1, 2, 3 };
    int scalar = 0;

    const std::array<speculo::ObjectRef, 6> objects{
        speculo::ObjectRef(mutable_2d),
        speculo::ObjectRef(static_cast<const void*>(const_2d), speculo::class_of<const int[2][3]>()),
        speculo::ObjectRef(volatile_2d),
        speculo::ObjectRef(one_dim),
        speculo::ObjectRef(scalar),
        speculo::ObjectRef{},
    };

    for (std::size_t r = 0; r < kLengthForms.size(); ++r)
    {
        for (std::size_t c = 0; c < kDimensions.size(); ++c)
        {
            SCOPED_TRACE(std::format("{}/dim {}", length_form_name(kLengthForms[r]), kDimensions[c]));

            const LengthCell cell = kLengthMatrix[r][c];
            const auto result = speculo::Array::get_length(objects[r], kDimensions[c]);

            if (cell.expected == Expected::Ok)
            {
                ASSERT_TRUE(result.is_ok());
                EXPECT_EQ(result.unwrap(), cell.length);
            }
            else
            {
                ASSERT_TRUE(result.is_err());
                EXPECT_EQ(result.unwrap_err().kind, expected_kind(cell.expected));
            }
        }
    }

    // Dimension 0 is the one-argument form, and the outermost length stays the
    // same however deep the object is.
    EXPECT_EQ(speculo::Array::get_length(objects[0], 0).unwrap(), speculo::Array::get_length(objects[0]).unwrap());
    EXPECT_EQ(speculo::Array::get_length(objects[3], 0).unwrap(), 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// multi-index single cases: values, sub-array views, error precedence
// ─────────────────────────────────────────────────────────────────────────────

TEST(ArrayTest, MultiIndexReadsAndWritesEveryDimension)
{
    int cube[2][2][2] = { { { 1, 2 }, { 3, 4 } }, { { 5, 6 }, { 7, 8 } } };
    speculo::ObjectRef object(cube);

    EXPECT_EQ(object.type().get_array_rank(), 3u);
    EXPECT_EQ(speculo::Array::get_length(object).unwrap(), 2u);
    EXPECT_EQ(speculo::Array::get_length(object, 1).unwrap(), 2u);
    EXPECT_EQ(speculo::Array::get_length(object, 2).unwrap(), 2u);

    EXPECT_EQ(speculo::Array::get(object, { 1, 0, 1 }).unwrap().as<int>().unwrap(), 6);

    int nine = 9;
    EXPECT_TRUE(speculo::Array::set(object, { 0, 1, 0 }, nine).is_ok());
    EXPECT_EQ(cube[0][1][0], 9);

    // A partial index selects a sub-array: index it again, or view it natively.
    auto plane = speculo::Array::get_ref(object, { 1 }).unwrap();
    EXPECT_EQ(plane.type(), speculo::class_of<int[2][2]>());
    auto row = speculo::Array::get_ref(plane, { 1 }).unwrap();
    EXPECT_EQ(row.type(), speculo::class_of<int[2]>());
    EXPECT_EQ(speculo::Array::get(row, { 0 }).unwrap().as<int>().unwrap(), 7);
    EXPECT_EQ(row.as<int[2]>().unwrap()[1], 8);

    // A sub-array is still a view at this depth: neither boxable nor assignable.
    EXPECT_EQ(speculo::Array::get(object, { 1 }).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(speculo::Array::set(object, { 1 }, plane).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(speculo::Array::get(object, { 1, 1, 1, 1 }).unwrap_err().kind,
              speculo::ErrorKind::InvalidArgument);
}

TEST(ArrayTest, MultiIndexMatchesChainedSingleIndexing)
{
    int matrix[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    speculo::ObjectRef object(matrix);
    auto row = speculo::Array::get_ref(object, 1).unwrap();

    EXPECT_EQ(speculo::Array::get(object, { 1, 2 }).unwrap().as<int>().unwrap(),
              speculo::Array::get(row, 2).unwrap().as<int>().unwrap());

    int nine = 9;
    EXPECT_TRUE(speculo::Array::set(object, { 1, 2 }, nine).is_ok());
    EXPECT_EQ(speculo::Array::get(row, 2).unwrap().as<int>().unwrap(), 9);

    // get_ref agrees with the single-index form at the same coordinates.
    EXPECT_EQ(speculo::Array::get_ref(object, { 1, 1 }).unwrap().as<int>().unwrap(),
              speculo::Array::get_ref(row, 1).unwrap().as<int>().unwrap());
}

TEST(ArrayTest, MultiIndexCvQualifiersFlowDownTheChain)
{
    static const int const_2d[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    volatile int volatile_2d[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    int nine = 9;

    const speculo::ObjectRef const_object(static_cast<const void*>(const_2d), speculo::class_of<const int[2][3]>());
    EXPECT_EQ(speculo::Array::get(const_object, { 1, 2 }).unwrap().as<int>().unwrap(), 6);

    auto element = speculo::Array::get_ref(const_object, { 0, 2 }).unwrap();
    EXPECT_TRUE(element.is_const());
    EXPECT_EQ(element.as<const int>().unwrap(), 3);
    EXPECT_TRUE(element.as<int>().is_none());

    auto row = speculo::Array::get_ref(const_object, { 1 }).unwrap();
    EXPECT_TRUE(row.is_const());
    EXPECT_TRUE(row.as<const int[3]>().is_some());
    EXPECT_EQ(speculo::Array::set(row, { 0 }, nine).unwrap_err().kind, speculo::ErrorKind::ReadOnly);
    EXPECT_EQ(speculo::Array::set(const_object, { 1, 0 }, nine).unwrap_err().kind, speculo::ErrorKind::ReadOnly);

    const speculo::ObjectRef volatile_object(volatile_2d);
    EXPECT_EQ(speculo::Array::get(volatile_object, { 0, 0 }).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(speculo::Array::set(volatile_object, { 0, 0 }, nine).unwrap_err().kind,
              speculo::ErrorKind::NotSupported);

    auto volatile_element = speculo::Array::get_ref(volatile_object, { 0, 0 }).unwrap();
    EXPECT_TRUE(volatile_element.is_volatile());
    EXPECT_EQ(volatile_element.as<volatile int>().unwrap(), 1);
    EXPECT_TRUE(speculo::Array::get_ref(volatile_object, { 0 }).unwrap().is_volatile());
}

TEST(ArrayTest, MultiIndexErrorsFollowTheDocumentedOrder)
{
    int matrix[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    int flat[3] = { 1, 2, 3 };
    int scalar = 0;
    speculo::ObjectRef object(matrix);
    speculo::ObjectRef flat_object(flat);
    speculo::ObjectRef scalar_object(scalar);
    speculo::ObjectRef null_object{};

    // The receiver is validated before the index list.
    EXPECT_EQ(speculo::Array::get(null_object, {}).unwrap_err().kind, speculo::ErrorKind::NullInstance);
    EXPECT_EQ(speculo::Array::get(scalar_object, {}).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_EQ(speculo::Array::get(null_object, { 0, 0, 0 }).unwrap_err().kind,
              speculo::ErrorKind::NullInstance);
    EXPECT_EQ(speculo::Array::get(scalar_object, { 0, 0, 0 }).unwrap_err().kind,
              speculo::ErrorKind::TypeMismatch);

    // Rank before range: too many indices cannot reach an element.
    EXPECT_EQ(speculo::Array::get(object, { 0, 0, 0 }).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
    EXPECT_EQ(speculo::Array::get(flat_object, { 0, 0 }).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
    EXPECT_EQ(speculo::Array::get_ref(flat_object, { 0, 0 }).unwrap_err().kind,
              speculo::ErrorKind::InvalidArgument);
    EXPECT_EQ(speculo::Array::get(flat_object, { 0 }).unwrap().as<int>().unwrap(), 1);

    // Range before capability: an out-of-range index wins over "not copyable".
    EXPECT_EQ(speculo::Array::get(object, { 0, 3 }).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
    EXPECT_EQ(speculo::Array::get(object, { 2, 0 }).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);

    // A row is a view, not a value: readable by reference, not by value.
    EXPECT_TRUE(speculo::Array::get_ref(object, { 0 }).is_ok());
    EXPECT_EQ(speculo::Array::get(object, { 0 }).unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(ArrayTest, MultiIndexSetChecksTheValueForm)
{
    int matrix[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    static const int const_2d[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    speculo::ObjectRef object(matrix);
    speculo::ObjectRef row(matrix[0]);
    int nine = 9;

    // The value is checked before the element kind: a row value reaches the
    // array check, a scalar or a null value stops at the type check.
    EXPECT_EQ(speculo::Array::set(object, { 0 }, row).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(speculo::Array::set(object, { 0 }, nine).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_EQ(speculo::Array::set(object, { 0 }, speculo::ObjectRef{}).unwrap_err().kind,
              speculo::ErrorKind::TypeMismatch);

    // A const receiver is refused before any index or value is looked at.
    const speculo::ObjectRef const_object(static_cast<const void*>(const_2d), speculo::class_of<const int[2][3]>());
    EXPECT_EQ(speculo::Array::set(const_object, { 9, 9 }, nine).unwrap_err().kind, speculo::ErrorKind::ReadOnly);
    EXPECT_EQ(speculo::Array::set(const_object, {}, nine).unwrap_err().kind, speculo::ErrorKind::ReadOnly);
}

TEST(ArrayTest, MultiIndexValueCapabilities)
{
    PinnedElement pinned[2];
    NonAssignable items[2];
    NonAssignable value;

    // Elements that cannot be copied are readable by reference only.
    EXPECT_EQ(speculo::Array::get(speculo::ObjectRef(pinned), { 0 }).unwrap_err().kind,
              speculo::ErrorKind::NotSupported);
    EXPECT_TRUE(speculo::Array::get_ref(speculo::ObjectRef(pinned), { 0 }).is_ok());

    // Elements that cannot be assigned are writable by reference only.
    EXPECT_EQ(speculo::Array::set(speculo::ObjectRef(items), { 0 }, value).unwrap_err().kind,
              speculo::ErrorKind::NotSupported);
    EXPECT_TRUE(speculo::Array::get_ref(speculo::ObjectRef(items), { 0 }).is_ok());
}

// ─────────────────────────────────────────────────────────────────────────────
// dynamic arrays of arrays
// ─────────────────────────────────────────────────────────────────────────────

TEST(ArrayTest, DynamicArrayOfArraysIsAMultiDimensionalArray)
{
    auto made = speculo::Array::new_instance(speculo::class_of<int[3]>(), 2);
    ASSERT_TRUE(made.is_ok());
    speculo::Object rows = std::move(made).unwrap();
    speculo::ObjectRef object = rows.ref();

    EXPECT_TRUE(object.type().is_array());
    EXPECT_EQ(object.type().get_name(), "int[3][]");
    EXPECT_EQ(object.type().get_qualified_name(), "int[3][]");
    EXPECT_EQ(object.type().get_array_rank(), 2u);
    EXPECT_TRUE(object.type().get_array_extent().is_none());
    EXPECT_FALSE(object.type().has_canonical_name());
    EXPECT_EQ(speculo::Array::get_element_type(object).unwrap(), speculo::class_of<int[3]>());

    // One runtime dimension over the element's static extents.
    EXPECT_EQ(speculo::Array::get_length(object).unwrap(), 2u);
    EXPECT_EQ(speculo::Array::get_length(object, 0).unwrap(), 2u);
    EXPECT_EQ(speculo::Array::get_length(object, 1).unwrap(), 3u);
    EXPECT_EQ(speculo::Array::get_length(object, 2).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);

    // A zero-length dynamic array keeps the inner extent.
    auto empty = speculo::Array::new_instance(speculo::class_of<int[3]>(), 0);
    ASSERT_TRUE(empty.is_ok());
    speculo::Object empty_array = std::move(empty).unwrap();
    EXPECT_EQ(speculo::Array::get_length(empty_array.ref()).unwrap(), 0u);
    EXPECT_EQ(speculo::Array::get_length(empty_array.ref(), 1).unwrap(), 3u);

    // The dynamic class is a display name only: it is never registered.
    speculo::register_class(object.type());
    EXPECT_TRUE(speculo::Class::for_name("int[3][]").is_none());

    // An array is not copy-constructible, so the whole dynamic array cannot be cloned.
    EXPECT_EQ(rows.clone().unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(ArrayTest, DynamicArrayOfArraysIndexesRowsAndElements)
{
    auto made = speculo::Array::new_instance(speculo::class_of<int[3]>(), 2);
    ASSERT_TRUE(made.is_ok());
    speculo::Object rows = std::move(made).unwrap();
    speculo::ObjectRef object = rows.ref();
    int nine = 9;

    EXPECT_TRUE(speculo::Array::set(object, { 1, 2 }, nine).is_ok());
    EXPECT_EQ(speculo::Array::get(object, { 1, 2 }).unwrap().as<int>().unwrap(), 9);
    EXPECT_EQ(speculo::Array::get(object, { 0, 0 }).unwrap().as<int>().unwrap(), 0); // value-initialized

    // A row is a view: index it, or see it as a native array.
    auto row = speculo::Array::get_ref(object, { 1 }).unwrap();
    EXPECT_EQ(row.type(), speculo::class_of<int[3]>());
    EXPECT_EQ(speculo::Array::get_length(row).unwrap(), 3u);
    EXPECT_EQ(row.as<int[3]>().unwrap()[2], 9);
    row.as<int[3]>().unwrap()[0] = 7;
    EXPECT_EQ(speculo::Array::get(object, { 1, 0 }).unwrap().as<int>().unwrap(), 7);

    // The row itself cannot be boxed, and the rank is the dynamic one.
    EXPECT_EQ(speculo::Array::get(object, { 1 }).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(speculo::Array::get(object, { 0, 0, 0 }).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
    EXPECT_EQ(speculo::Array::get(object, { 2, 0 }).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
}

TEST(ArrayTest, DynamicArrayOfArraysConstructsAndDestroysEveryElement)
{
    Tracked::alive = 0;
    {
        auto made = speculo::Array::new_instance(speculo::class_of<Tracked[2]>(), 3);
        ASSERT_TRUE(made.is_ok());
        speculo::Object rows = std::move(made).unwrap();
        EXPECT_EQ(Tracked::alive, 6); // 3 rows x 2 elements

        Tracked value(5);
        EXPECT_TRUE(speculo::Array::set(rows.ref(), { 2, 1 }, value).is_ok());
        EXPECT_EQ(speculo::Array::get(rows.ref(), { 2, 1 }).unwrap().as<Tracked>().unwrap().v, 5);
    }
    EXPECT_EQ(Tracked::alive, 0); // every element of every row was destroyed
}

TEST(ArrayTest, DynamicArrayOfArraysRejectsUnsupportedElementTypes)
{
    EXPECT_EQ(speculo::Array::new_instance(speculo::class_of<const int[3]>(), 2).unwrap_err().kind,
              speculo::ErrorKind::NotSupported);
    EXPECT_EQ(speculo::Array::new_instance(speculo::class_of<volatile int[3]>(), 2).unwrap_err().kind,
              speculo::ErrorKind::NotSupported);
    EXPECT_EQ(speculo::Array::new_instance(speculo::class_of<NoDefault[2]>(), 2).unwrap_err().kind,
              speculo::ErrorKind::NotSupported);
    EXPECT_EQ(speculo::Array::new_instance(speculo::class_of<int(&)[3]>(), 2).unwrap_err().kind,
              speculo::ErrorKind::NotSupported);
    EXPECT_EQ(speculo::Array::new_instance(speculo::class_of<int[]>(), 2).unwrap_err().kind,
              speculo::ErrorKind::NotSupported);
}

TEST(ArrayTest, DynamicArrayOfThreeDimensions)
{
    auto made = speculo::Array::new_instance(speculo::class_of<int[2][3]>(), 2);
    ASSERT_TRUE(made.is_ok());
    speculo::Object cube = std::move(made).unwrap();
    speculo::ObjectRef object = cube.ref();

    EXPECT_EQ(object.type().get_array_rank(), 3u);
    EXPECT_EQ(speculo::Array::get_length(object).unwrap(), 2u);
    EXPECT_EQ(speculo::Array::get_length(object, 1).unwrap(), 2u);
    EXPECT_EQ(speculo::Array::get_length(object, 2).unwrap(), 3u);
    EXPECT_EQ(speculo::Array::get_length(object, 3).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
    EXPECT_EQ(speculo::Array::get_element_type(object).unwrap(), speculo::class_of<int[2][3]>());

    int nine = 9;
    EXPECT_TRUE(speculo::Array::set(object, { 1, 0, 2 }, nine).is_ok());
    EXPECT_EQ(speculo::Array::get(object, { 1, 0, 2 }).unwrap().as<int>().unwrap(), 9);
    EXPECT_EQ(speculo::Array::get(object, { 0, 0, 0 }).unwrap().as<int>().unwrap(), 0);

    EXPECT_EQ(speculo::Array::get(object, { 1, 0 }).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(speculo::Array::get(object, { 0, 0, 0, 0 }).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
}

// ─────────────────────────────────────────────────────────────────────────────
// Array::new_instance(element, length): element form × length (6 × 3 = 18 cells)
//
// The element is validated before the length, so a rejected element type
// reports NotSupported for every length; a valid one reports InvalidArgument
// only when `Offset + length * sizeof(E)` would overflow.
//
//                  0            | typical      | overflowing
// int             | Ok           | Ok           | InvalidArgument
// int[3]          | Ok           | Ok           | InvalidArgument
// const int[3]    | NotSupported | NotSupported | NotSupported
// NoDefault[2]    | NotSupported | NotSupported | NotSupported
// int(&)[3]       | NotSupported | NotSupported | NotSupported
// int[]           | NotSupported | NotSupported | NotSupported
// ─────────────────────────────────────────────────────────────────────────────

TEST(ArrayNewInstanceMatrix, ElementFormByLength)
{
    constexpr std::size_t max = std::numeric_limits<std::size_t>::max();
    constexpr std::array<std::array<Expected, 3>, 6> expected{{
        /* int          */ { Expected::Ok, Expected::Ok, Expected::InvalidArgument },
        /* int[3]       */ { Expected::Ok, Expected::Ok, Expected::InvalidArgument },
        /* const int[3] */ { Expected::NotSupported, Expected::NotSupported, Expected::NotSupported },
        /* NoDefault[2] */ { Expected::NotSupported, Expected::NotSupported, Expected::NotSupported },
        /* int(&)[3]    */ { Expected::NotSupported, Expected::NotSupported, Expected::NotSupported },
        /* int[]        */ { Expected::NotSupported, Expected::NotSupported, Expected::NotSupported },
    }};

    const std::array<const speculo::Class*, 6> elements{
        &speculo::class_of<int>(),
        &speculo::class_of<int[3]>(),
        &speculo::class_of<const int[3]>(),
        &speculo::class_of<NoDefault[2]>(),
        &speculo::class_of<int(&)[3]>(),
        &speculo::class_of<int[]>(),
    };
    const std::array<std::size_t, 6> element_sizes{ sizeof(int), sizeof(int[3]), 0, 0, 0, 0 };
    const std::array<std::string_view, 6> names{ "int", "int[3]", "const int[3]", "NoDefault[2]", "int(&)[3]",
                                                 "int[]" };
    const std::array<std::string_view, 3> length_names{ "zero", "typical", "overflowing" };

    for (std::size_t r = 0; r < elements.size(); ++r)
    {
        for (std::size_t c = 0; c < length_names.size(); ++c)
        {
            SCOPED_TRACE(std::format("{}/{}", names[r], length_names[c]));

            const std::size_t length = c == 0 ? 0
                : c == 1 ? 3
                : (element_sizes[r] == 0 ? 1 : max / element_sizes[r] + 1);

            auto made = speculo::Array::new_instance(*elements[r], length);
            const Expected want = expected[r][c];

            if (want == Expected::Ok)
            {
                ASSERT_TRUE(made.is_ok());
                speculo::Object array = std::move(made).unwrap();
                EXPECT_EQ(speculo::Array::get_length(array.ref()).unwrap(), length);
            }
            else
            {
                ASSERT_TRUE(made.is_err());
                EXPECT_EQ(made.unwrap_err().kind, expected_kind(want));
            }
        }
    }
}

