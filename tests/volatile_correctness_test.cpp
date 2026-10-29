#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/instance_fixtures.h"

TEST(VolatileCorrectnessTest, HandleRecordsVolatility)
{
    Register plain;
    volatile Register device;

    speculo::ObjectRef plain_ref(plain);
    speculo::ObjectRef device_ref(device);
    EXPECT_FALSE(plain_ref.is_volatile());
    EXPECT_TRUE(device_ref.is_volatile());
    EXPECT_FALSE(device_ref.is_const());

    // Same unqualified type key either way.
    EXPECT_EQ(device_ref.type(), speculo::class_of<Register>());

    // as<T>() needs the volatile qualifier to be spelled out.
    EXPECT_TRUE(device_ref.as<Register>().is_none());
    EXPECT_TRUE(device_ref.as<const Register>().is_none());
    ASSERT_TRUE(device_ref.as<volatile Register>().is_some());
    EXPECT_EQ(&device_ref.as<volatile Register>().unwrap(), &device);
    EXPECT_TRUE(plain_ref.as<Register>().is_some());

    // Qualifiers combine.
    const volatile Register cv_device{};
    speculo::ObjectRef cv_ref(cv_device);
    EXPECT_TRUE(cv_ref.is_const());
    EXPECT_TRUE(cv_ref.is_volatile());
    EXPECT_TRUE(cv_ref.as<const Register>().is_none());
    EXPECT_TRUE(cv_ref.as<const volatile Register>().is_some());
}

TEST(VolatileCorrectnessTest, MethodsRequireVolatileQualifiedOnVolatileObject)
{
    const speculo::Class& cls = speculo::class_of<Register>();
    volatile Register device;
    speculo::ObjectRef device_ref(device);

    speculo::Method read = cls.get_method("read").unwrap();
    speculo::Method peek = cls.get_method("peek").unwrap();
    speculo::Method write = cls.get_method("write").unwrap();

    EXPECT_EQ(std::move(read.invoke(device_ref, {})).unwrap().as<int>().unwrap(), 5);
    EXPECT_EQ(std::move(peek.invoke(device_ref, {})).unwrap().as<int>().unwrap(), 5);

    int nine = 9;
    auto rejected = write.invoke(device_ref, { nine });
    ASSERT_TRUE(rejected.is_err());
    EXPECT_EQ(rejected.unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(device.value, 5);

    // A volatile method is callable on a non-volatile object.
    Register plain;
    EXPECT_EQ(std::move(read.invoke(plain, {})).unwrap().as<int>().unwrap(), 5);
}

TEST(VolatileCorrectnessTest, FieldAccessOnVolatileObject)
{
    const speculo::Class& cls = speculo::class_of<Register>();
    speculo::Field value = cls.get_field("value").unwrap();
    speculo::Field status = cls.get_field("status").unwrap();

    volatile Register device;
    speculo::ObjectRef device_ref(device);
    int one = 1;

    // By-value reads and writes would drop volatile: refused.
    EXPECT_EQ(value.get(device_ref).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(value.set(device_ref, one).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(device.value, 5);

    // A view keeps the qualifier, and as<volatile T>() is the explicit access.
    auto view = value.get_ref(device_ref);
    ASSERT_TRUE(view.is_ok());
    EXPECT_TRUE(view.unwrap().is_volatile());
    EXPECT_TRUE(view.unwrap().as<int>().is_none());
    ASSERT_TRUE(view.unwrap().as<volatile int>().is_some());
    view.unwrap().as<volatile int>().unwrap() = 11;
    EXPECT_EQ(device.value, 11);

    // A member declared volatile gives a volatile view even on a plain object,
    // while its by-value get / set stay available (the thunk accesses it as volatile).
    Register plain;
    auto status_view = status.get_ref(plain);
    ASSERT_TRUE(status_view.is_ok());
    EXPECT_TRUE(status_view.unwrap().is_volatile());
    EXPECT_TRUE(status_view.unwrap().as<int>().is_none());
    EXPECT_EQ(status_view.unwrap().as<volatile int>().unwrap(), 7);
    EXPECT_TRUE(status.set(plain, one).is_ok());
    EXPECT_EQ(plain.status, 1);

    auto plain_view = value.get_ref(plain);
    ASSERT_TRUE(plain_view.is_ok());
    EXPECT_FALSE(plain_view.unwrap().is_volatile());
}

TEST(VolatileCorrectnessTest, VolatileArgumentsAndCopiesAreRefused)
{
    volatile int sample = 3;
    speculo::ObjectRef sample_ref(sample);
    EXPECT_TRUE(sample_ref.is_volatile());
    EXPECT_EQ(sample_ref.clone().unwrap_err().kind, speculo::ErrorKind::NotSupported);

    // Passing it by value (copy) to a method is refused as a type mismatch.
    const speculo::Class& cls = speculo::class_of<Register>();
    Register plain;
    EXPECT_EQ(cls.get_method("write").unwrap().invoke(plain, { sample_ref }).unwrap_err().kind,
              speculo::ErrorKind::TypeMismatch);

    // Upcasts keep the qualifier.
    volatile Register device;
    auto same = cls.cast_to(speculo::ObjectRef(device), cls);
    ASSERT_TRUE(same.is_ok());
    EXPECT_TRUE(same.unwrap().is_volatile());
}
