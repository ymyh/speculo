#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

// Compile-time half: speculo:compile (api_design §10).
//
// 组合矩阵（.agents/testing.md §2–§5；P ≤ 64 一律全笛卡尔积）
//
// 矩阵 1：for_each_field(obj, f) —— 对象形态 (6) × 回调形态 (4) = 24 格
//  对象 \ 回调        | const auto& | auto&& | 函数对象 | mutable lambda
//  -------------------+-------------+--------+----------+---------------
//  Plain（V0 典型）   | 名字/顺序   | 同左   | 同左     | 同左
//  Empty（V1 边界）   | 空          | 空     | 空       | 空
//  StaticOnly（V1）   | 2 个静态字段| 同左   | 同左     | 同左
//  Mixed（V5 能力）   | 位域/数组/引用/静态全访问 | 同左 | 同左 | 同左
//  const Plain（V4）  | 3 个字段    | 同左   | 同左     | 同左
//  volatile Plain（V4)| 3 个字段    | 同左   | 同左     | 同左
//  V2 类型不符：N/A —— T 由实参推导，不存在「字段类型与回调不符」的运行期错误；
//  回调形状不匹配是编译错误，不属于运行期测试。
//  V3 空：N/A —— for_each_field 没有空句柄概念（对象引用必须有效）。
//
// 矩阵 2：for_each_member<T>(f) —— 类型形态 (7) × 回调形态 (3) = 21 格
//  类型 \ 回调        | lambda | 捕获 lambda | 函数对象
//  -------------------+--------+-------------+---------
//  Plain              | Field x, y, z
//  Empty              | 空
//  StaticOnly         | StaticField counter, limit
//  Methods            | Field + Method + Destructor（与运行期并集逐项一致）
//  WithPrivate（V5）  | 只看到 public 字段 visible
//  std::string（V5）  | 空（std 类型浅物化）
//  int（V2 非类）     | 空
//  V3 空：N/A（类型参数没有空值）。
//
// 矩阵 3：field_table<T> —— 类型形态 (6) × 列 (6) = 36 格，逐格与运行期 Field 对比
//  类型 \ 列          | name | offset | size | alignment | is_bitfield | is_static
//  -------------------+------+--------+------+-----------+-------------+----------
//  Plain              | 一致 | 一致   | 一致 | 一致      | 一致        | 一致
//  Empty              | N/A（表为空；6 列都断言 size()==0）
//  StaticOnly         | 一致（offset 恒 0，is_static 为 true）
//  Mixed              | 一致（含位域）
//  Methods            | 一致
//  int                | N/A（表为空）
//
// 矩阵 4：count 两路一致性 —— 类型形态 (8) × 来源 (2：编译期 / 运行期) = 16 格
//  类型               | field_count<T>() | get_fields().size()
//  -------------------+------------------+---------------------
//  Plain              | 3                | 3
//  Empty              | 0                | 0
//  StaticOnly         | 2                | 2
//  Mixed              | 5                | 5
//  Methods            | 2                | 2
//  WithPrivate        | 1                | 1
//  std::string        | 0                | 0
//  int                | 0                | 0
//  member_count<T>() 同样与 get_fields().size() + get_methods().size() 对齐。

namespace compile_time_fixtures
{

// Named namespace: an anonymous-namespace type with an anonymous union hits a
// GCC 16.2 duplicate-symbol bug in the annotation-set materialization.
struct WithAnonymousUnion
{
    int tag = 0;
    union
    {
        int as_int;
        float as_float;
    };
};

} // namespace compile_time_fixtures

namespace
{

struct Plain
{
    int x = 0;
    double y = 0;
    char z = 0;
};

struct Empty
{
};

struct StaticOnly
{
    static int counter;
    static constexpr int limit = 9;
};

int StaticOnly::counter = 42;

struct Mixed
{
    explicit Mixed(int& reference) : ref(reference) {}

    int normal = 1;
    unsigned bits : 3 = 2;
    int arr[2]{ 3, 4 };
    int& ref;
    static int counter;
};

int Mixed::counter = 9;

class WithPrivate
{
public:
    int visible = 1;

private:
    int hidden = 2;
};

struct Methods
{
    int x = 0;
    static int counter;

    void bump() { ++x; }
    int value() const { return x; }
    static int static_value() { return counter; }
    ~Methods() = default;
    auto operator=(const Methods&) -> Methods& = default;
};

int Methods::counter = 0;

enum class CallbackForm
{
    ConstRef,
    Forwarding,
    Functor,
    Mutable,
};

struct FieldRecorder
{
    std::vector<std::string>* names;

    template<typename V>
    void operator()(std::string_view name, V&&) const
    {
        names->emplace_back(name);
    }
};

struct MemberRecorder
{
    std::vector<std::pair<std::string, speculo::MemberKind>>* members;

    void operator()(std::string_view name, speculo::MemberKind kind) const
    {
        members->emplace_back(std::string(name), kind);
    }
};

template<CallbackForm Form, typename T>
auto visited_field_names(T& obj) -> std::vector<std::string>
{
    std::vector<std::string> names;
    if constexpr (Form == CallbackForm::ConstRef)
    {
        speculo::for_each_field(obj, [&](std::string_view name, const auto&) { names.emplace_back(name); });
    }
    else if constexpr (Form == CallbackForm::Forwarding)
    {
        speculo::for_each_field(obj, [&](std::string_view name, auto&&) { names.emplace_back(name); });
    }
    else if constexpr (Form == CallbackForm::Functor)
    {
        speculo::for_each_field(obj, FieldRecorder{ &names });
    }
    else
    {
        speculo::for_each_field(obj, [&](std::string_view name, const auto&) mutable
        {
            names.emplace_back(name);
        });
    }
    return names;
}

enum class MemberCallbackForm
{
    Lambda,
    CapturingLambda,
    Functor,
};

template<MemberCallbackForm Form, typename T>
auto visited_members() -> std::vector<std::pair<std::string, speculo::MemberKind>>
{
    std::vector<std::pair<std::string, speculo::MemberKind>> members;
    auto record = [&](std::string_view name, speculo::MemberKind kind)
    {
        members.emplace_back(std::string(name), kind);
    };

    if constexpr (Form == MemberCallbackForm::Lambda)
    {
        speculo::for_each_member<T>([&](std::string_view name, speculo::MemberKind kind) { record(name, kind); });
    }
    else if constexpr (Form == MemberCallbackForm::CapturingLambda)
    {
        speculo::for_each_member<T>([&members](std::string_view name, speculo::MemberKind kind)
        {
            members.emplace_back(std::string(name), kind);
        });
    }
    else
    {
        speculo::for_each_member<T>(MemberRecorder{ &members });
    }
    return members;
}

template<typename T>
auto runtime_members() -> std::vector<std::pair<std::string, speculo::MemberKind>>
{
    std::vector<std::pair<std::string, speculo::MemberKind>> members;
    const speculo::Class& cls = speculo::class_of<T>();
    for (const speculo::Field& field : cls.get_fields())
    {
        members.emplace_back(std::string(field.get_name()),
                             field.is_static() ? speculo::MemberKind::StaticField : speculo::MemberKind::Field);
    }
    for (const speculo::Method& method : cls.get_methods())
    {
        members.emplace_back(std::string(method.get_name()),
                             method.is_destructor() ? speculo::MemberKind::Destructor : speculo::MemberKind::Method);
    }
    return members;
}

template<typename T>
void expect_members_match_runtime()
{
    const auto expected = runtime_members<T>();
    EXPECT_EQ(speculo::member_count<T>(), expected.size());

    const auto compare = [&](const auto& actual, const char* trace)
    {
        SCOPED_TRACE(trace);
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            SCOPED_TRACE(static_cast<int>(i));
            EXPECT_EQ(actual[i].first, expected[i].first);
            EXPECT_EQ(static_cast<int>(actual[i].second), static_cast<int>(expected[i].second));
        }
    };

    compare(visited_members<MemberCallbackForm::Lambda, T>(), "lambda");
    compare(visited_members<MemberCallbackForm::CapturingLambda, T>(), "capturing lambda");
    compare(visited_members<MemberCallbackForm::Functor, T>(), "functor");
}

template<typename T>
void expect_field_count_matches_runtime()
{
    const speculo::Class& cls = speculo::class_of<T>();
    EXPECT_EQ(speculo::field_count<T>(), cls.get_fields().size());
    EXPECT_EQ(speculo::member_count<T>(), cls.get_fields().size() + cls.get_methods().size());
    EXPECT_EQ(speculo::field_table<T>.size(), cls.get_fields().size());
}

template<typename T>
void expect_table_matches_runtime()
{
    const speculo::Class& cls = speculo::class_of<T>();
    const auto fields = cls.get_fields();
    const auto& table = speculo::field_table<T>;
    ASSERT_EQ(table.size(), fields.size());

    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        SCOPED_TRACE(static_cast<int>(i));
        EXPECT_EQ(table[i].name, fields[i].get_name());          // name
        EXPECT_EQ(table[i].offset, fields[i].get_offset());      // offset
        EXPECT_EQ(table[i].size, fields[i].get_size());          // size
        EXPECT_EQ(table[i].alignment, fields[i].get_alignment()); // alignment
        EXPECT_EQ(table[i].is_bitfield, fields[i].is_bitfield()); // is_bitfield
        EXPECT_EQ(table[i].is_static, fields[i].is_static());     // is_static
    }
}

} // namespace

// ── Matrix 1: for_each_field — object form × callback form ──

TEST(CompileTimeTest, ForEachFieldMatrix)
{
    Plain plain{ 1, 2.5, 'p' };
    const Plain const_plain{ 1, 2.5, 'p' };
    volatile Plain volatile_plain{ 1, 2.5, 'p' };
    Empty empty;
    StaticOnly statics;
    int reference = 7;
    Mixed mixed{ reference };

    const std::vector<std::string> plain_names{ "x", "y", "z" };
    const std::vector<std::string> empty_names{};
    const std::vector<std::string> static_names{ "counter", "limit" };
    const std::vector<std::string> mixed_names{ "normal", "bits", "arr", "ref", "counter" };

    const auto check = [](auto& obj, const std::vector<std::string>& expected, const char* trace)
    {
        SCOPED_TRACE(trace);
        EXPECT_EQ(visited_field_names<CallbackForm::ConstRef>(obj), expected);
        EXPECT_EQ(visited_field_names<CallbackForm::Forwarding>(obj), expected);
        EXPECT_EQ(visited_field_names<CallbackForm::Functor>(obj), expected);
        EXPECT_EQ(visited_field_names<CallbackForm::Mutable>(obj), expected);
    };

    check(plain, plain_names, "Plain");
    check(empty, empty_names, "Empty");
    check(statics, static_names, "StaticOnly");
    check(mixed, mixed_names, "Mixed");
    check(const_plain, plain_names, "const Plain");
    check(volatile_plain, plain_names, "volatile Plain");
}

TEST(CompileTimeTest, ForEachFieldPassesRealValues)
{
    Plain plain{ 11, 2.5, 'p' };
    speculo::for_each_field(plain, [](std::string_view name, auto& value)
    {
        if (name == "x")
        {
            EXPECT_EQ(value, 11);
            value = 0;
        }
        else if (name == "y")
        {
            EXPECT_DOUBLE_EQ(value, 2.5);
            value = 0.0;
        }
        else
        {
            EXPECT_EQ(value, 'p');
            value = '\0';
        }
    });

    EXPECT_EQ(plain.x, 0);
    EXPECT_DOUBLE_EQ(plain.y, 0.0);
    EXPECT_EQ(plain.z, '\0');
}

TEST(CompileTimeTest, ForEachFieldBitfieldArrayReferenceAndStatic)
{
    int reference = 7;
    Mixed mixed{ reference };
    mixed.bits = 5;

    bool saw_bits = false;
    bool saw_array = false;
    bool saw_reference = false;
    bool saw_static = false;

    speculo::for_each_field(mixed, [&](std::string_view name, auto&& value)
    {
        using Value = std::remove_reference_t<decltype(value)>;
        if constexpr (std::is_array_v<Value>)
        {
            if (name == "arr")
            {
                saw_array = true;
                EXPECT_EQ(value[0], 3);
                EXPECT_EQ(value[1], 4);
            }
        }
        else if constexpr (std::is_same_v<std::remove_cvref_t<Value>, unsigned>)
        {
            if (name == "bits")
            {
                saw_bits = true;
                static_assert(!std::is_lvalue_reference_v<decltype(value)>, "bit-fields are passed by value");
                EXPECT_EQ(value, 5u);
            }
        }
        else if constexpr (std::is_same_v<std::remove_cvref_t<Value>, int>)
        {
            if (name == "ref")
            {
                saw_reference = true;
                EXPECT_EQ(&value, &reference);
            }
            else if (name == "counter")
            {
                saw_static = true;
                EXPECT_EQ(value, 9);
            }
            else
            {
                EXPECT_EQ(value, 1);
            }
        }
    });

    EXPECT_TRUE(saw_bits);
    EXPECT_TRUE(saw_array);
    EXPECT_TRUE(saw_reference);
    EXPECT_TRUE(saw_static);
}

TEST(CompileTimeTest, ForEachFieldIsConstantEvaluable)
{
    constexpr auto count_fields = []
    {
        Plain plain{};
        std::size_t count = 0;
        speculo::for_each_field(plain, [&](std::string_view, const auto&) { ++count; });
        return count;
    };
    static_assert(count_fields() == 3);

    constexpr auto sum_char = []
    {
        Plain plain{ 1, 2.0, 'A' };
        int total = 0;
        speculo::for_each_field(plain, [&](std::string_view, const auto& value)
        {
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, char>)
            {
                total += value;
            }
        });
        return total;
    };
    static_assert(sum_char() == 'A');
}

// ── Matrix 2: for_each_member — type form × callback form ──

TEST(CompileTimeTest, ForEachMemberMatrix)
{
    expect_members_match_runtime<Plain>();
    expect_members_match_runtime<Empty>();
    expect_members_match_runtime<StaticOnly>();
    expect_members_match_runtime<Methods>();
    expect_members_match_runtime<WithPrivate>();
    expect_members_match_runtime<std::string>();
    expect_members_match_runtime<int>();

    EXPECT_EQ((visited_members<MemberCallbackForm::Lambda, Empty>().size()), 0u);
    EXPECT_EQ((visited_members<MemberCallbackForm::Lambda, WithPrivate>().size()), 1u);
    EXPECT_EQ((visited_members<MemberCallbackForm::Lambda, std::string>().size()), 0u);
    EXPECT_EQ((visited_members<MemberCallbackForm::Lambda, int>().size()), 0u);
}

TEST(CompileTimeTest, ForEachMemberReportsStaticAndDestructor)
{
    const auto members = visited_members<MemberCallbackForm::Lambda, Methods>();
    ASSERT_FALSE(members.empty());

    const auto find = [&](std::string_view name) -> speculo::MemberKind
    {
        for (const auto& [member_name, kind] : members)
        {
            if (member_name == name)
            {
                return kind;
            }
        }
        ADD_FAILURE() << "missing member " << name;
        return speculo::MemberKind::Field;
    };

    EXPECT_EQ(find("x"), speculo::MemberKind::Field);
    EXPECT_EQ(find("counter"), speculo::MemberKind::StaticField);
    EXPECT_EQ(find("bump"), speculo::MemberKind::Method);

    std::vector<std::string> compile_time_destructors;
    for (const auto& [name, kind] : members)
    {
        if (kind == speculo::MemberKind::Destructor)
        {
            compile_time_destructors.push_back(name);
        }
    }

    std::vector<std::string> runtime_destructors;
    for (const auto& [name, kind] : runtime_members<Methods>())
    {
        if (kind == speculo::MemberKind::Destructor)
        {
            runtime_destructors.push_back(name);
        }
    }

    ASSERT_EQ(compile_time_destructors.size(), 1u);
    EXPECT_EQ(compile_time_destructors, runtime_destructors);
}

TEST(CompileTimeTest, ForEachMemberIsConstantEvaluable)
{
    constexpr auto count_members = []
    {
        std::size_t count = 0;
        speculo::for_each_member<Plain>([&](std::string_view, speculo::MemberKind) { ++count; });
        return count;
    };
    static_assert(count_members() == 3);
}

// ── Matrix 3: field_table — type form × column ──

TEST(CompileTimeTest, ForEachFieldMatchesRuntimeOrder)
{
    Plain plain{};
    const Plain const_plain{};
    Empty empty;
    StaticOnly statics;
    int reference = 0;
    Mixed mixed{ reference };
    Methods methods;
    WithPrivate private_object;
    std::string text;
    int number = 0;

    const auto compare = [](auto& obj, const speculo::Class& cls, const char* trace)
    {
        SCOPED_TRACE(trace);
        const std::vector<std::string> runtime_names = [&]
        {
            std::vector<std::string> names;
            for (const speculo::Field& field : cls.get_fields())
            {
                names.emplace_back(field.get_name());
            }
            return names;
        }();
        EXPECT_EQ(visited_field_names<CallbackForm::ConstRef>(obj), runtime_names);
    };

    compare(plain, speculo::class_of<Plain>(), "Plain");
    compare(const_plain, speculo::class_of<Plain>(), "const Plain");
    compare(empty, speculo::class_of<Empty>(), "Empty");
    compare(statics, speculo::class_of<StaticOnly>(), "StaticOnly");
    compare(mixed, speculo::class_of<Mixed>(), "Mixed");
    compare(methods, speculo::class_of<Methods>(), "Methods");
    compare(private_object, speculo::class_of<WithPrivate>(), "WithPrivate");
    compare(text, speculo::class_of<std::string>(), "std::string");
    compare(number, speculo::class_of<int>(), "int");
}

TEST(CompileTimeTest, ForEachFieldHandlesAnonymousUnion)
{
    compile_time_fixtures::WithAnonymousUnion value{};
    value.as_int = 7;

    std::vector<std::string> names;
    speculo::for_each_field(value, [&](std::string_view name, const auto& field)
    {
        names.emplace_back(name);
        using Field = std::remove_reference_t<decltype(field)>;
        if constexpr (std::is_union_v<Field>)
        {
            EXPECT_EQ(field.as_int, 7);
        }
    });

    std::vector<std::string> runtime_names;
    for (const speculo::Field& field : speculo::class_of<compile_time_fixtures::WithAnonymousUnion>().get_fields())
    {
        runtime_names.emplace_back(field.get_name());
    }

    EXPECT_EQ(names, runtime_names);
    EXPECT_EQ(speculo::field_count<compile_time_fixtures::WithAnonymousUnion>(), runtime_names.size());
}

TEST(CompileTimeTest, FieldTableMatrix){
    expect_table_matches_runtime<Plain>();
    expect_table_matches_runtime<Empty>();
    expect_table_matches_runtime<StaticOnly>();
    expect_table_matches_runtime<Mixed>();
    expect_table_matches_runtime<Methods>();
    expect_table_matches_runtime<int>();

    EXPECT_TRUE(speculo::field_table<Empty>.empty());
    EXPECT_TRUE(speculo::field_table<int>.empty());
}

TEST(CompileTimeTest, FieldTableStaticMembersUseZeroOffset)
{
    const auto& table = speculo::field_table<StaticOnly>;
    ASSERT_EQ(table.size(), 2u);
    EXPECT_EQ(table[0].name, "counter");
    EXPECT_EQ(table[0].offset, 0u);
    EXPECT_TRUE(table[0].is_static);
    EXPECT_EQ(table[1].name, "limit");
    EXPECT_EQ(table[1].offset, 0u);
    EXPECT_TRUE(table[1].is_static);
}

TEST(CompileTimeTest, FieldTableIsAConstantIndex)
{
    static_assert(speculo::field_count<Plain>() == 3);
    static_assert(speculo::field_table<Plain>.size() == 3);
    static_assert(speculo::field_table<Plain>[0].name == "x");
    static_assert(speculo::field_table<Plain>[0].offset == 0);
    static_assert(speculo::field_table<Plain>[0].size == sizeof(int));
    static_assert(speculo::field_table<Plain>[0].alignment == alignof(int));
    static_assert(!speculo::field_table<Plain>[0].is_bitfield);
    static_assert(!speculo::field_table<Plain>[0].is_static);
    static_assert(speculo::field_table<Mixed>[1].is_bitfield);

    constexpr std::size_t y_offset = speculo::field_table<Plain>[1].offset;
    static_assert(y_offset == speculo::field_table<Plain>[1].offset);
}

TEST(CompileTimeTest, FieldTableNormalizesCv)
{
    static_assert(speculo::field_count<const Plain>() == speculo::field_count<Plain>());
    static_assert(speculo::field_count<volatile Plain>() == speculo::field_count<Plain>());
    static_assert(speculo::field_count<Plain&>() == speculo::field_count<Plain>());
    static_assert(speculo::field_table<const Plain>.size() == speculo::field_table<Plain>.size());
    static_assert(speculo::member_count<const Plain>() == speculo::member_count<Plain>());
}

// ── Matrix 4: count two-path consistency ──

TEST(CompileTimeTest, FieldAndMemberCountsMatchRuntime)
{
    expect_field_count_matches_runtime<Plain>();
    expect_field_count_matches_runtime<Empty>();
    expect_field_count_matches_runtime<StaticOnly>();
    expect_field_count_matches_runtime<Mixed>();
    expect_field_count_matches_runtime<Methods>();
    expect_field_count_matches_runtime<WithPrivate>();
    expect_field_count_matches_runtime<std::string>();
    expect_field_count_matches_runtime<int>();
}

TEST(CompileTimeTest, CountsAreConstantEvaluable)
{
    static_assert(speculo::field_count<Plain>() == 3);
    static_assert(speculo::field_count<Empty>() == 0);
    static_assert(speculo::field_count<StaticOnly>() == 2);
    static_assert(speculo::field_count<Mixed>() == 5);
    static_assert(speculo::field_count<std::string>() == 0);
    static_assert(speculo::field_count<int>() == 0);

    static_assert(speculo::member_count<Plain>() == 3);
    static_assert(speculo::member_count<Empty>() == 0);
    static_assert(speculo::member_count<std::string>() == 0);
}
