#include <speculo/macros.h>

#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/registry_fixtures.h"

namespace p6
{

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

} // namespace p6

SPECULO_REGISTER(p6::MacroRegistered);

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
    // Independent of test order: method lookup resolves parameter names through
    // the global type registry, so materialize the name this test relies on.
    (void)speculo::class_of<unsigned long long>();
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
