#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

// BaseClass::get_offset (api_design §6).
//
// The API has no parameters, so the single value-class axis is the shape of the
// `BaseClass` value it is called on (testing.md §2). One row per value class; the
// product is 6 cells, all asserted, no second dimension.
//
//  | 值类 | 基类形态                      | get_offset()          | 断言位置 |
//  |------|-------------------------------|-----------------------|----------|
//  | V0   | 非虚 public 基（第一个）      | Some(0)               | NonVirtualBasesHaveStaticOffsets / OffsetAgreesWithCastTo |
//  | V0   | 非虚 public 基（后续）        | Some(真实偏移)         | 同上（与 cast_to 地址差一致） |
//  | V1   | 偏移 0 的边界（第一个基、EBO）| Some(0)               | EmptyBaseOptimizationGivesZero |
//  | V2   | 类型不符                      | N/A：没有实参，也没有第二个操作数 | — |
//  | V3   | 默认构造的 BaseClass{}        | None（offset thunk 为 nullptr） | DefaultConstructedBaseClassHasNoOffset |
//  | V4   | 视图修饰                      | N/A：BaseClass 不接收 ObjectRef，没有 const/volatile 视图 | — |
//  | V5   | 虚基                          | None                  | VirtualBaseHasNoStaticOffset |
//  | V5   | protected / private 基        | None                  | InaccessibleBaseHasNoOffset |
//
// Option 的三条 None 路径各有一条用例（虚基 / 不可访问基 / 默认构造）。
// bool 维度：is_virtual() 与 modifiers.is_private() 的 true、false 两侧都有断言。
// 注意 BaseClass 是聚合值而不是句柄，所以 V3 是 None 而不是 panic（api_design §6）。

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

    // bool false side: a public, non-virtual base reports neither bit.
    EXPECT_FALSE(bases[0].modifiers.is_private());
    EXPECT_FALSE(bases[0].modifiers.is_protected());
    EXPECT_TRUE(bases[0].modifiers.is_public());
    EXPECT_FALSE(bases[0].is_virtual());

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
    EXPECT_FALSE(base.modifiers.is_private());
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
    EXPECT_FALSE(base.modifiers.is_public());
    EXPECT_FALSE(base.is_virtual());
    EXPECT_TRUE(base.get_offset().is_none());
    EXPECT_EQ(base.cast, nullptr);
}

TEST(BaseOffsetTest, DefaultConstructedBaseClassHasNoOffset)
{
    // BaseClass is a plain aggregate, not a handle into the descriptor table, so
    // "no base relationship" is expressed by the null thunk: get_offset() is None
    // and no read through it panics (api_design §6).
    const speculo::BaseClass none{};
    EXPECT_EQ(none.type, nullptr);
    EXPECT_EQ(none.cast, nullptr);
    EXPECT_TRUE(none.get_offset().is_none());
    EXPECT_TRUE(none.get_annotations().empty());
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
