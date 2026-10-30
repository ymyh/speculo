#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

// Class::get_declared_classes / get_enclosing_class (api_design §4).
//
// get_declared_classes()（span）：本类声明的类 / 联合 / 枚举，按声明序
//  | 成员种类              | 列入 | 理由                                   |
//  | 嵌套类 / 联合 / 枚举  | 是   | 有完整类型，可物化                     |
//  | 类型别名              | 否   | `dealias` 后才是实体（S8.5 的分类顺序）|
//  | 别名指向嵌套类        | 否   | 同上                                   |
//  | 未实例化的成员类模板  | 否   | 不是类型                               |
//  | 前向声明的嵌套类      | 否   | 不完整，无法物化                       |
//  | 继承来的类型          | 否   | 与 get_declared_fields() 同一视图约定  |
//
// get_enclosing_class()（Option）：类内声明 -> Some；命名空间作用域 -> None

namespace
{

struct Outer
{
    struct Inner
    {
        int v = 0;
    };

    class Hidden
    {
    };

    union Choice
    {
        int i;
        float f;
    };

    enum class Color
    {
        Red,
        Green,
    };

    enum Plain
    {
        Alpha,
    };

    using Alias = int;
    using AliasToNested = Inner;

    template<class T>
    struct MemberTemplate
    {
        T t{};
    };

    struct Forward;

private:
    struct Secret
    {
    };
};

struct Derived : Outer
{
    int extra = 0;
};

struct TopLevel
{
    int v = 0;
};

template<class T>
struct Wrapper
{
    struct Nested
    {
        T t{};
    };

    T value{};
};

} // namespace

TEST(NestedTypeTest, DeclaredClassesAreClassesUnionsAndEnumsInOrder)
{
    const speculo::Class& outer = speculo::class_of<Outer>();
    const std::span<const speculo::Class* const> nested = outer.get_declared_classes();

    ASSERT_EQ(nested.size(), 6u);
    EXPECT_EQ(nested[0]->get_name(), "Inner");
    EXPECT_EQ(nested[1]->get_name(), "Hidden");
    EXPECT_EQ(nested[2]->get_name(), "Choice");
    EXPECT_EQ(nested[3]->get_name(), "Color");
    EXPECT_EQ(nested[4]->get_name(), "Plain");
    EXPECT_EQ(nested[5]->get_name(), "Secret");

    EXPECT_TRUE(nested[0]->is_class());
    EXPECT_TRUE(nested[2]->is_union());
    EXPECT_TRUE(nested[3]->is_enum());
    EXPECT_TRUE(nested[4]->is_enum());

    // Access level comes from the nested type's own modifiers.
    EXPECT_TRUE(nested[0]->modifiers().is_public());
    EXPECT_TRUE(nested[2]->modifiers().is_public());
    EXPECT_TRUE(nested[5]->modifiers().is_private());

    // Aliases, member templates, incomplete declarations and inherited names are
    // never part of the list.
    for (const speculo::Class* cls : nested)
    {
        EXPECT_NE(cls->get_name(), "Alias");
        EXPECT_NE(cls->get_name(), "AliasToNested");
        EXPECT_NE(cls->get_name(), "MemberTemplate");
        EXPECT_NE(cls->get_name(), "Forward");
        EXPECT_NE(cls->get_name(), "extra");
    }
}

TEST(NestedTypeTest, EnclosingClassAndQualifiedName)
{
    const speculo::Class& inner = speculo::class_of<Outer::Inner>();

    // S8.5: get_name() is the simple name; the qualified name is spliced from the
    // parent's display string (the fixtures live in an anonymous namespace, so the
    // full spelling carries that prefix).
    EXPECT_EQ(inner.get_name(), "Inner");
    EXPECT_TRUE(inner.get_qualified_name().ends_with("Outer::Inner"));

    ASSERT_TRUE(inner.get_enclosing_class().is_some());
    EXPECT_EQ(inner.get_enclosing_class().unwrap(), speculo::class_of<Outer>());

    // The enclosing type of an enum / union is its class too.
    EXPECT_EQ(speculo::class_of<Outer::Color>().get_enclosing_class().unwrap(), speculo::class_of<Outer>());
    EXPECT_EQ(speculo::class_of<Outer::Choice>().get_enclosing_class().unwrap(), speculo::class_of<Outer>());

    // Namespace scope has no enclosing class.
    EXPECT_TRUE(speculo::class_of<Outer>().get_enclosing_class().is_none());
    EXPECT_TRUE(speculo::class_of<TopLevel>().get_enclosing_class().is_none());
    EXPECT_TRUE(speculo::class_of<int>().get_enclosing_class().is_none());
}

TEST(NestedTypeTest, NestedTypesOfAClassTemplateInstance)
{
    // S8.5: parent_of(Wrapper<int>::Nested) is the instantiation itself.
    const speculo::Class& wrapper = speculo::class_of<Wrapper<int>>();
    const speculo::Class& nested = speculo::class_of<Wrapper<int>::Nested>();

    ASSERT_EQ(wrapper.get_declared_classes().size(), 1u);
    EXPECT_EQ(*wrapper.get_declared_classes()[0], nested);

    ASSERT_TRUE(nested.get_enclosing_class().is_some());
    EXPECT_EQ(nested.get_enclosing_class().unwrap(), wrapper);
    EXPECT_FALSE(nested.is_template_specialization());

    // A different instantiation is a different enclosing type.
    EXPECT_NE(speculo::class_of<Wrapper<double>>().get_declared_classes()[0], wrapper.get_declared_classes()[0]);
}

TEST(NestedTypeTest, SingleTypeHasNoNestedTypes)
{
    EXPECT_TRUE(speculo::class_of<TopLevel>().get_declared_classes().empty());
    EXPECT_TRUE(speculo::class_of<int>().get_declared_classes().empty());
    EXPECT_TRUE(speculo::class_of<Outer::Inner>().get_declared_classes().empty());
    EXPECT_TRUE(speculo::class_of<int*>().get_declared_classes().empty());
}
