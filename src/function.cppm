module;

#include <meta>
#include <new>

export module speculo:function;

import std;
import crab_cpp;
import :error;
import :modifier;
import :descriptor;
import :object;

export namespace speculo
{

/**
 * @brief Handle describing one namespace-scope function
 *
 * Java has no counterpart: a free function does not belong to a class. The
 * handle is obtained by naming the function (`function_of`), never by enumerating
 * a namespace. Invocation takes no receiver; argument checking matches Method.
 */
class Function
{
public:
    constexpr Function() noexcept = default;

    [[nodiscard]] auto get_name() const -> std::string_view { return m_desc->name; }
    [[nodiscard]] auto get_namespace() const -> std::string_view { return m_desc->namespace_name; }
    [[nodiscard]] auto get_qualified_name() const -> std::string_view { return m_desc->qualified_name; }
    [[nodiscard]] auto get_return_type() const -> const Class&
    {
        return m_desc->return_type != nullptr ? *m_desc->return_type->self : class_of<void>();
    }
    [[nodiscard]] auto get_parameter_count() const -> std::size_t { return m_desc->parameter_count; }

    [[nodiscard]] auto get_parameter_types() const -> std::span<const Class* const>
    {
        return std::span<const Class* const>(m_desc->parameter_types, m_desc->parameter_count);
    }

    [[nodiscard]] auto modifiers() const -> Modifier { return Modifier(m_desc->modifiers); }
    [[nodiscard]] auto is_noexcept() const -> bool { return modifiers().is_noexcept(); }
    [[nodiscard]] auto is_deleted() const -> bool { return modifiers().is_deleted(); }
    [[nodiscard]] auto is_operator() const -> bool { return m_desc->is_operator; }
    [[nodiscard]] auto is_variadic() const -> bool { return m_desc->is_variadic; }

    /// Whether invoke() can run this function (false for deleted, C-variadic, and
    /// signatures the thunk cannot express, such as a move-only by-value parameter).
    [[nodiscard]] auto is_invokable() const -> bool { return m_desc->invoke != nullptr; }

    [[nodiscard]] auto returns_reference() const -> bool
    {
        return m_desc->return_type != nullptr && m_desc->return_type->reference_kind != RefQualifier::None;
    }

    [[nodiscard]] auto is_ref_invokable() const -> bool { return m_desc->invoke_ref != nullptr; }

    /// False for internal linkage (`static`, anonymous namespace): the function can
    /// still be invoked, but Function::for_name cannot name it.
    [[nodiscard]] auto has_canonical_name() const -> bool { return m_desc->has_canonical_name; }

    /**
     * @brief Whether this is one instantiation of a function template
     * @note `function_of<id<int>>()` is a specialization; there is deliberately no
     *       get_primary_template(): an uninstantiated function template has no
     *       parameter types, so it cannot be described by a Function (see
     *       @.agents/api_design.md §1.2 and @.agents/review.md「Phase 9」).
     */
    [[nodiscard]] auto is_template_specialization() const -> bool { return m_desc->is_template_specialization; }

    /// Template arguments in declaration order, defaulted arguments included.
    [[nodiscard]] auto get_template_arguments() const -> std::span<const TemplateArgument>
    {
        return std::span<const TemplateArgument>(static_cast<const TemplateArgument*>(m_desc->template_arguments),
                                                 m_desc->template_argument_count);
    }

    /// The argument at `index` when it is a *type* argument; None otherwise.
    [[nodiscard]] auto get_template_argument_type(std::size_t index) const -> Option<const Class&>
    {
        const std::span<const TemplateArgument> arguments = get_template_arguments();
        if (index >= arguments.size() || arguments[index].kind() != TemplateArgumentKind::Type)
        {
            return None{};
        }

        return arguments[index].as_type();
    }

    /// Registry key. The compiler's display string, with " noexcept" appended when
    /// the function is noexcept (the display string itself leaves that out).
    [[nodiscard]] auto get_signature() const -> std::string_view { return m_desc->signature; }

    [[nodiscard]] auto get_annotations() const -> std::span<const Annotation>
    {
        return detail::annotation_span(m_desc->annotations, m_desc->annotation_count);
    }

    template<typename T>
    [[nodiscard]] auto get_annotation() const -> Option<const T&>
    {
        return detail::first_annotation<T>(get_annotations());
    }

    /// Annotations on parameter `index` (same order as get_parameter_types()).
    /// An out-of-range index is a precondition failure.
    [[nodiscard]] auto get_parameter_annotations(std::size_t index) const -> std::span<const Annotation>
    {
        if (index >= m_desc->parameter_count)
        {
            panic("speculo: Function::get_parameter_annotations index is out of range");
        }

        const auto* spans = static_cast<const Annotation* const*>(m_desc->parameter_annotations);
        return std::span<const Annotation>(spans[index], m_desc->parameter_annotation_counts[index]);
    }

    template<typename T>
    [[nodiscard]] auto get_parameter_annotation(std::size_t index) const -> Option<const T&>
    {
        return detail::first_annotation<T>(get_parameter_annotations(index));
    }

    /**
     * @brief Calls the function, boxing the return value (empty Object for void)
     *
     * The boxed value is the `remove_cvref` of the declared return type. A function
     * returning `T&` yields a copy of the referent; use invoke_ref() for the referent
     * itself. Default arguments are not filled in.
     */
    [[nodiscard]] auto invoke(Args args = {}) const -> Outcome<Object>
    {
        const Status checked = check_call("Function::invoke", args);
        if (checked.is_err())
        {
            return checked.unwrap_err();
        }

        if (m_desc->invoke == nullptr)
        {
            const char* reason = is_deleted() ? "the function is deleted"
                                 : is_variadic() ? "a C variadic function cannot be called through this handle"
                                 : is_ref_invokable() ? "the referenced result is not copyable (use invoke_ref())"
                                                      : "the function is not available";
            return Error{ErrorKind::NotSupported, std::format("{}: {}", context("Function::invoke"), reason)};
        }

        const detail::ArgumentPointers pointers(args);
        void* result = m_desc->invoke(nullptr, pointers.data());
        if (result == nullptr)
        {
            return Object{};
        }

        return detail::Access::adopt_object(result, m_desc->return_type->decayed);
    }

    /**
     * @brief Calls a reference-returning function and views the referent (no copy)
     *
     * The view's type() is the referent's object type. It is const / volatile exactly
     * when the referenced type is. The view lives only as long as the reference the
     * callee returned. Functions that do not return a reference are NotSupported.
     */
    [[nodiscard]] auto invoke_ref(Args args = {}) const -> Outcome<ObjectRef>
    {
        const Status checked = check_call("Function::invoke_ref", args);
        if (checked.is_err())
        {
            return checked.unwrap_err();
        }

        if (!returns_reference())
        {
            return Error{ErrorKind::NotSupported,
                         std::format("{}: the function does not return a reference; use invoke()",
                                     context("Function::invoke_ref"))};
        }

        if (m_desc->invoke_ref == nullptr)
        {
            return Error{ErrorKind::NotSupported,
                         std::format("{}: {}", context("Function::invoke_ref"),
                                     is_deleted() ? "the function is deleted" : "the function is not available")};
        }

        const detail::ArgumentPointers pointers(args);
        const void* address = m_desc->invoke_ref(nullptr, pointers.data());
        const detail::TypeDescriptor* referent = m_desc->return_type->referenced;

        return detail::Access::view_object(address, m_desc->return_type->decayed, referent->is_const_qualified,
                                           referent->is_volatile_qualified);
    }

    [[nodiscard]] auto operator==(const Function& other) const noexcept -> bool { return m_desc == other.m_desc; }
    [[nodiscard]] auto operator!=(const Function& other) const noexcept -> bool { return m_desc != other.m_desc; }

    /// Looks up a function by the exact string get_signature() returns.
    [[nodiscard]] static auto for_name(std::string_view signature) -> Option<const Function&>;

    /// Every registered signature, sorted. For diagnostics.
    [[nodiscard]] static auto registered_signatures() -> std::vector<std::string_view>;

private:
    [[nodiscard]] auto check_call(std::string_view operation, const Args& args) const -> Status
    {
        const std::size_t count = m_desc->parameter_count;
        if (args.size() != count)
        {
            return Error{ErrorKind::InvalidArgument,
                         std::format("{}: expected {} argument(s), got {}", context(operation), count, args.size())};
        }

        for (std::size_t i = 0; i < count; ++i)
        {
            if (!detail::accepts_argument(m_desc->parameter_types[i], m_desc->parameter_decays[i], args[i], true))
            {
                return detail::argument_mismatch(context(operation), i, m_desc->parameter_types[i],
                                                 m_desc->parameter_decays[i], args[i], true);
            }
        }

        return None{};
    }

    [[nodiscard]] auto context(std::string_view operation) const -> std::string
    {
        return std::format("{}({})", operation, m_desc->signature);
    }

    friend struct detail::Access;

    explicit constexpr Function(const detail::FunctionDesc* desc) noexcept
        : m_desc(desc)
    {
    }

    detail::CheckedDesc<detail::FunctionDesc> m_desc;
};

/**
 * @brief Registers a function under get_signature()
 * @note Idempotent. Functions without a canonical name are skipped. Defined in
 *       the registry partition; function_of() calls it on first use.
 */
void register_function(const Function& fn);

/**
 * @brief Picks one overload by its parameter types
 *
 * The result is a function pointer suitable as the token of function_of.
 * A noexcept function binds here too; the reflection still describes the
 * original noexcept function. C variadic functions use the second overload.
 */
template<typename... Params, typename R>
[[nodiscard]] consteval auto overload_of(R (&fn)(Params...)) -> R (*)(Params...)
{
    return &fn;
}

template<typename... Params, typename R>
[[nodiscard]] consteval auto overload_of(R (&fn)(Params..., ...)) -> R (*)(Params..., ...)
{
    return &fn;
}

/**
 * @brief Materializes one free function and registers it on first use
 * @tparam Token A function (or pointer to one), the pointer returned by
 *         overload_of, or `^^f` naming that same function
 *
 * `function_of<ns::add>()` when the name is unique. An overload set is not a
 * single template argument; write `function_of<overload_of<int, int>(ns::scale)>()`.
 * Member functions are rejected: use Class::get_method. A `= delete` function
 * cannot be named on GCC 16.2: passing `^^f` into this template odr-uses it.
 */
template<auto Token>
[[nodiscard]] auto function_of() -> const Function&;

} // namespace speculo

namespace speculo::detail
{

namespace meta = std::meta;

template<meta::info F, std::size_t J>
using free_parameter_type = typename[: meta::type_of(meta::parameters_of(F)[J]) :];

/// Template arguments of a function template specialization (0 for a plain function).
template<meta::info F>
consteval auto function_template_argument_count() -> std::size_t
{
    if constexpr (meta::is_function(F) && meta::has_template_arguments(F))
    {
        return meta::template_arguments_of(F).size();
    }
    else
    {
        return 0;
    }
}

template<meta::info F, std::size_t... I>
consteval auto make_free_invoker(std::index_sequence<I...>) -> InvokeFn
{
    using FnType = typename[: meta::type_of(F) :];
    using R = typename[: meta::return_type_of(F) :];
    using FP = typename static_function_pointer<FnType>::type;

    return +[](void*, const void* const* args) -> void*
    {
        constexpr FP function = meta::extract<FP>(F);
        if constexpr (std::is_void_v<R>)
        {
            function(load_argument<F, I>(args)...);
            return nullptr;
        }
        else
        {
            return new std::remove_cvref_t<R>(function(load_argument<F, I>(args)...));
        }
    };
}

template<meta::info F, std::size_t... I>
consteval auto make_free_ref_invoker(std::index_sequence<I...>) -> InvokeRefFn
{
    using FnType = typename[: meta::type_of(F) :];
    using R = typename[: meta::return_type_of(F) :];
    using FP = typename static_function_pointer<FnType>::type;

    return +[](void*, const void* const* args) -> const void*
    {
        constexpr FP function = meta::extract<FP>(F);
        R result = function(load_argument<F, I>(args)...);
        return referent_address(result);
    };
}

template<meta::info F>
consteval auto make_function_desc(const Class* const* parameters, const TypeDescriptor* const* parameter_decays,
                                  const Annotation* const* empty_annotations, const std::size_t* empty_counts)
    -> FunctionDesc
{
    FunctionDesc desc{};

    const bool named = meta::has_identifier(F);
    desc.name = std::define_static_string(named ? meta::identifier_of(F) : meta::display_string_of(F));
    desc.is_operator = meta::is_operator_function(F);
    desc.is_variadic = meta::is_vararg_function(F);
    desc.has_canonical_name = has_canonical_name(F);
    desc.is_template_specialization = meta::has_template_arguments(F);

    if (meta::has_parent(F))
    {
        const meta::info parent = meta::parent_of(F);
        if (meta::is_namespace(parent) && meta::has_identifier(parent))
        {
            desc.namespace_name = std::define_static_string(meta::display_string_of(parent));
        }
    }

    if (named && !desc.namespace_name.empty())
    {
        std::string qualified{desc.namespace_name};
        qualified += "::";
        qualified += desc.name;
        desc.qualified_name = std::define_static_string(qualified);
    }
    else
    {
        desc.qualified_name = desc.name;
    }

    std::string signature{meta::display_string_of(F)};
    if (meta::is_noexcept(F))
    {
        signature += " noexcept";
    }
    desc.signature = std::define_static_string(signature);

    using R = typename[: meta::return_type_of(F) :];
    desc.return_type = std::addressof(bundle<R>.desc);
    desc.parameter_types = parameters;
    desc.parameter_decays = parameter_decays;
    desc.parameter_count = meta::parameters_of(F).size();

    std::uint32_t mods = 0;
    if (meta::is_noexcept(F))
    {
        mods |= Modifier::NOEXCEPT;
    }
    if (meta::is_deleted(F))
    {
        mods |= Modifier::DELETED;
    }
    desc.modifiers = mods;

    constexpr auto indices = std::make_index_sequence<meta::parameters_of(F).size()>{};
    if constexpr (meta::is_deleted(F) || meta::is_vararg_function(F))
    {
        // Nothing to call. A deleted function must not be `extract`ed.
    }
    else if constexpr (parameters_are_loadable<F>(indices))
    {
        if constexpr (result_is_boxable<F>())
        {
            desc.invoke = make_free_invoker<F>(indices);
        }
        if constexpr (returns_reference<F>())
        {
            desc.invoke_ref = make_free_ref_invoker<F>(indices);
        }
    }

    // annotations_of instantiates the function body. A deleted function has none.
    if constexpr (!meta::is_deleted(F))
    {
        desc.annotations = own_annotations<F>().handles.data();
        desc.annotation_count = own_annotations<F>().count;
        if constexpr (meta::parameters_of(F).size() > 0)
        {
            desc.parameter_annotations = parameter_annotation_index<F>.data.data();
            desc.parameter_annotation_counts = parameter_annotation_index<F>.counts.data();
        }
    }
    else if constexpr (meta::parameters_of(F).size() > 0)
    {
        desc.parameter_annotations = empty_annotations;
        desc.parameter_annotation_counts = empty_counts;
    }

    return desc;
}

template<meta::info F>
struct FreeFunctionBundle
{
    static constexpr std::size_t parameter_count = meta::parameters_of(F).size();

    std::array<const Class*, parameter_count> parameter_classes{};
    std::array<const TypeDescriptor*, parameter_count> parameter_decays{};
    std::array<const Annotation*, parameter_count> empty_annotations{};
    std::array<std::size_t, parameter_count> empty_counts{};
    std::array<TemplateArgDesc, function_template_argument_count<F>()> template_arg_descs{};
    std::array<TemplateArgument, function_template_argument_count<F>()> template_arg_handles{};
    FunctionDesc desc{};
    Function handle{};
};

template<meta::info F>
inline constexpr FreeFunctionBundle<F> free_function_bundle = []() consteval -> FreeFunctionBundle<F>
{
    FreeFunctionBundle<F> built{};

    [&built]<std::size_t... J>(std::index_sequence<J...>)
    {
        std::array<const Class*, sizeof...(J)> classes{
            std::addressof(bundle<free_parameter_type<F, J>>.cls)...
        };
        std::array<const TypeDescriptor*, sizeof...(J)> decays{
            std::addressof(bundle<std::remove_cvref_t<free_parameter_type<F, J>>>.desc)...
        };
        for (std::size_t j = 0; j < classes.size(); ++j)
        {
            built.parameter_classes[j] = classes[j];
            built.parameter_decays[j] = decays[j];
        }
    }(std::make_index_sequence<FreeFunctionBundle<F>::parameter_count>{});

    // Pointers aim at the static bundle, not `built`: the returned object is copied
    // onto free_function_bundle<F>, and those addresses stay valid afterwards.
    built.desc = make_function_desc<F>(free_function_bundle<F>.parameter_classes.data(),
                                       free_function_bundle<F>.parameter_decays.data(),
                                       free_function_bundle<F>.empty_annotations.data(),
                                       free_function_bundle<F>.empty_counts.data());

    // Template arguments (Phase 9): materialized next to the signature so that
    // `function_of<id<int>>()` can report `int`.
    [&built]<std::size_t... K>(std::index_sequence<K...>)
    {
        (([&built]()
        {
            constexpr meta::info A = template_argument_info<F, K>();
            constexpr TemplateArgumentKind kind = template_argument_kind<A>();

            TemplateArgDesc arg{};
            arg.kind = kind;
            arg.spelling = std::define_static_string(meta::display_string_of(A));
            if constexpr (kind == TemplateArgumentKind::Type)
            {
                arg.type = std::addressof(bundle<typename[: A :]>.desc);
            }
            else if constexpr (kind == TemplateArgumentKind::Template)
            {
                arg.primary_template = std::addressof(template_bundle<A>.desc);
            }
            else if constexpr (kind == TemplateArgumentKind::Value)
            {
                using ValueType = std::remove_cv_t<typename[: meta::type_of(A) :]>;
                arg.type = std::addressof(bundle<ValueType>.desc);
                arg.value = template_argument_value<A>();
            }

            built.template_arg_descs[K] = arg;
            built.template_arg_handles[K] =
                Access::make<TemplateArgument>(std::addressof(free_function_bundle<F>.template_arg_descs[K]));
        }()), ...);
    }(std::make_index_sequence<function_template_argument_count<F>()>{});

    built.desc.template_arguments = free_function_bundle<F>.template_arg_handles.data();
    built.desc.template_argument_count = function_template_argument_count<F>();

    built.handle = Access::make<Function>(std::addressof(free_function_bundle<F>.desc));

    return built;
}();

template<auto Token>
    requires std::same_as<decltype(Token), meta::info>
consteval auto resolve_function_token() -> meta::info
{
    static_assert(meta::is_function(Token), "function_of<^^entity>() requires a function");
    static_assert(!meta::is_class_member(Token),
                  "function_of does not reflect member functions; use Class::get_method");
    return Token;
}

template<auto Token>
    requires (std::is_pointer_v<decltype(Token)> && std::is_function_v<std::remove_pointer_t<decltype(Token)>>)
consteval auto resolve_function_token() -> meta::info
{
    constexpr meta::info reflected = meta::reflect_function(*Token);
    static_assert(!meta::is_class_member(reflected),
                  "function_of does not reflect member functions; use Class::get_method");
    return reflected;
}

template<auto Token>
consteval auto published_function() -> const Function*
{
    constexpr meta::info reflected = resolve_function_token<Token>();
    return std::addressof(free_function_bundle<reflected>.handle);
}

} // namespace speculo::detail

export namespace speculo
{

template<auto Token>
auto function_of() -> const Function&
{
    // The reflection stays inside a consteval function. A constexpr meta::info in
    // this runtime function odr-uses a `= delete` function and is ill-formed.
    constexpr const Function* handle = detail::published_function<Token>();

    static const bool registered = []
    {
        register_function(*handle);
        return true;
    }();
    (void)registered;

    return *handle;
}

} // namespace speculo
