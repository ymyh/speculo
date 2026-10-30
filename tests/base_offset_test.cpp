#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

// BaseClass::get_offset (api_design §6).
//
// 基类形状 × get_offset()  —— 返回 Option
//  | 基类            | Some(offset) | 说明                                   |
//  | 非虚基（第 1 个）| 0            | 派生对象起点就是它                     |
//  | 非虚基（后续）   | 真实偏移     | 与 cast_to 的地址差一致                |
//  | 空基             | 0            | EBO 生效                               |
//  | 虚基             | None         | 没有静态偏移（随完整对象变化，S8.10）  |
//  | protected/private 基 | None     | 命名空间作用域无法上转                 |

namespace
{

struct P1
{
    int p1 = 1;
};

struct P2
{
    int p2 = 2;
};

struct P3
{
    int p3 = 3;
};

struct Wide : P1, P2, P3
{
    int w = 4;
};

struct EmptyBase
{
};

struct WithEmpty : EmptyBase, P1
{
    int v = 5;
};

struct VirtualBase
{
    int vb = 6;
};

struct WithVirtual : virtual VirtualBase
{
    int v = 7;
};

struct HiddenBase
{
    int hb = 8;
};

class WithHidden : private HiddenBase
{
public:
    int v = 9;
};

struct PolyBase
{
    virtual ~PolyBase() = default;
    int pb = 10;
};

struct PolyDerived : PolyBase
{
    int pd = 11;
};

struct Again : P2
{
    int a = 12;
};

/// 两路一致性：每个可上转的基类的常量偏移 == cast_to 得到的实际地址差。
template<typename Object>
void expect_offsets_match_cast(const speculo::Class& cls, Object& object)
{
    for (const speculo::BaseClass& base : cls.get_bases())
    {
        if (base.cast == nullptr)
        {
            EXPECT_TRUE(base.get_offset().is_none()) << base.type->get_name();
            continue;
        }

        const speculo::ObjectRef view = cls.cast_to(object, *base.type).unwrap();
        const auto measured = static_cast<std::size_t>(static_cast<const std::byte*>(view.raw_data())
                                                       - reinterpret_cast<const std::byte*>(std::addressof(object)));

        if (base.is_virtual())
        {
            EXPECT_TRUE(base.get_offset().is_none()) << base.type->get_name();
        }
        else
        {
            ASSERT_TRUE(base.get_offset().is_some()) << base.type->get_name();
            EXPECT_EQ(base.get_offset().unwrap(), measured) << base.type->get_name();
        }
    }
}

} // namespace

TEST(BaseOffsetTest, NonVirtualBasesHaveStaticOffsets)
{
    const speculo::Class& wide = speculo::class_of<Wide>();
    const std::span<const speculo::BaseClass> bases = wide.get_bases();

    ASSERT_EQ(bases.size(), 3u);
    EXPECT_EQ(bases[0].type->get_name(), "P1");
    EXPECT_EQ(bases[1].type->get_name(), "P2");
    EXPECT_EQ(bases[2].type->get_name(), "P3");

    ASSERT_TRUE(bases[0].get_offset().is_some());
    EXPECT_EQ(bases[0].get_offset().unwrap(), 0u);

    ASSERT_TRUE(bases[1].get_offset().is_some());
    ASSERT_TRUE(bases[2].get_offset().is_some());
    EXPECT_LT(bases[0].get_offset().unwrap(), bases[1].get_offset().unwrap());
    EXPECT_LT(bases[1].get_offset().unwrap(), bases[2].get_offset().unwrap());
    EXPECT_GE(bases[2].get_offset().unwrap(), sizeof(P1) + sizeof(P2));
}

TEST(BaseOffsetTest, OffsetAgreesWithCastTo)
{
    Wide wide;
    expect_offsets_match_cast(speculo::class_of<Wide>(), wide);

    WithEmpty with_empty;
    expect_offsets_match_cast(speculo::class_of<WithEmpty>(), with_empty);

    PolyDerived poly;
    expect_offsets_match_cast(speculo::class_of<PolyDerived>(), poly);

    Again again;
    expect_offsets_match_cast(speculo::class_of<Again>(), again);

    WithVirtual with_virtual;
    expect_offsets_match_cast(speculo::class_of<WithVirtual>(), with_virtual);
}

TEST(BaseOffsetTest, EmptyBaseOptimizationGivesZero)
{
    const speculo::Class& cls = speculo::class_of<WithEmpty>();
    const speculo::BaseClass& empty = cls.get_bases()[0];
    const speculo::BaseClass& first = cls.get_bases()[1];

    EXPECT_EQ(empty.type->get_name(), "EmptyBase");
    ASSERT_TRUE(empty.get_offset().is_some());
    EXPECT_EQ(empty.get_offset().unwrap(), 0u);

    // EBO: the next base / member can also start at 0.
    ASSERT_TRUE(first.get_offset().is_some());
    EXPECT_EQ(first.get_offset().unwrap(), 0u);
}

TEST(BaseOffsetTest, VirtualBaseHasNoStaticOffset)
{
    const speculo::Class& cls = speculo::class_of<WithVirtual>();
    const speculo::BaseClass& base = cls.get_bases()[0];

    EXPECT_TRUE(base.is_virtual());
    EXPECT_TRUE(base.get_offset().is_none());

    // The upcast thunk still works; only the static offset is unavailable.
    WithVirtual object;
    const speculo::ObjectRef view = cls.cast_to(object, *base.type).unwrap();
    EXPECT_EQ(view.as<VirtualBase>().unwrap().vb, 6);
}

TEST(BaseOffsetTest, InaccessibleBaseHasNoOffset)
{
    const speculo::Class& cls = speculo::class_of<WithHidden>();
    const speculo::BaseClass& base = cls.get_bases()[0];

    EXPECT_TRUE(base.modifiers.is_private());
    EXPECT_TRUE(base.get_offset().is_none());
    EXPECT_EQ(base.cast, nullptr);
}

TEST(BaseOffsetTest, PolymorphicBaseOffsetsSkipTheVtablePointer)
{
    const speculo::Class& cls = speculo::class_of<PolyDerived>();
    const speculo::BaseClass& base = cls.get_bases()[0];

    ASSERT_TRUE(base.get_offset().is_some());
    // PolymorphicBase first: the vptr is shared, so the base is still at 0.
    EXPECT_EQ(base.get_offset().unwrap(), 0u);
    EXPECT_EQ(cls.get_size(), sizeof(PolyDerived));
}

TEST(BaseOffsetTest, NoBasesMeansAnEmptySpan)
{
    EXPECT_TRUE(speculo::class_of<P1>().get_bases().empty());
    EXPECT_TRUE(speculo::class_of<int>().get_bases().empty());
    EXPECT_TRUE(speculo::class_of<int*>().get_bases().empty());
}
