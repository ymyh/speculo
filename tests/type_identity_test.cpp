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
