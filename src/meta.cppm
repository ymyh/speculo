// speculo:meta —— 编译期与运行期共用的「成员收集器」分区。
//
// 这里只依赖 <meta> 与 std，不 import :object，因此 :object 与 :compile 都可以
// 依赖它而不形成强连通分量（design.md §14）。收集规则（哪些成员可反射、访问
// 级别如何过滤、std 类型如何浅物化）只有这一份实现，运行期物化与编译期遍历
// 都消费它，避免两边漂移。
module;

#include <meta>

export module speculo:meta;

import std;

export namespace speculo
{

/**
 * @brief Structural string usable as a non-type template parameter
 *
 * `std::string_view` is not a structural type in libstdc++ (its size member is
 * private), so this small wrapper enables compile-time constant names such as
 * `cls.get_method<"scale", int>()`.
 */
template<std::size_t N>
struct FixedString
{
    char data[N]{};

    constexpr FixedString(const char (&str)[N])
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            data[i] = str[i];
        }
    }

    [[nodiscard]] constexpr operator std::string_view() const noexcept
    {
        return std::string_view(data, N - 1);
    }
};

template<std::size_t N>
FixedString(const char (&)[N]) -> FixedString<N>;

} // namespace speculo

export namespace speculo::detail
{

/**
 * @brief Annotation attached to SPECULO_IMPL(_VIRTUAL)'s synthesized accessor
 *
 * The accessor (`get_class()`) is reflection plumbing: it exists to bridge a
 * concrete type to its Class. Reflecting it would drag in `const Class&` (and
 * hence `Class`'s whole signature closure, which reaches crab_cpp's move-only
 * variants), so annotated methods are excluded from the member tables. The
 * annotation is a plain `[[= detail::reflect_impl_tag{}]]` attribute.
 */
struct reflect_impl_tag
{
};

/**
 * @brief Opt-in flag set by SPECULO_DEEP(T) (api_design §14)
 *
 * Standard-library types are materialized shallowly by default: names, size and
 * the value operations an Object needs, but no member tables. `SPECULO_DEEP(T)`
 * specializes this variable template for one specialization, which makes the
 * collectors treat that type like a user type.
 *
 * The macro expands to an explicit specialization at namespace scope, so it must
 * appear before the first `class_of<T>()` (or anything else that materializes T)
 * in the translation unit; repeating it in one TU is a redefinition, while
 * identical specializations in different translation units are fine.
 */
template<typename T>
inline constexpr bool deep_materialized = false;

} // namespace speculo::detail

namespace speculo::detail
{

namespace meta = std::meta;

/// Public-only member access context (invoked at namespace scope).
consteval auto public_context() -> meta::access_context
{
    return meta::access_context::current();
}

/// All-access member access context (only used by materialization inside the module).
consteval auto unchecked_context() -> meta::access_context
{
    return meta::access_context::unchecked();
}

/// Reflection of T (bundles are only instantiated with canonical types).
template<typename T>
consteval auto info_of() -> meta::info
{
    return ^^T;
}

/// Whether C names an unqualified class/union (the only types whose members are
/// materialized; cv-qualified class types only carry identity + decomposition).
consteval auto is_unqualified_class(meta::info C) -> bool
{
    return (meta::is_class_type(C) || meta::is_union_type(C))
        && !meta::is_const_type(C) && !meta::is_volatile_type(C);
}

// Whether the entity is declared (at any depth) inside namespace std.
consteval auto is_in_std_namespace(meta::info entity) -> bool
{
    for (meta::info scope = meta::parent_of(entity); scope != ^^::; scope = meta::parent_of(scope))
    {
        if (meta::is_namespace(scope) && meta::has_identifier(scope) && meta::identifier_of(scope) == "std")
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Whether `type` is a standard-library type (namespace `std`, any depth)
 *
 * Such types are materialized shallowly: name, size, alignment, and the value
 * operations an `Object` needs (copy, destroy, assign). Their fields, methods,
 * constructors, bases, and enumerators are not enumerated, so holding a
 * `std::string` does not pull `basic_string`'s signature closure into the
 * program. A user type that *has* a `std::` member, or a function whose
 * signature *uses* a `std::` type, still reflects that member or function;
 * only the standard type itself is not expanded.
 */
consteval auto is_std_type(meta::info type) -> bool
{
    if (!meta::has_parent(type))
    {
        return false;
    }

    return is_in_std_namespace(type);
}

/**
 * @brief Whether SPECULO_DEEP(T) opted this type into full materialization
 * @note The flag lives in the same module, so the collectors and the macro see
 *       the same specialization; only the type itself is affected (members are
 *       not automatically deep).
 */
template<meta::info C>
consteval auto is_deep_type() -> bool
{
    if constexpr (is_unqualified_class(C))
    {
        return deep_materialized<typename[: C :]>;
    }
    else
    {
        return false;
    }
}

/// Whether a type's member tables are materialized: user classes always,
/// std types only after SPECULO_DEEP.
template<meta::info C>
consteval auto expandable_type() -> bool
{
    return is_unqualified_class(C) && (!is_std_type(C) || is_deep_type<C>());
}

/**
 * @brief The name used for an entity in every member table
 *
 * Declarators without an identifier (destructors, conversion operators) fall
 * back to the compiler's display string, exactly as the runtime descriptors do.
 */
consteval auto member_name(meta::info entity) -> std::string_view
{
    return meta::has_identifier(entity) ? meta::identifier_of(entity) : meta::display_string_of(entity);
}

consteval auto is_reflect_impl_annotation(meta::info annotation) -> bool
{
    return meta::is_annotation(annotation)
        && meta::remove_cv(meta::type_of(annotation)) == ^^reflect_impl_tag;
}

// Whether the type is a specialization of std::allocator.
consteval auto is_std_allocator_specialization(meta::info type) -> bool
{
    return meta::has_template_arguments(type) && meta::template_of(type) == ^^std::allocator;
}

// A non-template member function with an explicit object parameter
// (`void f(this C& self)`). `parameters_of` lists the object parameter first, so
// the reflected parameter list would be wrong and `member_function_pointer`
// would build a pointer type `extract` rejects; such members are not enumerated.
// (Templates -- including deducing-this ones -- are not `is_function` at all.)
consteval auto has_explicit_object_parameter(meta::info function) -> bool
{
    const auto parameters = meta::parameters_of(function);
    return !parameters.empty() && meta::is_explicit_object_parameter(parameters[0]);
}

consteval auto is_reflectable_method(meta::info member) -> bool
{
    // Function templates (and conversion / operator templates) are separate
    // reflections for which is_function() is false.
    if (!meta::is_function(member))
    {
        return false;
    }

    // GCC instantiates the definition of every function whose reflection is held
    // in a constant (even without calling it). Non-public members of standard
    // library types are implementation detail and may be ill-formed for a given
    // instantiation (tuple's const _M_swap), which would be a hard error inside
    // class_of<T>() of any type holding e.g. a std::unique_ptr. Never touch them.
    if (!meta::is_public(member) && is_in_std_namespace(meta::parent_of(member)))
    {
        return false;
    }

    // std::allocator's members are not reflected. Taking their addresses (which
    // the invoker thunks do) needs out-of-line definitions that `extern template
    // class std::allocator<char>` promises but libstdc++ does not provide for
    // `allocate` / `deallocate` / `allocate_at_least` -- with `import std` the
    // program then fails to link. std::string's allocator base would otherwise
    // drag them into every class that has a std::string member.
    if (is_std_allocator_specialization(meta::parent_of(member)))
    {
        return false;
    }

    // SPECULO_IMPL(_VIRTUAL)'s synthesized accessor is excluded (see
    // reflect_impl_tag): reflecting it would pull in `const Class&`.
    for (auto annotation : meta::annotations_of(member))
    {
        if (is_reflect_impl_annotation(annotation))
        {
            return false;
        }
    }

    if (meta::is_constructor(member))
    {
        return false;
    }

    if (meta::is_destructor(member))
    {
        // Only user-declared destructors are surfaced (every class has an
        // implicitly-declared one, which would otherwise pollute every table).
        return meta::is_user_declared(member);
    }

    if (has_explicit_object_parameter(member))
    {
        return false;
    }

    // Of the remaining special members only copy / move assignment is listed, and
    // only when user-declared (every class has implicit ones that would pollute
    // all tables). Deleted functions stay in the table too: they cannot be
    // `extract`ed, so they get a null invoker (see make_method_desc).
    if (meta::is_copy_assignment(member) || meta::is_move_assignment(member))
    {
        return meta::is_user_declared(member);
    }

    return !meta::is_special_member_function(member);
}

// ── The single collection rule ──
//
// `field_members<C>()` / `method_members<C>()` are the public view; the
// `declared_*` twins are the all-access view used by get_declared_*(). Every
// count/at accessor below funnels through them so the two layers cannot drift.

template<meta::info C>
consteval auto field_members() -> std::vector<meta::info>
{
    std::vector<meta::info> result;
    if constexpr (expandable_type<C>())
    {
        // Declaration order inside each group; non-static members come first so
        // the order matches Class::get_fields() (api_design §1).
        for (meta::info member : meta::nonstatic_data_members_of(C, public_context()))
        {
            result.push_back(member);
        }
        for (meta::info member : meta::static_data_members_of(C, public_context()))
        {
            result.push_back(member);
        }
    }
    return result;
}

template<meta::info C>
consteval auto declared_field_members() -> std::vector<meta::info>
{
    std::vector<meta::info> result;
    if constexpr (expandable_type<C>())
    {
        for (meta::info member : meta::nonstatic_data_members_of(C, unchecked_context()))
        {
            result.push_back(member);
        }
        for (meta::info member : meta::static_data_members_of(C, unchecked_context()))
        {
            result.push_back(member);
        }
    }
    return result;
}

template<meta::info C>
consteval auto method_members() -> std::vector<meta::info>
{
    std::vector<meta::info> result;
    if constexpr (expandable_type<C>())
    {
        for (meta::info member : meta::members_of(C, public_context()))
        {
            if (is_reflectable_method(member))
            {
                result.push_back(member);
            }
        }
    }
    return result;
}

template<meta::info C>
consteval auto declared_method_members() -> std::vector<meta::info>
{
    std::vector<meta::info> result;
    if constexpr (expandable_type<C>())
    {
        for (meta::info member : meta::members_of(C, unchecked_context()))
        {
            if (is_reflectable_method(member))
            {
                result.push_back(member);
            }
        }
    }
    return result;
}

template<meta::info C>
consteval auto public_field_count() -> std::size_t
{
    return field_members<C>().size();
}

template<meta::info C, std::size_t I>
consteval auto public_field_member() -> meta::info
{
    return field_members<C>()[I];
}

template<meta::info C>
consteval auto declared_field_count() -> std::size_t
{
    return declared_field_members<C>().size();
}

template<meta::info C, std::size_t I>
consteval auto declared_field_member() -> meta::info
{
    return declared_field_members<C>()[I];
}

template<meta::info C>
consteval auto public_method_count() -> std::size_t
{
    return method_members<C>().size();
}

template<meta::info C, std::size_t I>
consteval auto public_method_member() -> meta::info
{
    return method_members<C>()[I];
}

template<meta::info C>
consteval auto declared_method_count() -> std::size_t
{
    return declared_method_members<C>().size();
}

template<meta::info C, std::size_t I>
consteval auto declared_method_member() -> meta::info
{
    return declared_method_members<C>()[I];
}

// ── std::array views consumed by the compile-time half and by materialization ──

template<meta::info C>
consteval auto collect_field_array() -> std::array<meta::info, public_field_count<C>()>
{
    return [&]<std::size_t... I>(std::index_sequence<I...>) -> std::array<meta::info, sizeof...(I)>
    {
        return { public_field_member<C, I>()... };
    }(std::make_index_sequence<public_field_count<C>()>{});
}

template<meta::info C>
consteval auto collect_method_array() -> std::array<meta::info, public_method_count<C>()>
{
    return [&]<std::size_t... I>(std::index_sequence<I...>) -> std::array<meta::info, sizeof...(I)>
    {
        return { public_method_member<C, I>()... };
    }(std::make_index_sequence<public_method_count<C>()>{});
}

template<meta::info C>
consteval auto collect_declared_field_array() -> std::array<meta::info, declared_field_count<C>()>
{
    return [&]<std::size_t... I>(std::index_sequence<I...>) -> std::array<meta::info, sizeof...(I)>
    {
        return { declared_field_member<C, I>()... };
    }(std::make_index_sequence<declared_field_count<C>()>{});
}

template<meta::info C>
consteval auto collect_declared_method_array() -> std::array<meta::info, declared_method_count<C>()>
{
    return [&]<std::size_t... I>(std::index_sequence<I...>) -> std::array<meta::info, sizeof...(I)>
    {
        return { declared_method_member<C, I>()... };
    }(std::make_index_sequence<declared_method_count<C>()>{});
}

template<typename T>
inline constexpr std::size_t collected_field_count = public_field_count<info_of<std::remove_cvref_t<T>>()>();

template<typename T>
inline constexpr std::size_t collected_method_count = public_method_count<info_of<std::remove_cvref_t<T>>()>();

template<typename T>
inline constexpr std::size_t collected_declared_field_count = declared_field_count<info_of<std::remove_cvref_t<T>>()>();

template<typename T>
inline constexpr std::size_t collected_declared_method_count = declared_method_count<info_of<std::remove_cvref_t<T>>()>();

/**
 * @brief Public data members of T, in Class::get_fields() order
 *
 * Non-static members first (declaration order), then static members
 * (declaration order). cv / reference qualifiers are normalized away: a field
 * list belongs to the type, not to a particular cv view.
 */
template<typename T>
consteval auto collect_fields() -> std::array<meta::info, collected_field_count<T>>
{
    return collect_field_array<info_of<std::remove_cvref_t<T>>()>();
}

/// Public member functions of T, in Class::get_methods() order.
template<typename T>
consteval auto collect_methods() -> std::array<meta::info, collected_method_count<T>>
{
    return collect_method_array<info_of<std::remove_cvref_t<T>>()>();
}

/// All data members of T (any access level), in get_declared_fields() order.
template<typename T>
consteval auto collect_declared_fields() -> std::array<meta::info, collected_declared_field_count<T>>
{
    return collect_declared_field_array<info_of<std::remove_cvref_t<T>>()>();
}

/// All member functions of T (any access level), in get_declared_methods() order.
template<typename T>
consteval auto collect_declared_methods() -> std::array<meta::info, collected_declared_method_count<T>>
{
    return collect_declared_method_array<info_of<std::remove_cvref_t<T>>()>();
}

} // namespace speculo::detail
