#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

// Storage duration, static addresses, conversion operators and variadic methods
// (api_design §9).
//
// 矩阵 1：Field 存储期查询 —— 5 个成员 × 2 个查询，FieldStorageMatrix 全断言
//  成员        | is_thread_local | get_static_address
//  ------------+-----------------+-------------------
//  static      | false           | Some(&member)
//  thread_local| true            | Some(&member)（当前线程）
//  static constexpr | false      | Some
//  [[no_unique_address]] 实例 | false | None
//  instance    | false           | None
//  不存在的名字| get_field → None（Option 的 None 路径）
//  空 Field 句柄 → panic（EmptyHandlesPanic）
//
// 矩阵 2：Method 转换 / 可变参数查询
//  方法              | is_conversion | get_conversion_type | is_variadic
//  ------------------+---------------+---------------------+-------------
//  operator int      | true          | Some(int)           | false
//  operator Celsius  | true          | Some(Celsius)       | false
//  普通方法          | false         | None                | false
//  C 可变参数方法    | false         | None                | true
//  空 Method 句柄 → panic（EmptyHandlesPanic）
//  speculo::Modifier::NO_UNIQUE_ADDRESS 在 GCC 16.2 不可检测，永不置位（api_design §9）。

namespace
{

struct Celsius
{
    int degrees = 0;
};

struct Convertible
{
    int n = 5;

    operator int() const { return n; }

    operator Celsius() const { return Celsius{ n }; }

    int sum(int first, ...) const { return first; }

    int plain() const { return n; }
};

struct Storage
{
    static int plain;
    static thread_local int per_thread;
    static constexpr int constant = 5;

    [[no_unique_address]] int no_unique = 0;
    int instance = 1;
};

int Storage::plain = 1;
thread_local int Storage::per_thread = 2;

TEST(StorageTest, ThreadLocalAndStaticAddresses)
{
    const speculo::Class& cls = speculo::class_of<Storage>();

    const speculo::Field plain = cls.get_field("plain").unwrap();
    EXPECT_TRUE(plain.is_static());
    EXPECT_FALSE(plain.is_thread_local());
    EXPECT_FALSE(plain.modifiers().is_thread_local());
    ASSERT_TRUE(plain.get_static_address().is_some());
    EXPECT_EQ(plain.get_static_address().unwrap(), static_cast<const void*>(&Storage::plain));

    const speculo::Field per_thread = cls.get_field("per_thread").unwrap();
    EXPECT_TRUE(per_thread.is_static());
    EXPECT_TRUE(per_thread.is_thread_local());
    EXPECT_TRUE(per_thread.modifiers().is_thread_local());
    EXPECT_TRUE(per_thread.modifiers().bits() & speculo::Modifier::THREAD_LOCAL);
    ASSERT_TRUE(per_thread.get_static_address().is_some());
    EXPECT_EQ(per_thread.get_static_address().unwrap(), static_cast<const void*>(&Storage::per_thread));

    const speculo::Field constant = cls.get_field("constant").unwrap();
    EXPECT_TRUE(constant.is_static());
    EXPECT_FALSE(constant.is_thread_local());
    EXPECT_TRUE(constant.get_static_address().is_some());

    const speculo::Field instance = cls.get_field("instance").unwrap();
    EXPECT_FALSE(instance.is_static());
    EXPECT_FALSE(instance.is_thread_local());
    EXPECT_TRUE(instance.get_static_address().is_none());

    const speculo::Field no_unique = cls.get_field("no_unique").unwrap();
    EXPECT_EQ(no_unique.modifiers().bits() & speculo::Modifier::NO_UNIQUE_ADDRESS, 0u);
}

TEST(StorageTest, StaticAddressReflectsWritesAndStaysReadOnly)
{
    const speculo::Field plain = speculo::class_of<Storage>().get_field("plain").unwrap();
    Storage::plain = 17;

    EXPECT_EQ(*static_cast<const int*>(plain.get_static_address().unwrap()), 17);

    // The public API only exposes const void*; writing still goes through set().
    const speculo::Status written = plain.set(speculo::ObjectRef{}, speculo::ObjectRef(Storage::plain));
    ASSERT_TRUE(written.is_ok());
    EXPECT_EQ(Storage::plain, 17);
}

TEST(StorageTest, ConversionOperatorsAreIdentifiedAndInvokable)
{
    Convertible object;
    const speculo::Class& cls = speculo::class_of<Convertible>();

    bool saw_int = false;
    bool saw_celsius = false;
    for (const speculo::Method& method : cls.get_methods())
    {
        if (!method.is_conversion_operator())
        {
            continue;
        }

        ASSERT_TRUE(method.get_conversion_type().is_some());
        if (method.get_conversion_type().unwrap() == speculo::class_of<int>())
        {
            saw_int = true;
            EXPECT_TRUE(method.modifiers().is_conversion());
            EXPECT_EQ(method.invoke(object).unwrap().as<int>().unwrap(), 5);
        }
        else if (method.get_conversion_type().unwrap() == speculo::class_of<Celsius>())
        {
            saw_celsius = true;
            EXPECT_EQ(method.invoke(object).unwrap().as<Celsius>().unwrap().degrees, 5);
        }
    }

    EXPECT_TRUE(saw_int);
    EXPECT_TRUE(saw_celsius);

    const speculo::Method plain = cls.get_method<>("plain").unwrap();
    EXPECT_FALSE(plain.is_conversion_operator());
    EXPECT_TRUE(plain.get_conversion_type().is_none());
}

TEST(StorageTest, VariadicMethodIsListedButNotInvokable)
{
    const speculo::Method method = speculo::class_of<Convertible>().get_method<int>("sum").unwrap();

    EXPECT_TRUE(method.is_variadic());
    EXPECT_TRUE(method.modifiers().is_variadic());
    EXPECT_FALSE(method.is_invokable());
    EXPECT_EQ(method.get_parameter_count(), 1);

    Convertible object;
    int         first = 4;
    const auto  result = method.invoke(object, { first });
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(StorageTest, FieldStorageMatrix)
{
    struct Row
    {
        std::string_view name;
        bool             threaded;
        bool             has_address;
    };

    constexpr std::array<Row, 5> kRows{
        Row{ "plain", false, true },
        Row{ "per_thread", true, true },
        Row{ "constant", false, true },
        Row{ "no_unique", false, false },
        Row{ "instance", false, false },
    };

    const speculo::Class& cls = speculo::class_of<Storage>();
    for (const Row& row : kRows)
    {
        SCOPED_TRACE(std::string(row.name));
        const speculo::Field field = cls.get_field(row.name).unwrap();
        EXPECT_EQ(field.is_thread_local(), row.threaded);
        EXPECT_EQ(field.modifiers().is_thread_local(), row.threaded);
        EXPECT_EQ(field.get_static_address().is_some(), row.has_address);
    }

    EXPECT_TRUE(cls.get_field("missing").is_none());
}

TEST(StorageTest, EmptyHandlesPanic)
{
    EXPECT_DEATH(speculo::Field{}.is_thread_local(), "");
    EXPECT_DEATH(speculo::Field{}.get_static_address(), "");
    EXPECT_DEATH(speculo::Method{}.is_conversion_operator(), "");
    EXPECT_DEATH(speculo::Method{}.get_conversion_type(), "");
    EXPECT_DEATH(speculo::Method{}.is_variadic(), "");
}

} // namespace
