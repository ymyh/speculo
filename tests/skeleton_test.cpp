#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

TEST(SkeletonTest, ModuleImportsAndLinks)
{
    EXPECT_EQ(speculo::version_string(), "0.1.0");
}

TEST(SkeletonTest, CrabCppDependencyLinks)
{
    crab_cpp::Option<int> value = 42;
    ASSERT_TRUE(value.is_some());
    EXPECT_EQ(value.unwrap(), 42);
}
