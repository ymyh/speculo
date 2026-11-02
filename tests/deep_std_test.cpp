#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include <speculo/macros.h>

// SPECULO_DEEP(T) / detail::deep_materialize<T>() (api_design §14)
//
//   type \ state          | 未标注 | 已标注
//   普通用户类            | 深     | 深（标注是幂等的语义扩展）
//   std 特化              | 浅（成员表为空） | 深（字段/方法/构造可见）
//   被标注类型的成员类型  | ——     | 不受影响（不传播）
//
// 用 vector<long long> 是因为它不被其它测试物化；同一类型若在一个 TU 里
// 标注、在另一个 TU 里按浅物化调用，是 ODR 违规（见宏注释与 review.md）。

SPECULO_DEEP(std::vector<long long>);

TEST(DeepStdTest, MarkedSpecializationExposesItsMembers)
{
    const speculo::Class& cls = speculo::class_of<std::vector<long long>>();

    EXPECT_FALSE(cls.get_methods().empty());
    EXPECT_FALSE(cls.get_constructors().empty());

    const auto size = cls.get_method("size");
    ASSERT_TRUE(size.is_ok());
    EXPECT_EQ(size.unwrap().get_return_type(), speculo::class_of<std::size_t>());

    const auto push_back = cls.get_method("push_back", { &speculo::class_of<const long long&>() });
    ASSERT_TRUE(push_back.is_ok());
}

TEST(DeepStdTest, MarkingDoesNotPropagateToMemberTypes)
{
    const speculo::Class& element = speculo::class_of<long long>();
    EXPECT_TRUE(element.get_methods().empty());
    EXPECT_TRUE(element.get_declared_fields().empty());
}

TEST(DeepStdTest, UnmarkedStandardTypeStaysShallow)
{
    const speculo::Class& cls = speculo::class_of<std::vector<unsigned char>>();
    EXPECT_TRUE(cls.get_methods().empty());
    EXPECT_TRUE(cls.get_declared_fields().empty());
    EXPECT_TRUE(cls.has_canonical_name());
    EXPECT_NE(cls.get_simple_name().find("vector"), std::string_view::npos);
}

TEST(DeepStdTest, DescribeSeesTheDeepMembers)
{
    const std::string text = speculo::class_of<std::vector<long long>>().describe();
    EXPECT_NE(text.find("push_back"), std::string::npos);
    EXPECT_NE(text.find("size"), std::string::npos);
}
