// Probe: Phase 4 materialization facilities available in GCC 16.2.
#include <meta>
#include <print>
#include <string_view>
#include <cstdint>

namespace meta = std::meta;

struct Plain { int x = 0; };

struct Rich
{
    int a = 0;
    Rich() = default;
    explicit Rich(int v) : a(v) {}
    Rich(const Rich&) = default;
    Rich(Rich&&) = default;
    Rich& operator=(const Rich&) = default;
    ~Rich() = default;

    int operator+(int rhs) const { return a + rhs; }
    operator int() const { return a; }
};

struct NoCtor { int a = 1; int b = 2; };
struct ImplicitDtor { int a = 1; };

consteval auto count_members(meta::info C) -> std::size_t
{
    return meta::members_of(C, meta::access_context::unchecked()).size();
}

consteval auto count_ctors(meta::info C) -> std::size_t
{
    std::size_t n = 0;
    for (auto m : meta::members_of(C, meta::access_context::unchecked()))
        if (meta::is_function(m) && meta::is_constructor(m)) ++n;
    return n;
}

consteval auto count_dtors(meta::info C) -> std::size_t
{
    std::size_t n = 0;
    for (auto m : meta::members_of(C, meta::access_context::unchecked()))
        if (meta::is_function(m) && meta::is_destructor(m)) ++n;
    return n;
}

consteval auto count_user_declared_ctors(meta::info C) -> std::size_t
{
    std::size_t n = 0;
    for (auto m : meta::members_of(C, meta::access_context::unchecked()))
        if (meta::is_function(m) && meta::is_constructor(m) && meta::is_user_declared(m)) ++n;
    return n;
}

consteval auto has_operator_plus(meta::info C) -> bool
{
    for (auto m : meta::members_of(C, meta::access_context::unchecked()))
        if (meta::is_function(m) && meta::is_operator_function(m) && !meta::is_conversion_function(m))
            if (meta::symbol_of(meta::operator_of(m)) == "+") return true;
    return false;
}

consteval auto has_conversion(meta::info C) -> bool
{
    for (auto m : meta::members_of(C, meta::access_context::unchecked()))
        if (meta::is_function(m) && meta::is_conversion_function(m)) return true;
    return false;
}

consteval auto explicit_int_ctor(meta::info C) -> bool
{
    for (auto m : meta::members_of(C, meta::access_context::unchecked()))
        if (meta::is_function(m) && meta::is_constructor(m) && meta::is_explicit(m)) return true;
    return false;
}

consteval auto copy_ctor_detect(meta::info C) -> bool
{
    if constexpr (requires { meta::is_copy_constructor(meta::members_of(C, meta::access_context::unchecked())[0]); })
        return true;
    else
        return false;
}

consteval auto dtor_has_type(meta::info C) -> bool
{
    for (auto m : meta::members_of(C, meta::access_context::unchecked()))
        if (meta::is_function(m) && meta::is_destructor(m))
        {
            try { (void)meta::type_of(m); return true; }
            catch (...) { return false; }
        }
    return false;
}

consteval auto ctor_param0_name(meta::info C) -> std::string_view
{
    for (auto m : meta::members_of(C, meta::access_context::unchecked()))
        if (meta::is_function(m) && meta::is_constructor(m) && meta::parameters_of(m).size() == 1)
            return meta::display_string_of(meta::type_of(meta::parameters_of(m)[0]));
    return "<none>";
}

int main()
{
    std::println("members Plain={} Rich={} NoCtor={}", count_members(^^Plain), count_members(^^Rich), count_members(^^NoCtor));
    std::println("ctors Plain={} Rich={} NoCtor={}", count_ctors(^^Plain), count_ctors(^^Rich), count_ctors(^^NoCtor));
    std::println("user ctors Plain={} Rich={} NoCtor={}", count_user_declared_ctors(^^Plain), count_user_declared_ctors(^^Rich), count_user_declared_ctors(^^NoCtor));
    std::println("dtors Plain={} Rich={} ImplicitDtor={}", count_dtors(^^Plain), count_dtors(^^Rich), count_dtors(^^ImplicitDtor));
    std::println("Rich op+={} conv={} explicit={}", has_operator_plus(^^Rich), has_conversion(^^Rich), explicit_int_ctor(^^Rich));
    std::println("has is_copy_constructor predicate = {}", copy_ctor_detect(^^Rich));
    std::println("Rich dtor has type = {}", dtor_has_type(^^Rich));
    std::println("Rich 1-param ctor type = [{}]", ctor_param0_name(^^Rich));
    std::println("Plain dtor has type = {}", dtor_has_type(^^Plain));
    return 0;
}
