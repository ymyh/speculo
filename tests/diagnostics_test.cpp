#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/phase12_fixtures.h"

namespace p12_snapshot
{

/// A named-namespace type, so describe()'s first line is a stable string.
struct Point
{
    int x = 0;
    int y = 0;

    [[nodiscard]] auto sum() const -> int { return x + y; }
};

} // namespace p12_snapshot

// Class::describe() / to_string(ObjectRef) / std::formatter<Class / Field / Object>
// (api_design §16)
//
//   API                 | 典型（对象/类型） | V1 字段形态 | V3 空 | V4 volatile | V5 不可格式化
//   describe()          | 多行全文 / 快照    | 位域/引用/数组/volatile/静态都列在各自字段行 | N/A（空句柄 panic） | N/A（describe 只看类型） | 不适用
//   to_string(ObjectRef)| 逐字段             | 只列非静态字段；数组/volatile 字段 → <unprintable> | "<null>" | "<volatile>" | "<unprintable>"
//   formatter<Object>   | 同 to_string       | 同上 | "<null>" | N/A（Object 不持 volatile） | 同上
//   formatter<Class>    | 同 describe        | 同上 | N/A | N/A | 不适用
//   formatter<Field>    | "Type::name"       | 私有字段也能格式化 | N/A | N/A | 不适用
//
// 字段格式化按类型检测（std::formattable）：不可格式化的字段写 "<unprintable>"。
// describe 只列 public 成员；私有成员用 get_declared_* 查询。

TEST(DiagnosticsTest, DescribeSnapshot)
{
    const std::string expected =
        "p12_snapshot::Point\n"
        "  kind: class\n"
        "  size: 8 byte(s), alignment: 4\n"
        "  modifiers: public\n"
        "  fields:\n"
        "    int x @ 0\n"
        "    int y @ 4\n"
        "  methods:\n"
        "    int p12_snapshot::Point::sum() const";

    EXPECT_EQ(speculo::class_of<p12_snapshot::Point>().describe(), expected);
}

TEST(DiagnosticsTest, DescribeCoversEveryTypeKind)
{
    EXPECT_NE(speculo::class_of<void>().describe().find("kind: void"), std::string::npos);
    EXPECT_NE(speculo::class_of<int>().describe().find("kind: fundamental"), std::string::npos);
    EXPECT_NE(speculo::class_of<int*>().describe().find("kind: pointer"), std::string::npos);
    EXPECT_NE(speculo::class_of<int P12Aggregate::*>().describe().find("kind: member pointer"),
              std::string::npos);
    EXPECT_NE(speculo::class_of<int[3]>().describe().find("kind: array"), std::string::npos);
    EXPECT_NE(speculo::class_of<P12Enum>().describe().find("kind: enum"), std::string::npos);
    EXPECT_NE(speculo::class_of<P12Union>().describe().find("kind: union"), std::string::npos);
    EXPECT_NE(speculo::class_of<P12Aggregate>().describe().find("kind: class"), std::string::npos);
    EXPECT_NE(speculo::class_of<int(int)>().describe().find("kind: function"), std::string::npos);
    EXPECT_NE(speculo::class_of<int&>().describe().find("kind: reference"), std::string::npos);

    // The normalized spelling is shown when it differs from the compiler's.
    const std::string ll = speculo::class_of<unsigned long long>().describe();
    EXPECT_NE(ll.find("normalized: unsigned long long"), std::string::npos);
}

TEST(DiagnosticsTest, DescribeIsStable)
{
    const speculo::Class& cls = speculo::class_of<P12Aggregate>();
    const std::string text = cls.describe();

    EXPECT_EQ(text.rfind(cls.get_qualified_name(), 0), 0u);
    EXPECT_NE(text.find("\n  kind: class"), std::string::npos);
    EXPECT_NE(text.find("\n  size: 8 byte(s), alignment: 4"), std::string::npos);
    EXPECT_NE(text.find("\n  modifiers: public"), std::string::npos);
    EXPECT_NE(text.find("int a @ 0"), std::string::npos);
    EXPECT_NE(text.find("int b @ 4"), std::string::npos);
    EXPECT_NE(text.find("\n  methods: (none)"), std::string::npos);
}

TEST(DiagnosticsTest, DescribeFundamentalHasNoMembers)
{
    const std::string text = speculo::class_of<int>().describe();
    EXPECT_NE(text.find("kind: fundamental"), std::string::npos);
    EXPECT_NE(text.find("fields: (none)"), std::string::npos);
    EXPECT_NE(text.find("methods: (none)"), std::string::npos);
}

TEST(DiagnosticsTest, ToStringFormatsEveryPrintableField)
{
    P12Printable value;
    value.number = 7;
    value.ratio = 1.5;
    value.text = "hello";
    value.opaque.v = 3;

    const std::string text = speculo::to_string(value);
    const std::string expected = std::format(
        "{} {{ number = 7, ratio = 1.5, text = hello, opaque = <unprintable> }}",
        speculo::class_of<P12Printable>().get_normalized_name());
    EXPECT_EQ(text, expected);
}

TEST(DiagnosticsTest, ToStringSkipsStaticAndMarksUnformattableShapes)
{
    P12Fields value;

    const std::string expected = std::format(
        "{} {{ value = 5, ref = 5, bits = 2, arr = <unprintable>, vol = <unprintable> }}",
        speculo::class_of<P12Fields>().get_normalized_name());
    EXPECT_EQ(speculo::to_string(value), expected);

    // The static member is not part of an instance description.
    EXPECT_EQ(speculo::to_string(value).find("counter"), std::string::npos);
}

TEST(DiagnosticsTest, DescribeOnlyListsPublicMembers)
{
    const speculo::Class& cls = speculo::class_of<P12Private>();
    const std::string text = cls.describe();

    EXPECT_NE(text.find("exposed"), std::string::npos);
    EXPECT_EQ(text.find("hidden"), std::string::npos);
    EXPECT_EQ(text.find("guarded"), std::string::npos);
}

TEST(DiagnosticsTest, FormatterNamesDeclaredFields)
{
    const speculo::Field hidden = speculo::class_of<P12Private>().get_declared_field("hidden").unwrap();
    EXPECT_EQ(std::format("{}", hidden),
              std::format("{}.hidden", speculo::class_of<P12Private>().get_qualified_name()));
}

TEST(DiagnosticsTest, ToStringRefusesVolatileViews)
{
    volatile P12Counter counter(1);
    EXPECT_EQ(speculo::to_string(counter), "<volatile>");
}

TEST(DiagnosticsTest, ToStringOnEmptyAndShallowValues)
{
    EXPECT_EQ(speculo::to_string(speculo::ObjectRef{}), "<null>");

    // A shallow standard type has no fields, so only the header is printed.
    std::string text("x");
    EXPECT_EQ(speculo::to_string(text), "std::string {}");
}

TEST(DiagnosticsTest, FormattersDelegate)
{
    const speculo::Class& cls = speculo::class_of<P12Aggregate>();
    EXPECT_EQ(std::format("{}", cls), cls.describe());

    const speculo::Field field = cls.get_field("a").unwrap();
    EXPECT_EQ(std::format("{}", field),
              std::format("{}.a", cls.get_qualified_name()));

    P12Aggregate aggregate{ 1, 2 };
    EXPECT_EQ(std::format("{}", speculo::Object::from_value(aggregate).unwrap()),
              speculo::to_string(aggregate));

    const speculo::Object empty;
    EXPECT_EQ(std::format("{}", empty), "<null>");
}
