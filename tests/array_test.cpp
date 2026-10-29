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
