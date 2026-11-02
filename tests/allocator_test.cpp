#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/phase12_fixtures.h"

// Class::new_instance_with / new_aggregate_with(Allocator&, Args)
// Object::set_default_allocator / get_default_allocator
// ArenaAllocator
//   —— 参数值类见 .agents/testing.md §2；组合见 §3。
//
// allocator \ ctor | 匹配 | 无匹配 | 有歧义 | 已删除 | 抽象类
//  正常             | Ok   | NotFound | Ambiguous | NotSupported | NotSupported
//  拒绝分配         | NotSupported（构造匹配后才发现分配失败；其余格子与「正常」相同）
//  —— 构造选择发生在分配之前，所以后四列不消耗分配次数。
//
// 分配计数（§9）：成功时每个 Object 恰好 1 次 allocate + 1 次 deallocate；
// 构造 / 槽位校验失败时 allocate 过的块必须被归还且不调用析构。
// 默认分配器覆盖构造（new_instance / new_aggregate / from_value / make）与
// invoke；clone / Field::get / Callable::invoke 仍走自带 new（api_design §12.2）。

namespace
{

class CountingAllocator final : public speculo::Allocator
{
public:
    std::size_t allocations = 0;
    std::size_t deallocations = 0;
    std::size_t total_bytes = 0;
    std::size_t last_alignment = 0;

    [[nodiscard]] auto allocate(std::size_t size, std::size_t alignment) -> void* override
    {
        ++allocations;
        total_bytes += size;
        last_alignment = alignment;

        void* block = alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__
            ? ::operator new(size, std::align_val_t(alignment))
            : ::operator new(size);
        m_alignments.emplace(block, alignment);

        return block;
    }

    void deallocate(void* block) noexcept override
    {
        ++deallocations;

        const std::size_t alignment = m_alignments[block];
        m_alignments.erase(block);
        if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
        {
            ::operator delete(block, std::align_val_t(alignment));
        }
        else
        {
            ::operator delete(block);
        }
    }

private:
    std::unordered_map<void*, std::size_t> m_alignments;
};

class RefusingAllocator final : public speculo::Allocator
{
public:
    std::size_t allocations = 0;
    std::size_t deallocations = 0;

    [[nodiscard]] auto allocate(std::size_t, std::size_t) -> void* override
    {
        ++allocations;
        return nullptr;
    }

    void deallocate(void*) noexcept override { ++deallocations; }
};

class AllocatorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        speculo::Object::set_default_allocator(nullptr);
        P12Counter::live = 0;
    }

    void TearDown() override
    {
        speculo::Object::set_default_allocator(nullptr);
        P12Counter::live = 0;
    }
};

} // namespace

TEST_F(AllocatorTest, NewInstanceWithAllocatesAndDestroys)
{
    const speculo::Class& cls = speculo::class_of<P12Counter>();
    CountingAllocator allocator;
    int value = 12;

    {
        speculo::Outcome<speculo::Object> created = cls.new_instance_with(allocator, { value });
        ASSERT_TRUE(created.is_ok());
        EXPECT_EQ(allocator.allocations, 1u);
        EXPECT_EQ(allocator.deallocations, 0u);
        EXPECT_EQ(P12Counter::live, 1);

        speculo::Object object = std::move(created).unwrap();
        EXPECT_EQ(object.as<P12Counter>().unwrap().value, 12);
        EXPECT_GE(allocator.last_alignment, alignof(P12Counter));
    }

    EXPECT_EQ(allocator.deallocations, 1u);
    EXPECT_EQ(P12Counter::live, 0);
}

TEST_F(AllocatorTest, NewInstanceWithDefaultConstructor)
{
    const speculo::Class& cls = speculo::class_of<P12Counter>();
    CountingAllocator allocator;

    {
        speculo::Outcome<speculo::Object> created = cls.new_instance_with(allocator);
        ASSERT_TRUE(created.is_ok());
        EXPECT_EQ(P12Counter::live, 1);
    }

    EXPECT_EQ(allocator.allocations, 1u);
    EXPECT_EQ(allocator.deallocations, 1u);
    EXPECT_EQ(P12Counter::live, 0);
}

TEST_F(AllocatorTest, RefusingAllocatorIsNotSupportedAndLeaksNothing)
{
    const speculo::Class& cls = speculo::class_of<P12Counter>();
    RefusingAllocator allocator;

    const speculo::Outcome<speculo::Object> created = cls.new_instance_with(allocator);
    ASSERT_TRUE(created.is_err());
    EXPECT_EQ(created.unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(P12Counter::live, 0);
}

TEST_F(AllocatorTest, NewAggregateWithAllocatesAndDestroys)
{
    const speculo::Class& cls = speculo::class_of<P12Aggregate>();
    CountingAllocator allocator;
    int a = 1;
    int b = 2;

    {
        speculo::Outcome<speculo::Object> created = cls.new_aggregate_with(allocator, { a, b });
        ASSERT_TRUE(created.is_ok());

        speculo::Object object = std::move(created).unwrap();
        const P12Aggregate& aggregate = object.as<P12Aggregate>().unwrap();
        EXPECT_EQ(aggregate.a, 1);
        EXPECT_EQ(aggregate.b, 2);
    }

    EXPECT_EQ(allocator.allocations, 1u);
    EXPECT_EQ(allocator.deallocations, 1u);
}

TEST_F(AllocatorTest, SelectorErrorsAreReportedBeforeAllocating)
{
    const speculo::Class& cls = speculo::class_of<P12Counter>();
    CountingAllocator allocator;
    double wrong = 1.5;

    EXPECT_EQ(cls.new_instance_with(allocator, { wrong }).unwrap_err().kind, speculo::ErrorKind::NotFound);
    EXPECT_EQ(allocator.allocations, 0u);
}

TEST_F(AllocatorTest, OverAlignedStorageRespectsAlignment)
{
    const speculo::Class& cls = speculo::class_of<P12OverAligned>();
    CountingAllocator allocator;

    speculo::Outcome<speculo::Object> created = cls.new_instance_with(allocator);
    ASSERT_TRUE(created.is_ok());
    EXPECT_EQ(allocator.last_alignment, alignof(P12OverAligned));

    speculo::Object object = std::move(created).unwrap();
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(object.raw_data());
    EXPECT_EQ(address % alignof(P12OverAligned), 0u);
}

TEST_F(AllocatorTest, ArenaFreesEverythingAtOnce)
{
    speculo::ArenaAllocator arena(256);
    const speculo::Class& cls = speculo::class_of<P12Counter>();
    int value = 1;

    {
        auto first = cls.new_instance_with(arena, { value });
        auto second = cls.new_instance_with(arena, { value });
        auto third = speculo::class_of<P12Aggregate>().new_aggregate_with(arena, { value, value });
        ASSERT_TRUE(first.is_ok());
        ASSERT_TRUE(second.is_ok());
        ASSERT_TRUE(third.is_ok());
        EXPECT_EQ(P12Counter::live, 2);
        EXPECT_GE(arena.get_chunk_count(), 1u);
    }

    EXPECT_EQ(P12Counter::live, 0);
    arena.reset();
    EXPECT_EQ(arena.get_chunk_count(), 0u);
}

TEST_F(AllocatorTest, RefusingDefaultAllocatorReportsFailure)
{
    const speculo::Class& cls = speculo::class_of<P12Counter>();
    RefusingAllocator allocator;
    speculo::Object::set_default_allocator(&allocator);

    EXPECT_EQ(cls.new_instance().unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(speculo::Object::from_value(P12Counter(1)).unwrap_err().kind, speculo::ErrorKind::NotSupported);

    const speculo::Method get = cls.get_method("get").unwrap();
    {
        P12Counter counter;
        EXPECT_EQ(get.invoke(counter).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    }

    int a = 1;
    int b = 2;
    EXPECT_EQ(speculo::function_of<&p12_add>().invoke({ a, b }).unwrap_err().kind,
              speculo::ErrorKind::NotSupported);

    EXPECT_EQ(P12Counter::live, 0);
}

TEST_F(AllocatorTest, AllocationByConstructorMatrix)
{
    enum class AllocatorKind
    {
        Normal,
        Refusing,
    };

    enum class CtorKind
    {
        Match,
        Ambiguous,
        Deleted,
        Abstract,
        NoMatch,
    };

    constexpr std::array<AllocatorKind, 2> allocators{ AllocatorKind::Normal, AllocatorKind::Refusing };
    constexpr std::array<CtorKind, 5> ctors{ CtorKind::Match, CtorKind::Ambiguous, CtorKind::Deleted,
                                            CtorKind::Abstract, CtorKind::NoMatch };

    for (const AllocatorKind allocator_kind : allocators)
    {
        for (const CtorKind ctor_kind : ctors)
        {
            SCOPED_TRACE(std::format("allocator {} / ctor {}",
                                     static_cast<int>(allocator_kind), static_cast<int>(ctor_kind)));

            CountingAllocator normal;
            RefusingAllocator refusing;
            speculo::Allocator& allocator = allocator_kind == AllocatorKind::Normal
                ? static_cast<speculo::Allocator&>(normal)
                : static_cast<speculo::Allocator&>(refusing);

            const speculo::Class* cls = nullptr;
            int argument = 1;
            double wrong = 1.5;

            std::optional<speculo::ErrorKind> expected;
            std::size_t expected_allocations = 0;

            if (ctor_kind == CtorKind::Match || ctor_kind == CtorKind::NoMatch)
            {
                cls = &speculo::class_of<P12Counter>();
            }
            else if (ctor_kind == CtorKind::Ambiguous)
            {
                cls = &speculo::class_of<P12Ambig>();
            }
            else if (ctor_kind == CtorKind::Deleted)
            {
                cls = &speculo::class_of<P12Deleted>();
            }
            else
            {
                cls = &speculo::class_of<P12Abstract>();
            }

            switch (ctor_kind)
            {
                case CtorKind::Match:
                    expected = allocator_kind == AllocatorKind::Normal
                        ? std::nullopt
                        : std::optional(speculo::ErrorKind::NotSupported);
                    expected_allocations = 1;
                    break;
                case CtorKind::Ambiguous:
                    expected = speculo::ErrorKind::Ambiguous;
                    break;
                case CtorKind::Deleted:
                case CtorKind::Abstract:
                    expected = speculo::ErrorKind::NotSupported;
                    break;
                case CtorKind::NoMatch:
                    expected = speculo::ErrorKind::NotFound;
                    break;
            }

            {
                speculo::Outcome<speculo::Object> created = ctor_kind == CtorKind::NoMatch
                    ? cls->new_instance_with(allocator, { wrong })
                    : ctor_kind == CtorKind::Abstract ? cls->new_instance_with(allocator)
                                                      : cls->new_instance_with(allocator, { argument });
                if (expected.has_value())
                {
                    ASSERT_TRUE(created.is_err());
                    EXPECT_EQ(created.unwrap_err().kind, *expected);
                }
                else
                {
                    ASSERT_TRUE(created.is_ok());
                }
            }

            if (allocator_kind == AllocatorKind::Normal)
            {
                EXPECT_EQ(normal.allocations, expected_allocations);
                EXPECT_EQ(normal.deallocations, expected_allocations);
                EXPECT_EQ(refusing.allocations, 0u);
            }
            else
            {
                EXPECT_EQ(refusing.allocations, expected_allocations);
                EXPECT_EQ(refusing.deallocations, 0u);
                EXPECT_EQ(normal.allocations, 0u);
            }

            EXPECT_EQ(P12Counter::live, 0);
        }
    }
}

TEST_F(AllocatorTest, ConstructionFailureReturnsTheAllocatedBlock)
{
    const speculo::Class& cls = speculo::class_of<P12MoveOnlyCtor>();
    CountingAllocator allocator;

    speculo::Object argument = speculo::Object::from_value(std::make_unique<int>(3)).unwrap();

    const speculo::Outcome<speculo::Object> created = cls.new_instance_with(allocator, { argument });
    ASSERT_TRUE(created.is_err());
    EXPECT_EQ(created.unwrap_err().kind, speculo::ErrorKind::NotSupported);

    EXPECT_EQ(allocator.allocations, 1u);
    EXPECT_EQ(allocator.deallocations, 1u);
}

TEST_F(AllocatorTest, ArenaHandlesOverAlignedTypesAndReuse)
{
    speculo::ArenaAllocator arena(128);

    const speculo::Class& aligned = speculo::class_of<P12OverAligned>();
    {
        speculo::Outcome<speculo::Object> first = aligned.new_instance_with(arena);
        ASSERT_TRUE(first.is_ok());
        speculo::Object object = std::move(first).unwrap();
        const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(object.raw_data());
        EXPECT_EQ(address % alignof(P12OverAligned), 0u);
    }

    arena.reset();
    EXPECT_EQ(arena.get_chunk_count(), 0u);

    // Usable again after reset.
    speculo::Outcome<speculo::Object> second = aligned.new_instance_with(arena);
    ASSERT_TRUE(second.is_ok());
    EXPECT_GE(arena.get_chunk_count(), 1u);
}

TEST_F(AllocatorTest, NewAggregateWithRejectsNonAggregates)
{
    CountingAllocator allocator;
    const speculo::Class& cls = speculo::class_of<P12Counter>();

    EXPECT_EQ(cls.new_aggregate_with(allocator).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(allocator.allocations, 0u);
}

TEST_F(AllocatorTest, NewAggregateWithPropagatesSlotErrorsAndFreesTheBlock)
{
    CountingAllocator allocator;
    const speculo::Class& cls = speculo::class_of<P12Aggregate>();
    int a = 1;
    double wrong = 2.0;

    // Wrong slot count.
    EXPECT_EQ(cls.new_aggregate_with(allocator).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
    EXPECT_EQ(allocator.allocations, 1u);
    EXPECT_EQ(allocator.deallocations, 1u);

    // Wrong slot type.
    EXPECT_EQ(cls.new_aggregate_with(allocator, { a, wrong }).unwrap_err().kind,
              speculo::ErrorKind::TypeMismatch);
    EXPECT_EQ(allocator.allocations, 2u);
    EXPECT_EQ(allocator.deallocations, 2u);
}

TEST_F(AllocatorTest, DefaultAllocatorCoversMakeAndFreeFunctions)
{
    CountingAllocator allocator;
    speculo::Object::set_default_allocator(&allocator);

    {
        auto made = speculo::Object::make<P12Counter>(7);
        ASSERT_TRUE(made.is_ok());
        EXPECT_EQ(allocator.allocations, 1u);

        int a = 1;
        int b = 2;
        auto invoked = speculo::function_of<&p12_add>().invoke({ a, b });
        ASSERT_TRUE(invoked.is_ok());
        EXPECT_EQ(allocator.allocations, 2u);
        EXPECT_EQ(std::move(invoked).unwrap().as<int>().unwrap(), 3);
    }

    EXPECT_EQ(allocator.deallocations, 2u);
}

TEST_F(AllocatorTest, DefaultAllocatorIsThreadLocal)
{
    CountingAllocator main_allocator;
    speculo::Object::set_default_allocator(&main_allocator);

    CountingAllocator other_allocator;
    std::thread worker([&]
    {
        EXPECT_EQ(speculo::Object::get_default_allocator(), nullptr);
        speculo::Object::set_default_allocator(&other_allocator);
        EXPECT_EQ(speculo::Object::get_default_allocator(), &other_allocator);
    });
    worker.join();

    EXPECT_EQ(speculo::Object::get_default_allocator(), &main_allocator);
}

TEST_F(AllocatorTest, DefaultAllocatorIsUsedByConstructionAndInvoke)
{
    const speculo::Class& cls = speculo::class_of<P12Counter>();
    CountingAllocator allocator;
    speculo::Object::set_default_allocator(&allocator);
    EXPECT_EQ(speculo::Object::get_default_allocator(), &allocator);

    {
        auto constructed = cls.new_instance();
        ASSERT_TRUE(constructed.is_ok());
        EXPECT_EQ(allocator.allocations, 1u);

        auto from_value = speculo::Object::from_value(P12Counter(3));
        ASSERT_TRUE(from_value.is_ok());
        EXPECT_EQ(allocator.allocations, 2u);

        int argument = 2;
        const speculo::Method twice = cls.get_method("twice").unwrap();
        auto invoked = twice.invoke(speculo::ObjectRef{}, { argument });
        ASSERT_TRUE(invoked.is_ok());
        EXPECT_EQ(allocator.allocations, 3u);
        EXPECT_EQ(P12Counter::live, 2);
    }

    EXPECT_EQ(allocator.deallocations, 3u);
    EXPECT_EQ(P12Counter::live, 0);

    speculo::Object::set_default_allocator(nullptr);
    EXPECT_EQ(speculo::Object::get_default_allocator(), nullptr);
}
