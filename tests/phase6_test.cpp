#include <speculo/macros.h>

#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

namespace p6
{

struct Point
{
    int x = 0;
    int y = 0;

    Point() = default;
    Point(int a, int b) : x(a), y(b) {}

    int scale(int factor) const { return x * factor; }
};

struct Base
{
    int b = 0;
};

struct Derived : Base
{
    int d = 0;
};

/// Registration is not auto-triggered for this type anywhere else; the macro
/// below must make it findable by name without a class_of<MacroRegistered>() call.
struct MacroRegistered
{
    int value = 0;
};

/// SPECULO_IMPL synthesizes get_class(); reflection must exclude that accessor
/// (it would otherwise drag `const Class&` into the type's materialization).
struct WithImpl
{
    SPECULO_IMPL(WithImpl)
    int value = 0;
    int get_value() const { return value; }
};

/// Per-thread registration target for the concurrency test (must be in a named
/// namespace so it has a canonical name and actually gets registered).
template<int I>
struct Tag
{
    int payload = I;
};

/// A parameter whose compiler spelling (`long long unsigned int`) nobody writes.
struct Wide
{
    int take(unsigned long long value) const { return static_cast<int>(value); }
};

} // namespace p6

SPECULO_REGISTER(p6::MacroRegistered);

namespace
{

struct Anonymous
{
    int a = 0;
};

} // namespace

TEST(RegistryTest, ClassOfAutoRegisters)
{
    const speculo::Class& cls = speculo::class_of<p6::Point>();
    ASSERT_TRUE(cls.has_canonical_name());

    speculo::Option<const speculo::Class&> found = speculo::Class::for_name("p6::Point");
    ASSERT_TRUE(found.is_some());
    EXPECT_EQ(found.unwrap(), cls);
}

TEST(RegistryTest, FundamentalTypesAreRegistrable)
{
    // GCC's has_identifier is false for fundamentals; has_canonical_name must
    // still report true so that names like "int" resolve.
    EXPECT_TRUE(speculo::class_of<int>().has_canonical_name());

    (void)speculo::class_of<int>();
    EXPECT_TRUE(speculo::Class::for_name("int").is_some());
}

TEST(RegistryTest, UnknownNameIsNone)
{
    EXPECT_TRUE(speculo::Class::for_name("does::not::Exist").is_none());
}

TEST(RegistryTest, NormalizedNamesAreTheSameOnEveryCompiler)
{
    // The compiler spells these `long unsigned int`, `long long unsigned int`, ...
    EXPECT_EQ(speculo::class_of<unsigned long>().get_normalized_name(), "unsigned long");
    EXPECT_EQ(speculo::class_of<unsigned long long>().get_normalized_name(), "unsigned long long");
    EXPECT_EQ(speculo::class_of<unsigned>().get_normalized_name(), "unsigned int");
    EXPECT_EQ(speculo::class_of<unsigned short>().get_normalized_name(), "unsigned short");
    EXPECT_EQ(speculo::class_of<short>().get_normalized_name(), "short");
    EXPECT_EQ(speculo::class_of<long>().get_normalized_name(), "long");
    EXPECT_EQ(speculo::class_of<long long>().get_normalized_name(), "long long");
    EXPECT_EQ(speculo::class_of<signed char>().get_normalized_name(), "signed char");
    EXPECT_EQ(speculo::class_of<unsigned char>().get_normalized_name(), "unsigned char");
    EXPECT_EQ(speculo::class_of<long double>().get_normalized_name(), "long double");
    EXPECT_EQ(speculo::class_of<int>().get_normalized_name(), "int");

    EXPECT_EQ(speculo::class_of<const p6::Point&>().get_normalized_name(), "const p6::Point&");
    EXPECT_EQ(speculo::class_of<const int* const>().get_normalized_name(), "const int* const");
    EXPECT_EQ(speculo::class_of<int[3]>().get_normalized_name(), "int[3]");
    EXPECT_EQ(speculo::class_of<std::string_view>().get_normalized_name(), "std::string_view");
    EXPECT_EQ(speculo::class_of<p6::Tag<3>>().get_normalized_name(), "p6::Tag<3>");

    // The compiler's own spelling stays available and unchanged.
    EXPECT_NE(speculo::class_of<unsigned long long>().get_qualified_name(), "unsigned long long");
}

TEST(RegistryTest, ForNameAcceptsEveryReasonableSpelling)
{
    const speculo::Class& ull = speculo::class_of<unsigned long long>();
    const speculo::Class& ul = speculo::class_of<unsigned long>();
    for (std::string_view spelling : { "unsigned long long", "long long unsigned int", "long long unsigned",
                                       "unsigned  long   long", " unsigned long long " })
    {
        EXPECT_EQ(speculo::Class::for_name(spelling).unwrap(), ull) << spelling;
    }

    // Distinct types stay distinct.
    (void)speculo::class_of<int>();
    (void)speculo::class_of<short>();
    (void)speculo::class_of<unsigned>();
    (void)speculo::class_of<int>();
    (void)speculo::class_of<short>();
    (void)speculo::class_of<unsigned>();
    EXPECT_EQ(speculo::Class::for_name("unsigned long").unwrap(), ul);
    EXPECT_NE(ul, ull);
    EXPECT_EQ(speculo::Class::for_name("signed int").unwrap(), speculo::class_of<int>());
    EXPECT_EQ(speculo::Class::for_name("short int").unwrap(), speculo::class_of<short>());
    EXPECT_EQ(speculo::Class::for_name("unsigned").unwrap(), speculo::class_of<unsigned>());

    // Whitespace around qualifiers and declarators does not matter.
    const speculo::Class& ref = speculo::class_of<const p6::Point&>();
    const speculo::Class& cptr = speculo::class_of<const int* const>();
    const speculo::Class& array = speculo::class_of<int[3]>();
    EXPECT_EQ(speculo::Class::for_name("const p6::Point &").unwrap(), ref);
    EXPECT_EQ(speculo::Class::for_name("const  p6::Point&").unwrap(), ref);
    EXPECT_EQ(speculo::Class::for_name("const int *const").unwrap(), cptr);
    EXPECT_EQ(speculo::Class::for_name("int [3]").unwrap(), array);
    EXPECT_EQ(speculo::Class::for_name("int[3]").unwrap(), array);

    // The compiler's `{aka ...}` annotation is ignored; the standard string spellings meet.
    EXPECT_EQ(speculo::Class::for_name("p6::Point {aka p6::Point}").unwrap(), speculo::class_of<p6::Point>());
    const speculo::Class& view = speculo::class_of<std::string_view>();
    EXPECT_EQ(speculo::Class::for_name("std::string_view").unwrap(), view);
    EXPECT_EQ(speculo::Class::for_name("std::basic_string_view<char>").unwrap(), view);
    EXPECT_EQ(speculo::Class::for_name(view.get_qualified_name()).unwrap(), view);

    // A different type is still not found.
    EXPECT_TRUE(speculo::Class::for_name("unsigned long long long").is_none());
    EXPECT_TRUE(speculo::Class::for_name("p6::Pointer").is_none());
}

TEST(RegistryTest, RegisteredNamesListsBothSpellingsSorted)
{
    (void)speculo::class_of<unsigned long long>();
    (void)speculo::class_of<p6::Point>();

    const std::vector<std::string_view> names = speculo::Class::registered_names();
    EXPECT_TRUE(std::ranges::is_sorted(names));
    EXPECT_TRUE(std::ranges::contains(names, std::string_view("p6::Point")));
    EXPECT_TRUE(std::ranges::contains(names, std::string_view("unsigned long long")));
    EXPECT_TRUE(std::ranges::contains(names, std::string_view("long long unsigned int")));
}

TEST(RegistryTest, ParameterTypeNamesUseTheSameSpellings)
{
    const speculo::Class& cls = speculo::class_of<p6::Wide>();

    // `unsigned long long` is how it is written, not how GCC prints it.
    EXPECT_TRUE(cls.get_method("take", { "unsigned long long" }).is_ok());
    EXPECT_TRUE(cls.get_method("take", { "long long unsigned int" }).is_ok());
    EXPECT_TRUE(cls.get_method("take", { " unsigned  long long " }).is_ok());
    EXPECT_EQ(cls.get_method("take", { "unsigned long" }).unwrap_err().kind, speculo::ErrorKind::NotFound);

    // An unregistered name is reported as such, with what the registry knows.
    auto missing = cls.get_method("take", { "p6::Nope" });
    ASSERT_TRUE(missing.is_err());
    EXPECT_EQ(missing.unwrap_err().kind, speculo::ErrorKind::NotFound);
    const std::string message = missing.unwrap_err().message;
    EXPECT_NE(message.find("p6::Nope"), std::string::npos);
    EXPECT_NE(message.find("not registered"), std::string::npos);
    EXPECT_NE(message.find("registered:"), std::string::npos);
}

TEST(RegistryTest, RegisterClassIsIdempotent)
{
    const speculo::Class& cls = speculo::class_of<p6::Base>();

    speculo::register_class(cls);
    speculo::register_class(cls); // second call must be a no-op

    speculo::Option<const speculo::Class&> found = speculo::Class::for_name("p6::Base");
    ASSERT_TRUE(found.is_some());
    EXPECT_EQ(found.unwrap(), cls);
}

TEST(RegistryTest, MacroRegistersWithoutClassOf)
{
    // No class_of<p6::MacroRegistered>() is ever written; the static
    // initializer expanded by SPECULO_REGISTER did the registration.
    speculo::Option<const speculo::Class&> found = speculo::Class::for_name("p6::MacroRegistered");
    ASSERT_TRUE(found.is_some());
    EXPECT_TRUE(found.unwrap().is_class());
}

TEST(RegistryTest, AnonymousNamespaceTypeHasNoCanonicalName)
{
    const speculo::Class& cls = speculo::class_of<Anonymous>();
    EXPECT_FALSE(cls.has_canonical_name());

    speculo::register_class(cls);
    EXPECT_TRUE(speculo::Class::for_name(cls.get_qualified_name()).is_none());
}

TEST(RegistryTest, SpeculoImplAccessorIsReflectableAndExcluded)
{
    p6::WithImpl obj{ 5 };
    EXPECT_EQ(obj.get_class(), speculo::class_of<p6::WithImpl>());

    const speculo::Class& cls = speculo::class_of<p6::WithImpl>();
    EXPECT_EQ(cls.get_method("get_value").unwrap().invoke(obj, {}).unwrap().as<int>().unwrap(), 5);
    // The synthesized accessor is plumbing, not a reflectable member.
    EXPECT_TRUE(cls.get_method("get_class").is_err());
}

TEST(RegistryTest, MethodLookupByTypeName)
{
    const speculo::Class& cls = speculo::class_of<p6::Point>();
    (void)speculo::class_of<int>(); // make sure "int" is registered

    speculo::Result<speculo::Method, speculo::Error> by_int = cls.get_method("scale", { "int" });
    ASSERT_TRUE(by_int.is_ok());
    ASSERT_EQ(by_int.unwrap().get_parameter_count(), 1u);
    EXPECT_EQ(by_int.unwrap().get_parameter_types()[0], &speculo::class_of<int>());

    EXPECT_TRUE(cls.get_method("scale", { "double" }).is_err());
    EXPECT_TRUE(cls.get_method("scale", { "Nope" }).is_err());
    EXPECT_TRUE(cls.get_method("missing", { "int" }).is_err());
}

TEST(RegistryTest, DeclaredMethodLookupByTypeName)
{
    const speculo::Class& cls = speculo::class_of<p6::Point>();
    (void)speculo::class_of<int>();

    EXPECT_TRUE(cls.get_declared_method("scale", { "int" }).is_ok());
}

TEST(RegistryTest, ConstructorLookupByTypeName)
{
    const speculo::Class& cls = speculo::class_of<p6::Point>();
    (void)speculo::class_of<int>();

    speculo::Option<speculo::Constructor> two = cls.get_constructor({ "int", "int" });
    ASSERT_TRUE(two.is_some());
    EXPECT_EQ(two.unwrap().get_parameter_count(), 2u);

    EXPECT_TRUE(cls.get_constructor({ "int" }).is_none());
    EXPECT_TRUE(cls.get_constructor({ "int", "Nope" }).is_none());

    // Default constructor is found with an empty name list via the Class* path.
    EXPECT_TRUE(cls.get_constructor().is_some());
}

TEST(RegistryTest, BaseLookupByTypeName)
{
    const speculo::Class& derived = speculo::class_of<p6::Derived>();
    (void)speculo::class_of<p6::Base>(); // make sure "p6::Base" is registered

    speculo::Option<const speculo::Class&> base = derived.get_base("p6::Base");
    ASSERT_TRUE(base.is_some());
    EXPECT_EQ(base.unwrap(), speculo::class_of<p6::Base>());

    EXPECT_TRUE(derived.get_base("p6::Missing").is_none());
}

namespace
{

template<int I>
void hammer(std::atomic<int>& hits)
{
    for (int n = 0; n < 200; ++n)
    {
        // All threads race on the same first-time registration...
        (void)speculo::class_of<p6::Tag<0>>();

        // ...and each thread registers (and then finds) its own type.
        const speculo::Class& cls = speculo::class_of<p6::Tag<I>>();
        speculo::Option<const speculo::Class&> found = speculo::Class::for_name(cls.get_qualified_name());
        if (found.is_some() && found.unwrap() == cls)
        {
            hits.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

template<std::size_t... I>
void spawn_hammer_threads(std::index_sequence<I...>, std::atomic<int>& hits, std::vector<std::thread>& threads)
{
    (threads.emplace_back([&hits] { hammer<static_cast<int>(I)>(hits); }), ...);
}

} // namespace

TEST(RegistryTest, ConcurrentRegistrationAndLookup)
{
    constexpr int kThreads = 8;
    constexpr int kIterations = 200;

    std::atomic<int> hits{ 0 };
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    spawn_hammer_threads(std::make_index_sequence<kThreads>{}, hits, threads);
    for (std::thread& thread : threads)
    {
        thread.join();
    }

    EXPECT_EQ(hits.load(), kThreads * kIterations);
}

// ---------------------------------------------------------------------------
// Error messages name the member, the types involved, and the failing rule
// ---------------------------------------------------------------------------

namespace
{

auto contains_all(const std::string& text, std::initializer_list<std::string_view> parts) -> bool
{
    for (std::string_view part : parts)
    {
        if (!text.contains(part))
        {
            return false;
        }
    }

    return true;
}

} // namespace

TEST(ErrorMessageTest, FieldSetNamesMemberAndBothTypes)
{
    const auto field = speculo::class_of<p6::Point>().get_field("x").unwrap();
    p6::Point point{};
    double wrong = 1.0;

    const speculo::Error error = field.set(point, wrong).unwrap_err();
    EXPECT_EQ(error.kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_TRUE(contains_all(error.message, { "Field::set", "p6::Point::x", "double", "int" })) << error.message;
}

TEST(ErrorMessageTest, ReceiverMismatchNamesBothClasses)
{
    const auto field = speculo::class_of<p6::Point>().get_field("x").unwrap();
    p6::Base other{};

    const speculo::Error error = field.get(other).unwrap_err();
    EXPECT_EQ(error.kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_TRUE(contains_all(error.message, { "Field::get", "p6::Base", "p6::Point" })) << error.message;
}

TEST(ErrorMessageTest, MethodArgumentMismatchNamesIndexAndTypes)
{
    const auto method = speculo::class_of<p6::Point>().get_method("scale").unwrap();
    p6::Point point{};

    double wrong = 2.0;
    const speculo::Error mismatch = method.invoke(point, { wrong }).unwrap_err();
    EXPECT_EQ(mismatch.kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_TRUE(contains_all(mismatch.message, { "Method::invoke", "scale", "argument 0", "double", "int" }))
        << mismatch.message;

    int one = 1;
    const speculo::Error count = method.invoke(point, { one, one }).unwrap_err();
    EXPECT_EQ(count.kind, speculo::ErrorKind::InvalidArgument);
    EXPECT_TRUE(contains_all(count.message, { "scale", "expected 1", "got 2" })) << count.message;
}

TEST(ErrorMessageTest, ConstructorMismatchNamesTheSignature)
{
    const auto ctor = speculo::class_of<p6::Point>().get_constructor<int, int>().unwrap();
    int one = 1;
    double wrong = 1.0;

    const speculo::Error error = ctor.new_instance({ one, wrong }).unwrap_err();
    EXPECT_EQ(error.kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_TRUE(contains_all(error.message, { "Constructor::new_instance", "argument 1", "double", "int" }))
        << error.message;
}

TEST(ErrorMessageTest, LookupFailuresNameTheClassAndTheMember)
{
    const auto& cls = speculo::class_of<p6::Point>();

    const speculo::Error no_method = cls.get_method("nope").unwrap_err();
    EXPECT_EQ(no_method.kind, speculo::ErrorKind::NotFound);
    EXPECT_TRUE(contains_all(no_method.message, { "p6::Point", "nope" })) << no_method.message;

    const speculo::Error no_field = cls.get_field_recursive("nope").unwrap_err();
    EXPECT_EQ(no_field.kind, speculo::ErrorKind::NotFound);
    EXPECT_TRUE(contains_all(no_field.message, { "p6::Point" })) << no_field.message;

    char one = 1;
    const speculo::Error no_ctor = cls.new_instance({ one }).unwrap_err();
    EXPECT_EQ(no_ctor.kind, speculo::ErrorKind::NotFound);
    EXPECT_TRUE(contains_all(no_ctor.message, { "p6::Point", "(char)" })) << no_ctor.message;
}

TEST(ErrorMessageTest, CastAndArrayErrorsCarryTheirOperands)
{
    p6::Point point{};
    const speculo::Error cast =
        speculo::class_of<p6::Point>().cast_to(point, speculo::class_of<p6::Base>()).unwrap_err();
    EXPECT_EQ(cast.kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_TRUE(contains_all(cast.message, { "cast_to", "p6::Point", "p6::Base" })) << cast.message;

    speculo::Object array = std::move(speculo::Array::new_instance(speculo::class_of<int>(), 3).unwrap());
    const speculo::Error range = speculo::Array::get(array.ref(), 5).unwrap_err();
    EXPECT_EQ(range.kind, speculo::ErrorKind::InvalidArgument);
    EXPECT_TRUE(contains_all(range.message, { "index 5", "length 3" })) << range.message;
}

TEST(ErrorMessageTest, ErrorHasAFormatter)
{
    const speculo::Error error{ speculo::ErrorKind::NotFound, "nothing here" };
    EXPECT_EQ(std::format("{}", error), "NotFound: nothing here");
    EXPECT_EQ(std::format("{}", speculo::ErrorKind::ReadOnly), "ReadOnly");
    EXPECT_EQ(std::format("[{:>10}]", speculo::ErrorKind::Ambiguous), "[ Ambiguous]");
}

namespace p6b
{

struct Wide
{
    int sum(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) const
    {
        return a + b + c + d + e + f + g + h + i + j;
    }

    int sum8(int a, int b, int c, int d, int e, int f, int g, int h) const
    {
        return a + b + c + d + e + f + g + h;
    }

    Wide() = default;
    Wide(int, double) {}
};

} // namespace p6b

TEST(ArgumentPointersTest, InlineAndHeapCallsBothWork)
{
    const speculo::Class& cls = speculo::class_of<p6b::Wide>();
    p6b::Wide wide;
    int v[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    // Exactly the inline capacity (8) ...
    const auto eight = cls.get_method("sum8").unwrap();
    EXPECT_EQ(eight.invoke(wide, { v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7] }).unwrap().as<int>().unwrap(), 36);

    // ... and past it (10), which spills to the heap fallback.
    const auto ten = cls.get_method("sum").unwrap();
    EXPECT_EQ(ten.invoke(wide, { v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9] })
                  .unwrap().as<int>().unwrap(),
              55);
}

TEST(RegistryTest, ConstructorLookupByTypeNameResolvesNamesOnce)
{
    const speculo::Class& cls = speculo::class_of<p6b::Wide>();

    // Names resolve through the registry, which only knows types that were used.
    (void)speculo::class_of<int>();
    (void)speculo::class_of<double>();

    EXPECT_TRUE(cls.get_constructor({ "int", "double" }).is_some());
    EXPECT_TRUE(cls.get_constructor({ "int" }).is_none());
    EXPECT_TRUE(cls.get_constructor({ "int", "no::Such" }).is_none());
    EXPECT_TRUE(cls.get_declared_constructor({ "int", "double" }).is_some());
}

// ---------------------------------------------------------------------------
// Standard library members (std::string et al.) can be reflected
// ---------------------------------------------------------------------------

namespace p6c
{

struct Person
{
    int age = 0;
    std::string name;
    std::vector<std::string> tags;

    std::string greet(const std::string& prefix) const { return prefix + name; }
};

struct Anon
{
    int tag = 0;
    union
    {
        int   as_int;
        float as_float;
    };
};

} // namespace p6c

TEST(StdLibraryTest, StructWithStringMembersIsReflectable)
{
    const speculo::Class& cls = speculo::class_of<p6c::Person>();
    EXPECT_EQ(cls.get_fields().size(), 3u);

    p6c::Person person;
    person.name = "Ada";

    const auto name = cls.get_field("name").unwrap();
    EXPECT_EQ(name.get_type(), speculo::class_of<std::string>());
    EXPECT_EQ(name.get(person).unwrap().as<std::string>().unwrap(), "Ada");

    std::string grace = "Grace";
    ASSERT_TRUE(name.set(person, grace).is_ok());
    EXPECT_EQ(person.name, "Grace");

    auto name_ref = name.get_ref(person);
    ASSERT_TRUE(name_ref.is_ok());
    EXPECT_EQ(name_ref.unwrap().type(), speculo::class_of<std::string>());
    name_ref.unwrap().as<std::string>().unwrap() = "Lin";
    EXPECT_EQ(person.name, "Lin");

    person.tags = { "a", "b" };
    const auto tags = cls.get_field("tags").unwrap();
    EXPECT_EQ(tags.get_type(), speculo::class_of<std::vector<std::string>>());
    EXPECT_EQ(tags.get(person).unwrap().as<std::vector<std::string>>().unwrap().size(), 2u);
    std::vector<std::string> renamed{ "x" };
    ASSERT_TRUE(tags.set(person, renamed).is_ok());
    EXPECT_EQ(person.tags, renamed);

    const auto greet = cls.get_method("greet").unwrap();
    std::string hello = "Hello, ";
    EXPECT_EQ(greet.invoke(person, { hello }).unwrap().as<std::string>().unwrap(), "Hello, Lin");
}

TEST(StdLibraryTest, StdTypesKeepIdentityWithoutMembers)
{
    // A standard type is still a real Class (so a field or parameter can name it,
    // and Object can copy and destroy a value), but its own members are not expanded.
    const speculo::Class& text = speculo::class_of<std::string>();
    EXPECT_EQ(text.get_size(), sizeof(std::string));
    EXPECT_EQ(text.get_alignment(), alignof(std::string));
    EXPECT_FALSE(text.get_qualified_name().empty());
    EXPECT_TRUE(text.get_fields().empty());
    EXPECT_TRUE(text.get_declared_fields().empty());
    EXPECT_TRUE(text.get_methods().empty());
    EXPECT_TRUE(text.get_declared_methods().empty());
    EXPECT_TRUE(text.get_constructors().empty());
    EXPECT_TRUE(text.get_declared_constructors().empty());
    EXPECT_TRUE(text.get_bases().empty());

    std::string value = "reflection";
    const speculo::Object boxed = speculo::Object::from_value(value).unwrap();
    EXPECT_EQ(boxed.as<std::string>().unwrap(), "reflection");

    const speculo::Class& vec = speculo::class_of<std::vector<std::string>>();
    EXPECT_TRUE(vec.get_methods().empty());
    EXPECT_TRUE(vec.get_fields().empty());
    EXPECT_EQ(vec.get_size(), sizeof(std::vector<std::string>));

    const speculo::Class& order = speculo::class_of<std::endian>();
    EXPECT_TRUE(order.is_enum());
    EXPECT_TRUE(order.get_enumerators().empty());
    EXPECT_EQ(order.get_size(), sizeof(std::endian));
}

TEST(StdLibraryTest, AnonymousUnionMembersAreReflectable)
{
    // The members of an anonymous union live on the (unnamed) union type, which is
    // listed as one unnamed field of the enclosing class.
    const speculo::Class& cls = speculo::class_of<p6c::Anon>();
    EXPECT_EQ(cls.get_fields().size(), 2u);
    EXPECT_EQ(cls.get_field("tag").unwrap().get_offset(), 0u);
}

// ---------------------------------------------------------------------------
// C++26 annotations (P3394, [[= expr]])
// ---------------------------------------------------------------------------

namespace p6ann
{

struct Tag
{
};

struct Label
{
    int rank = 0;
};

struct Rename
{
    const char* name = nullptr;
};

struct [[= Tag{}]]
Base
{
};

struct [[= Label{ 9 }]]
Derived : [[= Tag{}]] Base
{
    [[= Label{ 3 }]]
    [[= 1, = 2]]
    int x = 0;

    [[= "literal"]]
    int text = 0;

    [[= std::array<char, 8>{ "literal" }]]
    int chars = 0;

    [[= Rename{ "hidden" }]]
    int renamed = 0;

    [[= Tag{}]]
    Derived() = default;

    [[nodiscard]]
    int plain() const { return x; }

    void scale([[= Tag{}]] int factor) const { (void)factor; }
};

enum class Color
{
    Red [[= 7]],
    Green,
};

} // namespace p6ann

TEST(AnnotationTest, ClassFieldMethodAndParameter)
{
    const speculo::Class& cls = speculo::class_of<p6ann::Derived>();
    EXPECT_EQ(cls.get_annotation<p6ann::Label>().unwrap().rank, 9);
    EXPECT_TRUE(cls.get_annotation<p6ann::Tag>().is_none());

    const speculo::Class& as_const = speculo::class_of<const p6ann::Derived>();
    EXPECT_EQ(as_const.get_annotation<p6ann::Label>().unwrap().rank, 9);
    EXPECT_TRUE(speculo::class_of<p6ann::Derived&>().get_annotations().empty());
    EXPECT_TRUE(speculo::class_of<p6ann::Derived*>().get_annotations().empty());

    const speculo::Field x = cls.get_field("x").unwrap();
    EXPECT_EQ(x.get_annotations().size(), 3u);
    EXPECT_EQ(x.get_annotation<p6ann::Label>().unwrap().rank, 3);
    EXPECT_EQ(x.get_annotation<int>().unwrap(), 1);
    EXPECT_EQ(x.get_annotations()[0].get_type(), speculo::class_of<p6ann::Label>());
    EXPECT_EQ(x.get_annotations()[1].get<int>().unwrap(), 1);
    EXPECT_EQ(x.get_annotations()[2].get<int>().unwrap(), 2);

    const speculo::Object boxed = x.get_annotations()[1].value().unwrap();
    EXPECT_EQ(boxed.as<int>().unwrap(), 1);

    const speculo::Method scale = cls.get_method("scale").unwrap();
    EXPECT_TRUE(scale.get_annotations().empty());
    EXPECT_TRUE(scale.get_parameter_annotation<p6ann::Tag>(0).is_some());

    const speculo::Method plain = cls.get_method("plain").unwrap();
    EXPECT_TRUE(plain.get_annotations().empty());

    const speculo::Constructor ctor = cls.get_constructor().unwrap();
    EXPECT_TRUE(ctor.get_annotation<p6ann::Tag>().is_some());
}

TEST(AnnotationTest, UnreadablePointerValuesStayListed)
{
    const speculo::Class& cls = speculo::class_of<p6ann::Derived>();

    const speculo::Field text = cls.get_field("text").unwrap();
    ASSERT_EQ(text.get_annotations().size(), 1u);
    const speculo::Annotation& literal = text.get_annotations()[0];
    EXPECT_EQ(literal.get_type(), speculo::class_of<const char*>());
    EXPECT_FALSE(literal.is_readable());
    EXPECT_TRUE(literal.get<const char*>().is_none());
    EXPECT_EQ(literal.value().unwrap_err().kind, speculo::ErrorKind::NotSupported);

    const speculo::Field renamed = cls.get_field("renamed").unwrap();
    const speculo::Annotation& rename = renamed.get_annotations()[0];
    EXPECT_EQ(rename.get_type(), speculo::class_of<p6ann::Rename>());
    EXPECT_FALSE(rename.is_readable());
    EXPECT_TRUE(rename.get<p6ann::Rename>().is_none());

    const speculo::Field chars = cls.get_field("chars").unwrap();
    const speculo::Annotation& array = chars.get_annotations()[0];
    EXPECT_TRUE(array.is_readable());
    const std::array<char, 8>& stored = array.get<std::array<char, 8>>().unwrap();
    EXPECT_EQ(stored[0], 'l');
    const speculo::Object boxed = array.value().unwrap();
    EXPECT_EQ((boxed.as<std::array<char, 8>>().unwrap()[0]), 'l');
}

TEST(AnnotationTest, BasesEnumeratorsAndNoInheritance)
{
    const speculo::Class& derived = speculo::class_of<p6ann::Derived>();
    const speculo::BaseClass& base = derived.get_bases()[0];
    EXPECT_EQ(base.type, &speculo::class_of<p6ann::Base>());
    EXPECT_TRUE(base.get_annotation<p6ann::Tag>().is_some());
    EXPECT_TRUE(speculo::class_of<p6ann::Base>().get_annotation<p6ann::Tag>().is_some());
    EXPECT_TRUE(derived.get_annotation<p6ann::Tag>().is_none());

    const speculo::Class& color = speculo::class_of<p6ann::Color>();
    EXPECT_TRUE(color.get_annotations().empty());
    const speculo::Enumerator red = color.get_enumerators()[0];
    EXPECT_EQ(red.get_name(), "Red");
    EXPECT_EQ(red.get_annotation<int>().unwrap(), 7);
    EXPECT_TRUE(color.get_enumerators()[1].get_annotations().empty());
}
