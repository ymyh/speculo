#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

// Class::get_template_arguments / get_primary_template / get_template_argument_type,
// TemplateArgument, Function::get_template_arguments (api_design §1).
//
// TemplateArgument::kind() x accessor  —— 参数值类见 .agents/testing.md §2
// kind \ accessor | as_type | as_value | as_template | as_pack
//  Type           | Some    | None     | None        | empty
//  Value          | None    | Some     | None        | empty
//  Template       | None    | None     | Some        | empty
//  Pack (GCC 16.2) | --      | --       | --          | 永不产生（包被展平）

namespace
{

// std::vector 以外的形状：类型 + 模板模板 + 非类型实参各一个。
template<class T, template<class> class TT, int N>
struct Pack3
{
};

namespace probe
{

template<class T>
T identity(T value)
{
    return value;
}

int plain(int value)
{
    return value;
}

} // namespace probe

struct Plain
{
    int x = 0;
};

// 类模板里的嵌套类型（外层是特化，内层自己不是特化）。
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

TEST(TemplateTest, TypeArgumentsOfAStandardSpecialization)
{
    const speculo::Class& vector_int = speculo::class_of<std::vector<int>>();

    EXPECT_TRUE(vector_int.is_template_specialization());

    // S8.1: a default template argument shows up as a real argument, so there are
    // two of them (int + std::allocator<int>), not one.
    const std::span<const speculo::TemplateArgument> args = vector_int.get_template_arguments();
    ASSERT_EQ(args.size(), 2u);
    EXPECT_EQ(args[0].kind(), speculo::TemplateArgumentKind::Type);
    EXPECT_EQ(args[0].get_spelling(), "int");
    ASSERT_TRUE(args[0].as_type().is_some());
    EXPECT_EQ(args[0].as_type().unwrap(), speculo::class_of<int>());
    EXPECT_TRUE(args[0].as_value().is_none());
    EXPECT_TRUE(args[0].as_template().is_none());
    EXPECT_TRUE(args[0].as_pack().empty());

    // The allocator argument is a type argument too.
    EXPECT_EQ(args[1].kind(), speculo::TemplateArgumentKind::Type);
    EXPECT_EQ(args[1].as_type().unwrap(), speculo::class_of<std::allocator<int>>());
}

TEST(TemplateTest, PrimaryTemplateIsSharedByEverySpecialization)
{
    const speculo::Class& vector_int = speculo::class_of<std::vector<int>>();
    const speculo::Class& vector_double = speculo::class_of<std::vector<double>>();

    ASSERT_TRUE(vector_int.get_primary_template().is_some());
    const speculo::Class& primary = vector_int.get_primary_template().unwrap();

    // A declaration-level descriptor: no size, no members, kind Template.
    EXPECT_EQ(primary.kind(), speculo::TypeKind::Template);
    EXPECT_EQ(primary.get_size(), 0u);
    EXPECT_EQ(primary.get_alignment(), 0u);
    EXPECT_TRUE(primary.get_fields().empty());
    EXPECT_TRUE(primary.get_methods().empty());
    EXPECT_FALSE(primary.is_template_specialization());

    // S8.1: display_string_of(primary) is "template<class _Tp, ...> class
    // std::vector", which is not a name -- the qualified name is spliced instead.
    EXPECT_EQ(primary.get_name(), "vector");
    EXPECT_EQ(primary.get_qualified_name(), "std::vector");

    // One instance per primary template, so handles compare equal.
    EXPECT_EQ(primary, vector_double.get_primary_template().unwrap());
    EXPECT_EQ(vector_double.get_primary_template().unwrap().get_qualified_name(), "std::vector");
}

TEST(TemplateTest, NonTypeArgumentIsBoxedAsItsOwnType)
{
    const speculo::Class& array3 = speculo::class_of<std::array<int, 3>>();
    const std::span<const speculo::TemplateArgument> args = array3.get_template_arguments();
    ASSERT_EQ(args.size(), 2u);

    EXPECT_EQ(args[1].kind(), speculo::TemplateArgumentKind::Value);
    EXPECT_TRUE(args[1].as_type().is_none());
    EXPECT_TRUE(args[1].as_template().is_none());
    EXPECT_TRUE(args[1].as_value().is_some());

    speculo::Object boxed = std::move(args[1].as_value()).unwrap();

    // S8.1: the value's type is exactly what the compiler reports for the
    // argument (`std::size_t`), never a guessed/widened type.
    EXPECT_EQ(boxed.type(), speculo::class_of<std::size_t>());
    EXPECT_EQ(boxed.as<std::size_t>().unwrap(), 3u);
    EXPECT_EQ(args[1].get_spelling(), "3");
}

TEST(TemplateTest, TemplateTemplateArgument)
{
    const speculo::Class& packed = speculo::class_of<Pack3<int, std::vector, 7>>();
    const std::span<const speculo::TemplateArgument> args = packed.get_template_arguments();
    ASSERT_EQ(args.size(), 3u);

    EXPECT_EQ(args[0].as_type().unwrap(), speculo::class_of<int>());

    EXPECT_EQ(args[1].kind(), speculo::TemplateArgumentKind::Template);
    EXPECT_TRUE(args[1].as_type().is_none());
    ASSERT_TRUE(args[1].as_template().is_some());
    EXPECT_EQ(args[1].as_template().unwrap().kind(), speculo::TypeKind::Template);
    EXPECT_EQ(args[1].as_template().unwrap().get_qualified_name(), "std::vector");

    EXPECT_EQ(args[2].kind(), speculo::TemplateArgumentKind::Value);
    EXPECT_EQ(std::move(args[2].as_value()).unwrap().as<int>().unwrap(), 7);
}

TEST(TemplateTest, ParameterPacksAreFlattened)
{
    // S8.1: a pack becomes several independent arguments; Kind::Pack never appears.
    const speculo::Class& tuple2 = speculo::class_of<std::tuple<int, char>>();
    const std::span<const speculo::TemplateArgument> args = tuple2.get_template_arguments();
    ASSERT_EQ(args.size(), 2u);
    EXPECT_EQ(args[0].as_type().unwrap(), speculo::class_of<int>());
    EXPECT_EQ(args[1].as_type().unwrap(), speculo::class_of<char>());

    for (const speculo::TemplateArgument& argument : args)
    {
        EXPECT_NE(argument.kind(), speculo::TemplateArgumentKind::Pack);
        EXPECT_TRUE(argument.as_pack().empty());
    }
}

TEST(TemplateTest, ArgumentTypeLookupByIndex)
{
    const speculo::Class& array3 = speculo::class_of<std::array<int, 3>>();

    ASSERT_TRUE(array3.get_template_argument_type(0).is_some());
    EXPECT_EQ(array3.get_template_argument_type(0).unwrap(), speculo::class_of<int>());

    // Index 1 is the non-type argument: not a type argument.
    EXPECT_TRUE(array3.get_template_argument_type(1).is_none());
    // Out of range.
    EXPECT_TRUE(array3.get_template_argument_type(2).is_none());
    EXPECT_TRUE(array3.get_template_argument_type(99).is_none());
}

TEST(TemplateTest, NonSpecializationsHaveNoTemplateInformation)
{
    const speculo::Class& plain = speculo::class_of<Plain>();

    EXPECT_FALSE(plain.is_template_specialization());
    EXPECT_TRUE(plain.get_primary_template().is_none());
    EXPECT_TRUE(plain.get_template_arguments().empty());
    EXPECT_TRUE(plain.get_template_argument_type(0).is_none());

    // Pointers, arrays and references to a specialization are not specializations.
    EXPECT_FALSE(speculo::class_of<std::vector<int>*>().is_template_specialization());
    EXPECT_FALSE(speculo::class_of<std::vector<int>[2]>().is_template_specialization());
    EXPECT_FALSE(speculo::class_of<std::vector<int>&>().is_template_specialization());
    EXPECT_FALSE(speculo::class_of<int>().is_template_specialization());
}

TEST(TemplateTest, CvQualifiedSpecializationIsNotItselfASpecialization)
{
    // S9 probe: GCC reports template arguments for `const std::vector<int>` too,
    // but a cv-qualified type is not "an instance of a class template"
    // (api_design §1.2) -- the unqualified type is.
    const speculo::Class& const_vector = speculo::class_of<const std::vector<int>>();

    EXPECT_TRUE(const_vector.is_const_qualified());
    EXPECT_FALSE(const_vector.is_template_specialization());
    EXPECT_TRUE(const_vector.get_primary_template().is_none());

    ASSERT_TRUE(const_vector.get_unqualified_type().is_some());
    EXPECT_TRUE(const_vector.get_unqualified_type().unwrap().is_template_specialization());
}

TEST(TemplateTest, NestedTypeOfASpecializationIsNotASpecialization)
{
    // S8.5: Wrapper<int>::Nested has no template arguments of its own, its parent
    // is the instantiation Wrapper<int>.
    const speculo::Class& nested = speculo::class_of<Wrapper<int>::Nested>();

    EXPECT_FALSE(nested.is_template_specialization());
    EXPECT_TRUE(speculo::class_of<Wrapper<int>>().is_template_specialization());
    EXPECT_TRUE(nested.get_enclosing_class().is_some());
    EXPECT_EQ(nested.get_enclosing_class().unwrap(), speculo::class_of<Wrapper<int>>());
}

TEST(TemplateTest, FunctionTemplateSpecialization)
{
    const speculo::Function& identity = speculo::function_of<probe::identity<int>>();

    EXPECT_TRUE(identity.is_template_specialization());
    const std::span<const speculo::TemplateArgument> args = identity.get_template_arguments();
    ASSERT_EQ(args.size(), 1u);
    EXPECT_EQ(args[0].kind(), speculo::TemplateArgumentKind::Type);
    EXPECT_EQ(args[0].as_type().unwrap(), speculo::class_of<int>());
    ASSERT_TRUE(identity.get_template_argument_type(0).is_some());
    EXPECT_EQ(identity.get_template_argument_type(0).unwrap(), speculo::class_of<int>());
    EXPECT_TRUE(identity.get_template_argument_type(1).is_none());
}

TEST(TemplateTest, PlainFunctionHasNoTemplateInformation)
{
    const speculo::Function& plain = speculo::function_of<probe::plain>();

    EXPECT_FALSE(plain.is_template_specialization());
    EXPECT_TRUE(plain.get_template_arguments().empty());
    EXPECT_TRUE(plain.get_template_argument_type(0).is_none());
}

TEST(TemplateTest, EmptyHandlePanics)
{
    const speculo::TemplateArgument empty;
    EXPECT_DEATH(static_cast<void>(empty.kind()), "");
}
