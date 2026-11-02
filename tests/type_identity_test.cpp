#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/type_fixtures.h"

TEST(TypeIdentityTest, ExactTypesAreDistinct)
{
    EXPECT_NE(speculo::class_of<int>(), speculo::class_of<const int>());
    EXPECT_NE(speculo::class_of<int>(), speculo::class_of<int&>());
    EXPECT_NE(speculo::class_of<int&>(), speculo::class_of<int&&>());
    EXPECT_TRUE(speculo::class_of<const int>().is_fundamental());
    EXPECT_TRUE(speculo::class_of<int&>().is_reference());
    EXPECT_EQ(speculo::class_of<int&>().kind(), speculo::TypeKind::Reference);
}

TEST(TypeIdentityTest, PointerDecomposition)
{
    EXPECT_TRUE(speculo::class_of<int*>().is_pointer());
    EXPECT_EQ(speculo::class_of<int*>().get_pointee_type().unwrap(), speculo::class_of<int>());
    EXPECT_EQ(speculo::class_of<const int*>().get_pointee_type().unwrap(), speculo::class_of<const int>());
    EXPECT_EQ(speculo::class_of<int**>().get_pointee_type().unwrap(), speculo::class_of<int*>());
    EXPECT_TRUE(speculo::class_of<int>().get_pointee_type().is_none());

    EXPECT_TRUE(speculo::class_of<int Prop::*>().is_member_pointer());
    EXPECT_EQ(speculo::class_of<int Prop::*>().get_pointee_class_type().unwrap(), speculo::class_of<Prop>());
    EXPECT_TRUE(speculo::class_of<int*>().get_pointee_class_type().is_none());
}

TEST(TypeIdentityTest, CvAndReferenceDecomposition)
{
    EXPECT_TRUE(speculo::class_of<const int>().is_const_qualified());
    EXPECT_FALSE(speculo::class_of<int>().is_const_qualified());
    EXPECT_EQ(speculo::class_of<const int>().get_unqualified_type().unwrap(), speculo::class_of<int>());
    EXPECT_TRUE(speculo::class_of<int>().get_unqualified_type().is_none());

    EXPECT_TRUE(speculo::class_of<volatile int>().is_volatile_qualified());

    EXPECT_EQ(speculo::class_of<int&>().get_reference_kind(), speculo::RefQualifier::Lvalue);
    EXPECT_EQ(speculo::class_of<int&&>().get_reference_kind(), speculo::RefQualifier::Rvalue);
    EXPECT_EQ(speculo::class_of<int>().get_reference_kind(), speculo::RefQualifier::None);
    EXPECT_EQ(speculo::class_of<int&>().get_referenced_type().unwrap(), speculo::class_of<int>());
    EXPECT_EQ(speculo::class_of<const int&>().get_referenced_type().unwrap(), speculo::class_of<const int>());
}

TEST(TypeIdentityTest, ArrayRankAndExtent)
{
    EXPECT_TRUE(speculo::class_of<int[4]>().is_array());
    EXPECT_EQ(speculo::class_of<int[4]>().get_array_extent().unwrap(), 4u);
    EXPECT_EQ(speculo::class_of<int[4]>().get_array_rank(), 1u);
    EXPECT_EQ(speculo::class_of<int[4]>().get_element_type().unwrap(), speculo::class_of<int>());
    EXPECT_EQ(speculo::class_of<int[4]>().get_size(), sizeof(int[4]));

    EXPECT_EQ(speculo::class_of<int[3][5]>().get_array_rank(), 2u);
    EXPECT_EQ(speculo::class_of<int[3][5]>().get_element_type().unwrap(), speculo::class_of<int[5]>());
    EXPECT_EQ(speculo::class_of<int[3][5]>().get_element_type().unwrap().get_array_extent().unwrap(), 5u);

    EXPECT_EQ(speculo::class_of<int[]>().get_array_extent().unwrap(), 0u);
    EXPECT_TRUE(speculo::class_of<int>().get_element_type().is_none());
}

// ── Phase 12: integer type identity and std::hash (api_design §13) ──
//
//   API                         | 同类型 | 不同类型 | 空句柄 | 非规范名类型
//   get_type_id()               | 相等   | 不等     | 0      | 由名字派生、进程内稳定
//   operator==                  | true   | false    | true（两个空句柄） | 仅指针相等
//   std::hash<Class>            | 与 id 一致 | ——   | 0      | 与 id 一致
//
// 第三个等价分支（两份 DSO 里的同名描述符）无法在单进程单 TU 里真实构造；
// 见 testing.md §9「跨 DSO 身份」，这里只覆盖 id / hash 契约。

TEST(TypeIdentityTest, TypeIdIsStablePerName)
{
    const speculo::Class& root = speculo::class_of<Root>();
    EXPECT_EQ(root.get_type_id(), speculo::class_of<Root>().get_type_id());
    EXPECT_NE(root.get_type_id(), speculo::class_of<Other>().get_type_id());
    EXPECT_EQ(root.get_type_id(), speculo::class_of<Root>().get_type_id()); // repeat: cached

    // cv / reference are distinct types, so their ids differ as well.
    EXPECT_NE(speculo::class_of<int>().get_type_id(), speculo::class_of<const int>().get_type_id());
    EXPECT_NE(speculo::class_of<int&>().get_type_id(), speculo::class_of<int>().get_type_id());
}

TEST(TypeIdentityTest, HashAgreesWithEquality)
{
    const std::hash<speculo::Class> hash;
    EXPECT_EQ(hash(speculo::class_of<Root>()), hash(speculo::class_of<Root>()));
    EXPECT_EQ(hash(speculo::class_of<Root>()), static_cast<std::size_t>(speculo::class_of<Root>().get_type_id()));

    std::unordered_set<speculo::Class> types;
    types.insert(speculo::class_of<Root>());
    types.insert(speculo::class_of<Root>());
    types.insert(speculo::class_of<Other>());
    EXPECT_EQ(types.size(), 2u);

    std::unordered_map<speculo::Class, int> by_type;
    by_type[speculo::class_of<Root>()] = 1;
    EXPECT_EQ(by_type.at(speculo::class_of<Root>()), 1);
}

TEST(TypeIdentityTest, NonCanonicalAndEmptyHandlesHaveIds)
{
    const speculo::Class empty;
    EXPECT_EQ(empty.get_type_id(), 0u);
    EXPECT_EQ(std::hash<speculo::Class>{}(empty), 0u);
    EXPECT_EQ(empty, speculo::Class{});
    EXPECT_FALSE(empty != speculo::Class{});

    // Dynamic arrays have no canonical name: the id comes from the display name
    // and stays stable for every object of that element type.
    speculo::Object first = speculo::Array::new_instance(speculo::class_of<int>(), 2).unwrap();
    speculo::Object second = speculo::Array::new_instance(speculo::class_of<int>(), 5).unwrap();
    EXPECT_FALSE(first.type().has_canonical_name());
    EXPECT_EQ(first.type().get_type_id(), second.type().get_type_id());
    EXPECT_EQ(std::hash<speculo::Class>{}(first.type()), std::hash<speculo::Class>{}(second.type()));

    // Different non-canonical types only compare by descriptor pointer.
    speculo::Object doubles = speculo::Array::new_instance(speculo::class_of<double>(), 2).unwrap();
    EXPECT_NE(first.type(), doubles.type());
    EXPECT_NE(first.type().get_type_id(), doubles.type().get_type_id());
}
