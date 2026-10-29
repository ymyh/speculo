#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

TEST(ErrorTest, KindsStatusAndOutcome)
{
    speculo::Status ok = speculo::None{};
    EXPECT_TRUE(ok.is_ok());

    speculo::Status bad = speculo::Error{speculo::ErrorKind::NotFound, "missing"};
    ASSERT_TRUE(bad.is_err());
    EXPECT_EQ(bad.unwrap_err().kind, speculo::ErrorKind::NotFound);
    EXPECT_EQ(bad.unwrap_err().message, "missing");

    EXPECT_EQ(speculo::to_string(speculo::ErrorKind::TypeMismatch), "TypeMismatch");

    speculo::Outcome<int> value = 42;
    ASSERT_TRUE(value.is_ok());
    EXPECT_EQ(value.unwrap(), 42);
}
