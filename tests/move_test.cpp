#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

// speculo::Class::assign / swap / destroy and speculo::Object::move_from (api_design §8.2).
//
// 矩阵 1：Class::assign(dst, src) —— 5 × 6 = 30 格，AssignMatrix 全断言
//  dst \ src       | 左值      | 右值      | const     | volatile     | 类型不符     | 空
//  ----------------+-----------+-----------+-----------+--------------+--------------+-----------
//  左值            | 拷贝      | 移动      | 拷贝      | NotSupported | TypeMismatch | NullInstance
//  const           | ReadOnly  | ReadOnly  | ReadOnly  | ReadOnly     | TypeMismatch | NullInstance
//  volatile        | NotSupported | NotSupported | NotSupported | NotSupported | TypeMismatch | NullInstance
//  类型不符        | TypeMismatch | TypeMismatch | TypeMismatch | TypeMismatch | TypeMismatch | NullInstance
//  空              | NullInstance（先于 src 检查）
//  （const + volatile src：const 检查先于 volatile → ReadOnly；空 src 先于类型/const 检查。）
//
// 矩阵 2：Class::swap(a, b) —— 5 × 5 = 25 格，SwapMatrix 全断言
//  任一空 → NullInstance；任一类型不符 → TypeMismatch（空检查先于类型）；
//  任一 const → ReadOnly（类型检查先于 const）；任一 volatile → NotSupported；
//  左右都正常 → Ok 且值交换。
//
// 矩阵 3：Class::destroy(obj) —— 5 格，DestroyMatrix 全断言
//  正常 / const → Ok；volatile → NotSupported；类型不符 → TypeMismatch；空 → NullInstance。
//  不可析构类型（能力缺失）单独由 ObjectMoveFromErrorPaths 的 Immobile 用例覆盖。
//
// 矩阵 4：Object::move_from(source) —— 目标状态 × 源状态 = 3 × 2 = 6 格，MoveFromMatrix 全断言
//  源空 → NullInstance；目标非空且类型不同 → TypeMismatch；空目标 / 同类型目标 → Ok。
//  move-only / 不可移动类型由单独用例覆盖。

namespace
{

struct Tracked
{
    inline static int copies = 0;
    inline static int moves = 0;
    inline static int destructions = 0;

    int value = 0;

    Tracked() = default;
    explicit Tracked(int value)
        : value(value)
    {
    }

    Tracked(const Tracked& other)
        : value(other.value)
    {
        ++copies;
    }

    auto operator=(const Tracked& other) -> Tracked&
    {
        value = other.value;
        ++copies;
        return *this;
    }

    Tracked(Tracked&& other) noexcept
        : value(other.value)
    {
        other.value = -1;
        ++moves;
    }

    auto operator=(Tracked&& other) noexcept -> Tracked&
    {
        value = other.value;
        other.value = -1;
        ++moves;
        return *this;
    }

    ~Tracked() { ++destructions; }

    static void reset()
    {
        copies = 0;
        moves = 0;
        destructions = 0;
    }
};

struct MoveOnly
{
    int value = 0;

    MoveOnly() = default;
    explicit MoveOnly(int value)
        : value(value)
    {
    }

    MoveOnly(const MoveOnly&) = delete;
    auto operator=(const MoveOnly&) -> MoveOnly& = delete;

    MoveOnly(MoveOnly&& other) noexcept
        : value(other.value)
    {
        other.value = -1;
    }

    auto operator=(MoveOnly&& other) noexcept -> MoveOnly&
    {
        value = other.value;
        other.value = -1;
        return *this;
    }
};

struct Immobile
{
    int value = 0;

    Immobile() = default;
    Immobile(const Immobile&) = delete;
    auto operator=(const Immobile&) -> Immobile& = delete;
    Immobile(Immobile&&) = delete;
    auto operator=(Immobile&&) -> Immobile& = delete;
};

struct Other
{
    int value = 0;
};

struct NoDtor
{
    int value = 0;
    ~NoDtor() = delete;
};

enum class DstForm
{
    Normal,
    Const,
    Volatile,
    WrongType,
    Null,
};

enum class SrcForm
{
    Lvalue,
    Rvalue,
    Const,
    Volatile,
    WrongType,
    Null,
};

enum class ObjForm
{
    Normal,
    Const,
    Volatile,
    WrongType,
    Null,
};

enum class DestroyForm
{
    Normal,
    Const,
    Volatile,
    WrongType,
    Null,
};

enum class DestState
{
    Empty,
    Same,
    Different,
};

enum class SourceState
{
    Value,
    Empty,
};

struct Expected
{
    bool             ok = false;
    speculo::ErrorKind kind = speculo::ErrorKind::NotSupported;
};

[[nodiscard]] auto assign_expected(DstForm dst, SrcForm src) -> Expected
{
    if (dst == DstForm::Null)
    {
        return { false, speculo::ErrorKind::NullInstance };
    }

    if (src == SrcForm::Null)
    {
        return { false, speculo::ErrorKind::NullInstance };
    }

    if (dst == DstForm::WrongType)
    {
        return { false, speculo::ErrorKind::TypeMismatch };
    }

    if (src == SrcForm::WrongType)
    {
        return { false, speculo::ErrorKind::TypeMismatch };
    }

    if (dst == DstForm::Const)
    {
        return { false, speculo::ErrorKind::ReadOnly };
    }

    if (dst == DstForm::Volatile || src == SrcForm::Volatile)
    {
        return { false, speculo::ErrorKind::NotSupported };
    }

    return { true, speculo::ErrorKind::NotSupported };
}

[[nodiscard]] auto swap_expected(ObjForm first, ObjForm second) -> Expected
{
    if (first == ObjForm::Null || second == ObjForm::Null)
    {
        return { false, speculo::ErrorKind::NullInstance };
    }

    if (first == ObjForm::WrongType || second == ObjForm::WrongType)
    {
        return { false, speculo::ErrorKind::TypeMismatch };
    }

    if (first == ObjForm::Const || second == ObjForm::Const)
    {
        return { false, speculo::ErrorKind::ReadOnly };
    }

    if (first == ObjForm::Volatile || second == ObjForm::Volatile)
    {
        return { false, speculo::ErrorKind::NotSupported };
    }

    return { true, speculo::ErrorKind::NotSupported };
}

[[nodiscard]] auto destroy_expected(DestroyForm form) -> Expected
{
    switch (form)
    {
    case DestroyForm::Null:
        return { false, speculo::ErrorKind::NullInstance };
    case DestroyForm::WrongType:
        return { false, speculo::ErrorKind::TypeMismatch };
    case DestroyForm::Volatile:
        return { false, speculo::ErrorKind::NotSupported };
    default:
        return { true, speculo::ErrorKind::NotSupported };
    }
}

TEST(MoveTest, AssignCopiesFromAnLvalueAndMovesFromAnRvalueView)
{
    Tracked::reset();
    Tracked dst(1);
    Tracked src(2);
    const speculo::Class& cls = speculo::class_of<Tracked>();

    ASSERT_TRUE(cls.assign(dst, src).is_ok());
    EXPECT_EQ(dst.value, 2);
    EXPECT_EQ(Tracked::copies, 1);
    EXPECT_EQ(Tracked::moves, 0);

    ASSERT_TRUE(cls.assign(dst, speculo::ObjectRef(src).as_rvalue()).is_ok());
    EXPECT_EQ(dst.value, 2);
    EXPECT_EQ(src.value, -1); // moved from
    EXPECT_EQ(Tracked::moves, 1);
}

TEST(MoveTest, AssignRejectsBadReceiversAndTypes)
{
    Tracked dst(1);
    Tracked src(2);
    const speculo::Class& cls = speculo::class_of<Tracked>();

    const auto null_dst = cls.assign(speculo::ObjectRef{}, src);
    ASSERT_TRUE(null_dst.is_err());
    EXPECT_EQ(null_dst.unwrap_err().kind, speculo::ErrorKind::NullInstance);

    const auto null_src = cls.assign(dst, speculo::ObjectRef{});
    ASSERT_TRUE(null_src.is_err());
    EXPECT_EQ(null_src.unwrap_err().kind, speculo::ErrorKind::NullInstance);

    Other other;
    const auto wrong_dst = cls.assign(other, src);
    ASSERT_TRUE(wrong_dst.is_err());
    EXPECT_EQ(wrong_dst.unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    const auto wrong_src = cls.assign(dst, other);
    ASSERT_TRUE(wrong_src.is_err());
    EXPECT_EQ(wrong_src.unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    const Tracked const_dst(3);
    const auto const_target = cls.assign(const_dst, src);
    ASSERT_TRUE(const_target.is_err());
    EXPECT_EQ(const_target.unwrap_err().kind, speculo::ErrorKind::ReadOnly);

    volatile Tracked volatile_dst;
    const auto volatile_target = cls.assign(volatile_dst, src);
    ASSERT_TRUE(volatile_target.is_err());
    EXPECT_EQ(volatile_target.unwrap_err().kind, speculo::ErrorKind::NotSupported);

    const auto volatile_source = cls.assign(dst, speculo::ObjectRef(volatile_dst));
    ASSERT_TRUE(volatile_source.is_err());
    EXPECT_EQ(volatile_source.unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(MoveTest, AssignIsNotSupportedForImmobileTypes)
{
    Immobile first;
    Immobile second;
    const speculo::Class& cls = speculo::class_of<Immobile>();

    const auto copied = cls.assign(first, second);
    ASSERT_TRUE(copied.is_err());
    EXPECT_EQ(copied.unwrap_err().kind, speculo::ErrorKind::NotSupported);

    const auto moved = cls.assign(first, speculo::ObjectRef(second).as_rvalue());
    ASSERT_TRUE(moved.is_err());
    EXPECT_EQ(moved.unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(MoveTest, SwapExchangesValues)
{
    Tracked first(1);
    Tracked second(2);
    const speculo::Class& cls = speculo::class_of<Tracked>();

    ASSERT_TRUE(cls.swap(first, second).is_ok());
    EXPECT_EQ(first.value, 2);
    EXPECT_EQ(second.value, 1);

    const Tracked const_first(3);
    const auto const_result = cls.swap(const_first, second);
    ASSERT_TRUE(const_result.is_err());
    EXPECT_EQ(const_result.unwrap_err().kind, speculo::ErrorKind::ReadOnly);

    const auto null_result = cls.swap(speculo::ObjectRef{}, second);
    ASSERT_TRUE(null_result.is_err());
    EXPECT_EQ(null_result.unwrap_err().kind, speculo::ErrorKind::NullInstance);
}

TEST(MoveTest, SwapIsNotSupportedForImmobileTypes)
{
    Immobile first;
    Immobile second;
    const auto result = speculo::class_of<Immobile>().swap(first, second);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(MoveTest, DestroyRunsTheDestructorInPlace)
{
    Tracked::reset();
    alignas(Tracked) unsigned char storage[sizeof(Tracked)];
    auto* object = ::new (static_cast<void*>(storage)) Tracked(9);

    ASSERT_TRUE(speculo::class_of<Tracked>().destroy(speculo::ObjectRef(*object)).is_ok());
    EXPECT_EQ(Tracked::destructions, 1);

    const auto again = speculo::class_of<Tracked>().destroy(speculo::ObjectRef{});
    ASSERT_TRUE(again.is_err());
    EXPECT_EQ(again.unwrap_err().kind, speculo::ErrorKind::NullInstance);
}

TEST(MoveTest, ObjectMoveFromMovesTheValueAndLeavesTheSourceValid)
{
    auto destination = speculo::Object::make<Tracked>(1).unwrap();
    auto source = speculo::Object::make<Tracked>(2).unwrap();

    ASSERT_TRUE(destination.move_from(std::move(source)).is_ok());
    EXPECT_EQ(destination.as<Tracked>().unwrap().value, 2);
    ASSERT_FALSE(source.is_null());
    EXPECT_EQ(source.as<Tracked>().unwrap().value, -1);
}

TEST(MoveTest, ObjectMoveFromWorksForMoveOnlyValues)
{
    auto destination = speculo::Object::make<MoveOnly>(1).unwrap();
    auto source = speculo::Object::make<MoveOnly>(2).unwrap();

    ASSERT_TRUE(destination.move_from(std::move(source)).is_ok());
    EXPECT_EQ(destination.as<MoveOnly>().unwrap().value, 2);
    EXPECT_EQ(source.as<MoveOnly>().unwrap().value, -1);
}

TEST(MoveTest, ObjectMoveFromErrorPaths)
{
    speculo::Object empty;
    speculo::Object empty_source;
    const auto null_source = empty.move_from(std::move(empty_source));
    ASSERT_TRUE(null_source.is_err());
    EXPECT_EQ(null_source.unwrap_err().kind, speculo::ErrorKind::NullInstance);

    auto destination = speculo::Object::make<Tracked>(1).unwrap();
    auto wrong_type = speculo::Object::make<Other>().unwrap();
    const auto mismatch = destination.move_from(std::move(wrong_type));
    ASSERT_TRUE(mismatch.is_err());
    EXPECT_EQ(mismatch.unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    auto immobile = speculo::Object::make<Immobile>().unwrap();
    auto immobile_source = speculo::Object::make<Immobile>().unwrap();
    const auto unsupported = immobile.move_from(std::move(immobile_source));
    ASSERT_TRUE(unsupported.is_err());
    EXPECT_EQ(unsupported.unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(MoveTest, AssignMatrix)
{
    constexpr std::array<DstForm, 5> kDstForms{ DstForm::Normal, DstForm::Const, DstForm::Volatile,
                                                 DstForm::WrongType, DstForm::Null };
    constexpr std::array<SrcForm, 6> kSrcForms{ SrcForm::Lvalue, SrcForm::Rvalue, SrcForm::Const,
                                                 SrcForm::Volatile, SrcForm::WrongType, SrcForm::Null };

    const speculo::Class& cls = speculo::class_of<Tracked>();
    for (DstForm dst_form : kDstForms)
    {
        for (SrcForm src_form : kSrcForms)
        {
            SCOPED_TRACE(std::to_string(static_cast<int>(dst_form)) + "x" + std::to_string(static_cast<int>(src_form)));
            Tracked::reset();
            Tracked        dst(1);
            Tracked        src(2);
            const Tracked  const_dst(1);
            volatile Tracked volatile_dst;
            const Tracked  const_src(2);
            volatile Tracked volatile_src;
            Other          other;

            speculo::ObjectRef dst_ref;
            switch (dst_form)
            {
            case DstForm::Normal:    dst_ref = speculo::ObjectRef(dst); break;
            case DstForm::Const:     dst_ref = speculo::ObjectRef(const_dst); break;
            case DstForm::Volatile:  dst_ref = speculo::ObjectRef(volatile_dst); break;
            case DstForm::WrongType: dst_ref = speculo::ObjectRef(other); break;
            case DstForm::Null:      break;
            }

            speculo::ObjectRef src_ref;
            switch (src_form)
            {
            case SrcForm::Lvalue:    src_ref = speculo::ObjectRef(src); break;
            case SrcForm::Rvalue:    src_ref = speculo::ObjectRef(src).as_rvalue(); break;
            case SrcForm::Const:     src_ref = speculo::ObjectRef(const_src); break;
            case SrcForm::Volatile:  src_ref = speculo::ObjectRef(volatile_src); break;
            case SrcForm::WrongType: src_ref = speculo::ObjectRef(other); break;
            case SrcForm::Null:      break;
            }

            const Expected expected = assign_expected(dst_form, src_form);
            const auto     result = cls.assign(dst_ref, src_ref);
            if (!expected.ok)
            {
                ASSERT_TRUE(result.is_err());
                EXPECT_EQ(result.unwrap_err().kind, expected.kind);
                continue;
            }

            ASSERT_TRUE(result.is_ok());
            EXPECT_EQ(dst.value, 2);
            if (src_form == SrcForm::Rvalue)
            {
                EXPECT_EQ(src.value, -1);
                EXPECT_EQ(Tracked::moves, 1);
                EXPECT_EQ(Tracked::copies, 0);
            }
            else
            {
                EXPECT_EQ(Tracked::copies, 1);
                EXPECT_EQ(Tracked::moves, 0);
            }
        }
    }
}

TEST(MoveTest, SwapMatrix)
{
    constexpr std::array<ObjForm, 5> kForms{ ObjForm::Normal, ObjForm::Const, ObjForm::Volatile,
                                             ObjForm::WrongType, ObjForm::Null };

    const speculo::Class& cls = speculo::class_of<Tracked>();
    for (ObjForm first_form : kForms)
    {
        for (ObjForm second_form : kForms)
        {
            SCOPED_TRACE(std::to_string(static_cast<int>(first_form)) + "x" + std::to_string(static_cast<int>(second_form)));
            Tracked        first(1);
            Tracked        second(2);
            const Tracked  const_first(1);
            const Tracked  const_second(2);
            volatile Tracked volatile_first;
            volatile Tracked volatile_second;
            Other          other_first;
            Other          other_second;

            speculo::ObjectRef a;
            switch (first_form)
            {
            case ObjForm::Normal:    a = speculo::ObjectRef(first); break;
            case ObjForm::Const:     a = speculo::ObjectRef(const_first); break;
            case ObjForm::Volatile:  a = speculo::ObjectRef(volatile_first); break;
            case ObjForm::WrongType: a = speculo::ObjectRef(other_first); break;
            case ObjForm::Null:      break;
            }

            speculo::ObjectRef b;
            switch (second_form)
            {
            case ObjForm::Normal:    b = speculo::ObjectRef(second); break;
            case ObjForm::Const:     b = speculo::ObjectRef(const_second); break;
            case ObjForm::Volatile:  b = speculo::ObjectRef(volatile_second); break;
            case ObjForm::WrongType: b = speculo::ObjectRef(other_second); break;
            case ObjForm::Null:      break;
            }

            const Expected expected = swap_expected(first_form, second_form);
            const auto     result = cls.swap(a, b);
            if (!expected.ok)
            {
                ASSERT_TRUE(result.is_err());
                EXPECT_EQ(result.unwrap_err().kind, expected.kind);
                continue;
            }

            ASSERT_TRUE(result.is_ok());
            EXPECT_EQ(first.value, 2);
            EXPECT_EQ(second.value, 1);
        }
    }
}

TEST(MoveTest, DestroyMatrix)
{
    constexpr std::array<DestroyForm, 5> kForms{ DestroyForm::Normal, DestroyForm::Const, DestroyForm::Volatile,
                                                 DestroyForm::WrongType, DestroyForm::Null };

    const speculo::Class& cls = speculo::class_of<Tracked>();
    for (DestroyForm form : kForms)
    {
        SCOPED_TRACE(std::to_string(static_cast<int>(form)));
        Tracked::reset();
        speculo::ObjectRef object;
        alignas(Tracked) unsigned char storage[sizeof(Tracked)];
        volatile Tracked volatile_object;
        Other            other;

        switch (form)
        {
        case DestroyForm::Normal:
        {
            auto* placed = ::new (static_cast<void*>(storage)) Tracked(9);
            object = speculo::ObjectRef(*placed);
            break;
        }
        case DestroyForm::Const:
        {
            auto* placed = ::new (static_cast<void*>(storage)) Tracked(9);
            object = speculo::ObjectRef(std::as_const(*placed));
            break;
        }
        case DestroyForm::Volatile:  object = speculo::ObjectRef(volatile_object); break;
        case DestroyForm::WrongType: object = speculo::ObjectRef(other); break;
        case DestroyForm::Null:      break;
        }

        const Expected expected = destroy_expected(form);
        const auto     result = cls.destroy(object);
        if (!expected.ok)
        {
            ASSERT_TRUE(result.is_err());
            EXPECT_EQ(result.unwrap_err().kind, expected.kind);
            continue;
        }

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(Tracked::destructions, 1);
    }
}

TEST(MoveTest, DestroyOfANonDestructibleTypeIsNotSupported)
{
    auto* object = new NoDtor;
    const auto result = speculo::class_of<NoDtor>().destroy(speculo::ObjectRef(*object));
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(MoveTest, MoveFromMatrix)
{
    constexpr std::array<DestState, 3> kDestinations{ DestState::Empty, DestState::Same, DestState::Different };
    constexpr std::array<SourceState, 2> kSources{ SourceState::Value, SourceState::Empty };

    for (DestState destination_state : kDestinations)
    {
        for (SourceState source_state : kSources)
        {
            SCOPED_TRACE(std::to_string(static_cast<int>(destination_state)) + "x" + std::to_string(static_cast<int>(source_state)));
            speculo::Object destination;
            if (destination_state == DestState::Same)
            {
                destination = speculo::Object::make<Tracked>(1).unwrap();
            }
            else if (destination_state == DestState::Different)
            {
                destination = speculo::Object::make<Other>().unwrap();
            }

            speculo::Object source;
            if (source_state == SourceState::Value)
            {
                source = speculo::Object::make<Tracked>(2).unwrap();
            }

            const auto result = destination.move_from(std::move(source));

            if (source_state == SourceState::Empty)
            {
                ASSERT_TRUE(result.is_err());
                EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::NullInstance);
                continue;
            }

            if (destination_state == DestState::Different)
            {
                ASSERT_TRUE(result.is_err());
                EXPECT_EQ(result.unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
                continue;
            }

            ASSERT_TRUE(result.is_ok());
            EXPECT_EQ(destination.as<Tracked>().unwrap().value, 2);
            EXPECT_EQ(source.as<Tracked>().unwrap().value, -1);
        }
    }
}

} // namespace
