#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/class_fixtures.h"

TEST(DeclaredViewTest, PrivateAndProtectedMembers)
{
    const auto& cls = speculo::class_of<Secret>();

    // Public view only exposes public members.
    EXPECT_EQ(cls.get_fields().size(), 1u);
    EXPECT_EQ(cls.get_methods().size(), 1u);
    EXPECT_TRUE(cls.get_field("hidden").is_none());
    EXPECT_TRUE(cls.get_field("guarded").is_none());
    EXPECT_TRUE(cls.get_method("secret_method").is_err());

    // Declared view exposes private/protected members too.
    ASSERT_TRUE(cls.get_declared_field("hidden").is_some());
    ASSERT_TRUE(cls.get_declared_field("guarded").is_some());
    EXPECT_EQ(cls.get_declared_fields().size(), 3u);
    ASSERT_TRUE(cls.get_declared_method("secret_method").is_ok());

    Secret s;
    speculo::Field hidden = cls.get_declared_field("hidden").unwrap();
    EXPECT_TRUE(hidden.modifiers().is_private());
    EXPECT_EQ(hidden.get(s).unwrap().as<int>().unwrap(), 3);

    int five = 5;
    ASSERT_TRUE(hidden.set(s, five).is_ok());
    EXPECT_EQ(s.read_secret(), 7); // hidden(5) + guarded(2)

    // Invoking a private member function through the declared view works.
    speculo::Method secret = cls.get_declared_method("secret_method").unwrap();
    EXPECT_EQ(secret.modifiers().is_private(), true);
    EXPECT_EQ(secret.invoke(s, {}).unwrap().as<int>().unwrap(), 5);

    // The public method sees the new value too.
    speculo::Method read = cls.get_method("read_secret").unwrap();
    EXPECT_EQ(read.invoke(s, {}).unwrap().as<int>().unwrap(), 7);
}
