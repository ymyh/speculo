module;

#include <meta>
#include <new>

export module speculo:object;

import std;
import :error;
import :modifier;
import :descriptor;

export namespace speculo
{

class Class;
class Object;
class ObjectRef;
class Annotation;
class Field;
class Method;
class Constructor;
class Enumerator;
class Array;
class TemplateArgument;
class Callable;

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

template<typename T>
[[nodiscard]] auto class_of() -> const Class&;

/**
 * @brief Non-owning, type-erased reference to an existing object or subobject
 * @note Lifetime is the caller's responsibility (span-like semantics).
 */
class ObjectRef
{
public:
    ObjectRef() noexcept = default;

    /// Views an existing object; the concrete type is deduced (cv-normalized).
    /// Constness and volatility of the source object are preserved (see
    /// is_const() / is_volatile()).
    template<typename T>
        requires (!std::is_same_v<std::remove_cvref_t<T>, ObjectRef>
               && !std::is_same_v<std::remove_cvref_t<T>, Object>)
    ObjectRef(T& obj) noexcept;

    /// Views an existing object via its Class (const view).
    ObjectRef(const void* data, const Class& type) noexcept;

    /// Views an existing object via its Class (mutable view).
    ObjectRef(void* data, const Class& type) noexcept;

    [[nodiscard]] auto is_null() const noexcept -> bool { return m_data == nullptr; }

    /// Whether this view refers to a const object.
    [[nodiscard]] auto is_const() const noexcept -> bool { return m_is_const; }

    /// Whether this view refers to a volatile object. Reflective reads, writes and
    /// non-volatile calls would access it as a non-volatile object, so they are
    /// refused; `as<volatile T>()` gives the volatile access explicitly.
    [[nodiscard]] auto is_volatile() const noexcept -> bool { return m_is_volatile; }

    /// The referenced type; panics if the reference is null.
    [[nodiscard]] auto type() const -> const Class&;

    /// Read-only address. There is deliberately no `void*` overload: the view
    /// may refer to a const object, which must not be writable through it.
    [[nodiscard]] auto raw_data() const noexcept -> const void* { return m_data; }

    /// Views the referenced object as T; None when the type does not match, or
    /// when the view refers to a const (volatile) object and T is not const
    /// (volatile) qualified (use `as<const T>()` / `as<volatile T>()` to access
    /// such an object through a non-const handle).
    template<typename T>
    [[nodiscard]] auto as() noexcept -> Option<T&>;

    /// @copydoc as()
    template<typename T>
    [[nodiscard]] auto as() const noexcept -> Option<const T&>;

    /// Copies the referenced object into a new owning Object.
    [[nodiscard]] auto clone() const -> Result<Object, Error>;

private:
    friend class Object;
    friend class Field;
    friend class Method;
    friend class Constructor;
    friend class Enumerator;
    friend class Class;
    friend class Array;
    friend class Callable;
    friend class TemplateArgument;
    friend struct detail::Access;

    /// Internal descriptor-based constructor.
    ObjectRef(const void* data, const detail::TypeDescriptor* desc, bool is_const = false,
              bool is_volatile = false) noexcept
        : m_data(data)
        , m_desc(desc)
        , m_is_const(is_const)
        , m_is_volatile(is_volatile)
    {
    }

    [[nodiscard]] auto descriptor() const noexcept -> const detail::TypeDescriptor* { return m_desc; }

    const void*                   m_data = nullptr;
    const detail::TypeDescriptor* m_desc = nullptr;
    bool                          m_is_const = false;
    bool                          m_is_volatile = false;
};

/**
 * @brief Owning, type-erased object (move-only; use clone() for a deep copy)
 */
class Object
{
public:
    Object() noexcept = default;

    Object(Object&& other) noexcept
        : m_data(other.m_data)
        , m_desc(other.m_desc)
    {
        other.m_data = nullptr;
        other.m_desc = nullptr;
    }

    auto operator=(Object&& other) noexcept -> Object&
    {
        if (this != std::addressof(other))
        {
            reset();
            m_data = other.m_data;
            m_desc = other.m_desc;
            other.m_data = nullptr;
            other.m_desc = nullptr;
        }

        return *this;
    }

    Object(const Object&) = delete;
    auto operator=(const Object&) -> Object& = delete;

    ~Object() { reset(); }

    /// Boxes a value by move/copy-constructing it on the heap.
    template<typename T>
    [[nodiscard]] static auto from_value(T value) -> Outcome<Object>;

    /// In-place heap construction of a T from ctor arguments.
    template<typename T, typename... CtorArgs>
        requires std::is_constructible_v<std::remove_cvref_t<T>, CtorArgs...>
    [[nodiscard]] static auto make(CtorArgs&&... args) -> Outcome<Object>;

    /**
     * @brief Aggregate-initializes a value and boxes it (`T{values...}`)
     * @tparam T An aggregate type (cv / references are stripped)
     * @note The compile-time counterpart of `Class::new_aggregate`: no
     *       reflection, no run-time matching, and bit-fields / array members
     *       work as the language allows them to.
     */
    template<typename T, typename... Ts>
        requires std::is_aggregate_v<std::remove_cvref_t<T>>
    [[nodiscard]] static auto make_aggregate(Ts&&... values) -> Outcome<Object>
    {
        return from_value(std::remove_cvref_t<T>{ std::forward<Ts>(values)... });
    }

    /// Deep-copies the boxed value through the descriptor's clone thunk.
    [[nodiscard]] auto clone() const -> Outcome<Object>
    {
        if (m_data == nullptr || m_desc == nullptr)
        {
            return Error{ErrorKind::NullInstance, "Object::clone(): the object is empty"};
        }

        if (m_desc->clone == nullptr)
        {
            return Error{ErrorKind::NotSupported,
                         std::format("Object::clone(): '{}' is not copy-constructible", m_desc->qualified_name)};
        }

        return from_raw(m_desc->clone(m_data), m_desc);
    }

    [[nodiscard]] auto is_null() const noexcept -> bool { return m_data == nullptr; }

    /// The boxed type; panics if the object is empty.
    [[nodiscard]] auto type() const -> const Class&;

    [[nodiscard]] auto raw_data() noexcept -> void* { return m_data; }
    [[nodiscard]] auto raw_data() const noexcept -> const void* { return m_data; }

    /// Views the boxed value as T; None when the type does not match.
    template<typename T>
    [[nodiscard]] auto as() noexcept -> Option<T&>;

    /// @copydoc as()
    template<typename T>
    [[nodiscard]] auto as() const noexcept -> Option<const T&>;

    /// Moves the value out of the box, leaving the object empty.
    template<typename T>
    [[nodiscard]] auto take() && -> Outcome<T>;

    /// Implicit non-owning view.
    operator ObjectRef() noexcept { return ref(); }

    [[nodiscard]] auto ref() noexcept -> ObjectRef
    {
        return m_desc != nullptr ? ObjectRef(m_data, m_desc, false) : ObjectRef{};
    }

    [[nodiscard]] auto cref() const noexcept -> ObjectRef
    {
        return m_desc != nullptr ? ObjectRef(m_data, m_desc, true) : ObjectRef{};
    }

private:
    friend class ObjectRef;
    friend class Annotation;
    friend class Field;
    friend class Method;
    friend class Constructor;
    friend class Enumerator;
    friend class Class;
    friend class Array;
    friend class TemplateArgument;
    friend class Callable;
    friend struct detail::Access;

    /// Adopts an existing heap allocation (internal).
    [[nodiscard]] static auto from_raw(void* data, const detail::TypeDescriptor* desc) noexcept -> Object
    {
        Object obj;
        obj.m_data = data;
        obj.m_desc = desc;

        return obj;
    }

    [[nodiscard]] auto descriptor() const noexcept -> const detail::TypeDescriptor* { return m_desc; }

    void reset() noexcept
    {
        if (m_data != nullptr && m_desc != nullptr && m_desc->release != nullptr)
        {
            m_desc->release(m_data);
        }

        m_data = nullptr;
        m_desc = nullptr;
    }

    void*                         m_data = nullptr;
    const detail::TypeDescriptor* m_desc = nullptr;
};

/**
 * @brief One element of a braced argument list; exists only to make
 *        `invoke(obj, {3, std::string("x"), "text"})` work
 *
 * Implicitly views lvalues and ObjectRefs, and also binds *temporaries*. A
 * temporary lives until the end of the full-expression containing the call, so
 * a call written in place is safe; storing the resulting Args for later is not.
 * A string literal decays to `const char*`, as it would in a C++ call.
 *
 * Exact type match is still required: there are no numeric promotions, so
 * `{true}` is a `bool` and `{3}` an `int`, `{3.0f}` a `float`.
 *
 * Arg itself is pinned in place (the literal case points into the Arg), which
 * is fine because it is only ever an element of an initializer_list.
 */
class Arg
{
public:
    Arg(const ObjectRef& ref) noexcept
        : m_ref(ref)
    {
    }

    /// A concrete lvalue (or an Object).
    template<typename T>
        requires (!std::is_same_v<std::remove_cvref_t<T>, ObjectRef> && std::convertible_to<T&, ObjectRef>)
    Arg(T& value) noexcept
        : m_ref(static_cast<ObjectRef>(value))
    {
    }

    /// A temporary or moved-from value; viewed, never moved from.
    template<typename T>
        requires (!std::is_lvalue_reference_v<T> && !std::is_same_v<std::remove_cvref_t<T>, ObjectRef>
               && std::convertible_to<T&, ObjectRef>)
    Arg(T&& value) noexcept
        : m_ref(static_cast<ObjectRef>(value))
    {
    }

    /// A string literal (or any `const char` array) is passed as `const char*`.
    template<std::size_t N>
    Arg(const char (&text)[N]) noexcept
        : m_text(text)
        , m_ref(m_text)
    {
    }

    Arg(const Arg&) = delete;
    auto operator=(const Arg&) -> Arg& = delete;

    [[nodiscard]] auto ref() const noexcept -> const ObjectRef& { return m_ref; }

private:
    const char* m_text = nullptr;
    ObjectRef   m_ref;
};

/**
 * @brief Small-object-optimized collection of ObjectRef arguments
 *
 * Up to 8 arguments live in an inline buffer (no heap allocation); more fall
 * back to a heap-allocated std::vector. Args only stores views -- the callers
 * must keep the referenced objects alive. Temporaries written inside the braced
 * list (see Arg) live until the end of the full-expression, i.e. through the call.
 */
class Args
{
public:
    Args() noexcept = default;

    Args(std::initializer_list<Arg> args)
    {
        if (args.size() <= kInlineCapacity)
        {
            m_size = args.size();
            std::size_t index = 0;
            for (const Arg& arg : args)
            {
                m_inline[index++] = arg.ref();
            }
        }
        else
        {
            m_heap = new std::vector<ObjectRef>();
            m_heap->reserve(args.size());
            for (const Arg& arg : args)
            {
                m_heap->push_back(arg.ref());
            }
            m_size = args.size();
        }
    }

    /// Views any mix of convertible arguments (Object, concrete T&, ...).
    template<typename... Ts>
        requires (sizeof...(Ts) > 0) && (std::convertible_to<Ts&, ObjectRef> && ...)
    Args(Ts&... objs)
    {
        constexpr std::size_t count = sizeof...(Ts);
        if constexpr (count <= kInlineCapacity)
        {
            m_size = count;
            std::size_t index = 0;
            ((m_inline[index++] = static_cast<ObjectRef>(objs)), ...);
        }
        else
        {
            m_heap = new std::vector<ObjectRef>{ static_cast<ObjectRef>(objs)... };
            m_size = count;
        }
    }

    Args(Args&& other) noexcept
        : m_size(other.m_size)
        , m_heap(other.m_heap)
    {
        if (m_heap != nullptr)
        {
            other.m_heap = nullptr;
        }
        else
        {
            m_inline = other.m_inline;
        }

        other.m_size = 0;
    }

    auto operator=(Args&& other) noexcept -> Args&
    {
        if (this != std::addressof(other))
        {
            release();
            m_size = other.m_size;
            m_heap = other.m_heap;
            if (m_heap != nullptr)
            {
                other.m_heap = nullptr;
            }
            else
            {
                m_inline = other.m_inline;
            }

            other.m_size = 0;
        }

        return *this;
    }

    Args(const Args&) = delete;
    auto operator=(const Args&) -> Args& = delete;

    ~Args() { release(); }

    [[nodiscard]] auto span() const noexcept -> std::span<const ObjectRef>
    {
        if (m_heap != nullptr)
        {
            return std::span<const ObjectRef>(*m_heap);
        }

        return std::span<const ObjectRef>(m_inline.data(), m_size);
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t { return m_size; }
    [[nodiscard]] auto empty() const noexcept -> bool { return m_size == 0; }

    [[nodiscard]] auto operator[](std::size_t index) const -> const ObjectRef&
    {
        return span()[index];
    }

private:
    static constexpr std::size_t kInlineCapacity = 8;

    auto release() noexcept -> void
    {
        delete m_heap;
        m_heap = nullptr;
        m_size = 0;
    }

    std::array<ObjectRef, kInlineCapacity> m_inline{};
    std::size_t             m_size = 0;
    std::vector<ObjectRef>* m_heap = nullptr;
};

} // namespace speculo

namespace speculo::detail
{

/**
 * @brief The raw data pointers of a call's arguments, as the thunks take them
 *
 * Up to Args' inline capacity (8) the pointers live in the object itself, so a
 * call needs no heap allocation; only larger calls fall back to a vector.
 */
class ArgumentPointers
{
public:
    explicit ArgumentPointers(const Args& args)
    {
        const std::size_t count = args.size();
        if (count <= kInlineCapacity)
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                m_inline[i] = args[i].raw_data();
            }

            m_data = m_inline.data();
        }
        else
        {
            m_heap.resize(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                m_heap[i] = args[i].raw_data();
            }

            m_data = m_heap.data();
        }
    }

    ArgumentPointers(const ArgumentPointers&) = delete;
    auto operator=(const ArgumentPointers&) -> ArgumentPointers& = delete;

    [[nodiscard]] auto data() const noexcept -> const void* const* { return m_data; }

private:
    static constexpr std::size_t kInlineCapacity = 8;

    std::array<const void*, kInlineCapacity> m_inline{};
    std::vector<const void*>                 m_heap;
    const void* const*                       m_data = nullptr;
};

/**
 * @brief Whether `arg` (always an lvalue view) can bind to a parameter
 * @param parameter The parameter's exact type (references and cv preserved)
 * @param decay     The parameter's remove_cvref type; must be arg's type
 * @param explicit_choice Whether the caller picked this exact callable. Only then
 *        may a `T&&` parameter take the argument, because ObjectRef has no value
 *        category and the call moves from it. A Method, Constructor, or Function
 *        handle is an explicit choice; Class::new_instance is not.
 *
 * Rules: a const argument cannot bind to a non-const reference; `T&&` needs
 * `explicit_choice`; by-value and `const T&` accept any argument of the type.
 */
[[nodiscard]] auto accepts_argument(const Class* parameter, const TypeDescriptor* decay,
                                    const ObjectRef& arg, bool explicit_choice) -> bool;

/**
 * @brief Error explaining why accepts_argument() rejected the argument at `index`
 * @param callee Context prefix, e.g. `Method::invoke(int Calc::add(int, int))`
 * @note Names the parameter's declared type, the argument's actual type, and
 *       the specific rule that failed.
 */
[[nodiscard]] auto argument_mismatch(std::string_view callee, std::size_t index, const Class* parameter,
                                     const TypeDescriptor* decay, const ObjectRef& arg,
                                     bool explicit_choice) -> Error;

/// Error for a receiver that is not an object of the member's declaring class.
[[nodiscard]] auto receiver_mismatch(std::string_view callee, const TypeDescriptor* declaring,
                                     const ObjectRef& obj) -> Error;

/// `(double, int)`: the argument types, for lookup failures that cannot name one parameter.
[[nodiscard]] auto describe_arguments(const Args& args) -> std::string;

// ─────────────────────────────────────────────────────────────────────────────
// Two-type predicates (api_design §5.2)
//
// `std::is_layout_compatible`, `std::is_pointer_interconvertible_base_of` and
// the two conversion traits need *both* types at compile time. A runtime
// `const Class&` cannot be turned back into a template argument (there is no
// RTTI), and the set of type pairs is not enumerable while materializing, so
// these four stay compile-time functions: the public API below takes both types
// as template arguments.
// ─────────────────────────────────────────────────────────────────────────────

template<typename A, typename B>
consteval auto layout_compatible_types() -> bool
{
    return std::meta::is_layout_compatible_type(^^A, ^^B);
}

/// Note the operand order: `std::meta` (like `std::is_pointer_interconvertible_base_of`)
/// takes (base, derived), while the public function takes (derived, base).
template<typename Derived, typename Base>
consteval auto pointer_interconvertible_types() -> bool
{
    return std::meta::is_pointer_interconvertible_base_of_type(^^Base, ^^Derived);
}

template<typename From, typename To>
consteval auto convertible_types() -> bool
{
    return std::meta::is_convertible_type(^^From, ^^To);
}

template<typename From, typename To>
consteval auto nothrow_convertible_types() -> bool
{
    return std::meta::is_nothrow_convertible_type(^^From, ^^To);
}

} // namespace speculo::detail

export namespace speculo
{

/**
 * @brief One C++26 annotation value (`[[= expr]]`)
 *
 * The handle identifies a single annotation on a declaration. The value's type
 * has top-level const removed (`const Tag` is `class_of<Tag>()`). A value whose
 * type contains a pointer or a reference is listed but not readable: GCC 16.2
 * cannot `extract` it (`reflect_constant` fails), and trying would make
 * `class_of` of the annotated type ill-formed.
 */
class Annotation
{
public:
    constexpr Annotation() noexcept = default;

    [[nodiscard]] auto get_type() const -> const Class&;
    [[nodiscard]] auto is_readable() const -> bool { return m_desc->value != nullptr; }

    /// Exact type match (top-level const ignored) of a readable value. Anything
    /// else is None; the reference points at the static copy made while materializing.
    template<typename T>
    [[nodiscard]] auto get() const -> Option<const T&>;

    /// A boxed copy. NotSupported when the value is unreadable or not copyable.
    [[nodiscard]] auto value() const -> Outcome<Object>;

    [[nodiscard]] auto operator==(const Annotation& other) const noexcept -> bool { return m_desc == other.m_desc; }
    [[nodiscard]] auto operator!=(const Annotation& other) const noexcept -> bool { return m_desc != other.m_desc; }

private:
    friend struct detail::Access;

    explicit constexpr Annotation(const detail::AnnotationDesc* desc) noexcept
        : m_desc(desc)
    {
    }

    detail::CheckedDesc<detail::AnnotationDesc> m_desc;
};

} // namespace speculo

namespace speculo::detail
{

[[nodiscard]] inline auto annotation_span(const void* data, std::size_t count) -> std::span<const Annotation>
{
    return std::span<const Annotation>(static_cast<const Annotation*>(data), count);
}

template<typename T>
[[nodiscard]] auto first_annotation(std::span<const Annotation> annotations) -> Option<const T&>
{
    for (const Annotation& annotation : annotations)
    {
        Option<const T&> value = annotation.get<T>();
        if (value.is_some())
        {
            return value;
        }
    }

    return None{};
}

} // namespace speculo::detail

export namespace speculo
{

/**
 * @brief Handle describing one data member (Java {@code java.lang.reflect.Field})
 */
class Field
{
public:
    constexpr Field() noexcept = default;

    [[nodiscard]] auto get_name() const -> std::string_view { return m_desc->name; }
    [[nodiscard]] auto get_declaring_class() const -> const Class& { return *m_desc->declaring_class->self; }
    [[nodiscard]] auto get_type() const -> const Class& { return *m_desc->type->self; }
    [[nodiscard]] auto modifiers() const -> Modifier { return Modifier(m_desc->modifiers); }
    [[nodiscard]] auto is_static() const -> bool { return m_desc->is_static; }

    /// Reads the field value (a boxed copy); obj is ignored for static fields.
    [[nodiscard]] auto get(ObjectRef obj) const -> Outcome<Object>
    {
        if (!is_static() && obj.is_null())
        {
            return Error{ErrorKind::NullInstance, std::format("{}: the receiver is null", context("Field::get"))};
        }

        if (m_desc->get == nullptr)
        {
            return Error{ErrorKind::NotSupported,
                         std::format("{}: this field cannot be read by value", context("Field::get"))};
        }

        if (!is_static() && obj.descriptor() != m_desc->declaring_class->decayed)
        {
            return detail::receiver_mismatch(context("Field::get"), m_desc->declaring_class, obj);
        }

        // A volatile object's members are volatile; the by-value read would access
        // them as non-volatile. (A reference member's referent is not part of the owner.)
        if (!is_static() && obj.is_volatile() && !is_reference())
        {
            return Error{ErrorKind::NotSupported,
                         std::format("{}: reading through a volatile object is not supported; use get_ref() and "
                                     "as<volatile T>()",
                                     context("Field::get"))};
        }

        return Object::from_raw(m_desc->get(obj.raw_data()), m_desc->type->decayed);
    }

    /// Writes the field value (exact type match required); obj is ignored for statics.
    [[nodiscard]] auto set(ObjectRef obj, ObjectRef value) const -> Status
    {
        if (!is_static() && obj.is_null())
        {
            return Error{ErrorKind::NullInstance, std::format("{}: the receiver is null", context("Field::set"))};
        }

        // A const object protects its own members, not what a reference member
        // refers to (that is decided by the referenced type, see is_write_protected()).
        if (!is_static() && obj.is_const() && !is_mutable() && !is_reference())
        {
            return Error{ErrorKind::ReadOnly,
                         std::format("{}: the receiver is a const object", context("Field::set"))};
        }

        if (!is_static() && obj.descriptor() != m_desc->declaring_class->decayed)
        {
            return detail::receiver_mismatch(context("Field::set"), m_desc->declaring_class, obj);
        }

        if (!is_static() && obj.is_volatile() && !is_reference())
        {
            return Error{ErrorKind::NotSupported,
                         std::format("{}: writing through a volatile object is not supported; use get_ref() and "
                                     "as<volatile T>()",
                                     context("Field::set"))};
        }

        if (m_desc->set == nullptr)
        {
            if (is_write_protected())
            {
                return Error{ErrorKind::ReadOnly, std::format("{}: the field is const", context("Field::set"))};
            }

            return Error{ErrorKind::NotSupported,
                         std::format("{}: the field type '{}' cannot be copy-assigned (array or non-copyable)",
                                     context("Field::set"), m_desc->type->qualified_name)};
        }

        if (value.is_null())
        {
            return Error{ErrorKind::TypeMismatch,
                         std::format("{}: the value is null, expected '{}'", context("Field::set"),
                                     m_desc->type->qualified_name)};
        }

        if (value.descriptor() != m_desc->type->decayed)
        {
            return Error{ErrorKind::TypeMismatch,
                         std::format("{}: the value has type '{}', expected '{}' (types must match exactly)",
                                     context("Field::set"), value.descriptor()->qualified_name,
                                     m_desc->type->qualified_name)};
        }

        // Constness was checked above (mutable / reference members excepted).
        if (!m_desc->set(const_cast<void*>(obj.raw_data()), value.raw_data()))
        {
            return Error{ErrorKind::TypeMismatch, std::format("{}: the assignment failed", context("Field::set"))};
        }

        return None{};
    }

    /**
     * @brief Returns a reference view of the field (NotSupported for bitfields)
     * @note For a reference member the view is the *referenced* object, typed
     *       as the object type `T` (the handle carries no reference type) and
     *       const exactly when the referenced type is const.
     */
    [[nodiscard]] auto get_ref(ObjectRef obj) const -> Result<ObjectRef, Error>
    {
        if (!is_static() && obj.is_null())
        {
            return Error{ErrorKind::NullInstance,
                         std::format("{}: the receiver is null", context("Field::get_ref"))};
        }

        if (m_desc->get_ref == nullptr)
        {
            return Error{ErrorKind::NotSupported,
                         std::format("{}: a reference view is not available for this field (bitfield)",
                                     context("Field::get_ref"))};
        }

        if (!is_static() && obj.descriptor() != m_desc->declaring_class->decayed)
        {
            return detail::receiver_mismatch(context("Field::get_ref"), m_desc->declaring_class, obj);
        }

        // A mutable member stays mutable through a const object; a static field's
        // constness comes only from its own declaration, never from an instance.
        // A reference member's view is its referent: a const owner does not make
        // it const, only the referenced type does.
        const bool through_const_object = !is_static() && obj.is_const() && !is_mutable() && !is_reference();
        const bool is_const = through_const_object || is_write_protected();

        // Volatility follows the same rule, except that `mutable` does not cancel it.
        const bool through_volatile_object = !is_static() && obj.is_volatile() && !is_reference();
        const bool is_volatile = through_volatile_object || is_declared_volatile();

        // The view's own is_const() / is_volatile() carry the qualifiers computed above.
        return ObjectRef(m_desc->get_ref(const_cast<void*>(obj.raw_data())), m_desc->type->decayed, is_const,
                         is_volatile);
    }

    [[nodiscard]] auto get_size() const -> std::size_t { return m_desc->size; }
    [[nodiscard]] auto get_alignment() const -> std::size_t { return m_desc->alignment; }
    [[nodiscard]] auto get_offset() const -> std::size_t { return m_desc->offset; }
    [[nodiscard]] auto get_bit_offset() const -> std::size_t { return m_desc->bit_offset; }
    [[nodiscard]] auto is_bitfield() const -> bool { return m_desc->is_bitfield; }
    [[nodiscard]] auto is_mutable() const -> bool { return m_desc->is_mutable; }
    [[nodiscard]] auto is_reference() const -> bool { return m_desc->is_reference; }

    [[nodiscard]] auto get_bit_width() const -> Option<std::size_t>
    {
        if (!is_bitfield())
        {
            return None{};
        }

        return m_desc->bit_width;
    }

    [[nodiscard]] auto get_annotations() const -> std::span<const Annotation>
    {
        return detail::annotation_span(m_desc->annotations, m_desc->annotation_count);
    }

    template<typename T>
    [[nodiscard]] auto get_annotation() const -> Option<const T&>
    {
        return detail::first_annotation<T>(get_annotations());
    }

    [[nodiscard]] auto operator==(const Field& other) const noexcept -> bool { return m_desc == other.m_desc; }
    [[nodiscard]] auto operator!=(const Field& other) const noexcept -> bool { return m_desc != other.m_desc; }

private:
    /// Whether the declaration itself forbids writes: a `const` member, or a
    /// reference member whose referenced type is const.
    [[nodiscard]] auto is_write_protected() const -> bool
    {
        if (m_desc->is_reference)
        {
            return m_desc->type->referenced != nullptr && m_desc->type->referenced->is_const_qualified;
        }

        return modifiers().is_const();
    }

    /// Whether the declaration itself is volatile (the member, or the referent of
    /// a reference member).
    [[nodiscard]] auto is_declared_volatile() const -> bool
    {
        if (m_desc->is_reference)
        {
            return m_desc->type->referenced != nullptr && m_desc->type->referenced->is_volatile_qualified;
        }

        return modifiers().is_volatile();
    }

    /// "Field::get(Point::x)": the operation plus the member it was applied to.
    [[nodiscard]] auto context(std::string_view operation) const -> std::string
    {
        return std::format("{}({}::{})", operation, m_desc->declaring_class->qualified_name, m_desc->name);
    }

    friend struct detail::Access;

    explicit constexpr Field(const detail::FieldDesc* desc) noexcept
        : m_desc(desc)
    {
    }

    detail::CheckedDesc<detail::FieldDesc> m_desc;
};

/**
 * @brief Handle describing one member function (Java {@code java.lang.reflect.Method})
 * @note Phase 3 minimal surface: identity, signature and invocation.
 */
class Method
{
public:
    constexpr Method() noexcept = default;

    [[nodiscard]] auto get_name() const -> std::string_view { return m_desc->name; }
    [[nodiscard]] auto get_declaring_class() const -> const Class& { return *m_desc->declaring_class->self; }
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
    [[nodiscard]] auto is_const() const -> bool { return modifiers().is_const(); }
    [[nodiscard]] auto is_volatile() const -> bool { return modifiers().is_volatile(); }
    [[nodiscard]] auto is_static_member() const -> bool { return modifiers().is_static(); }
    [[nodiscard]] auto is_noexcept() const -> bool { return modifiers().is_noexcept(); }
    [[nodiscard]] auto is_virtual() const -> bool { return modifiers().is_virtual(); }
    [[nodiscard]] auto is_pure_virtual() const -> bool { return modifiers().is_abstract(); }

    /// Whether invoke() can run this method (false for `&&`-qualified members,
    /// destructors, deleted functions, C-variadic functions, and signatures the
    /// thunk cannot express).
    [[nodiscard]] auto is_invokable() const -> bool { return m_desc->invoke != nullptr; }

    /// `= delete`d (listed so overloads are complete; never invokable).
    [[nodiscard]] auto is_deleted() const -> bool { return modifiers().is_deleted(); }
    [[nodiscard]] auto is_defaulted() const -> bool { return modifiers().is_defaulted(); }
    [[nodiscard]] auto is_override() const -> bool { return modifiers().is_override(); }
    [[nodiscard]] auto is_operator() const -> bool { return m_desc->is_operator; }
    [[nodiscard]] auto is_destructor() const -> bool { return m_desc->is_destructor; }
    [[nodiscard]] auto get_ref_qualifier() const -> RefQualifier { return m_desc->ref_qualifier; }
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
            crab_cpp::panic("speculo: Method::get_parameter_annotations index is out of range");
        }

        const auto* spans = static_cast<const Annotation* const*>(m_desc->parameter_annotations);
        return std::span<const Annotation>(spans[index], m_desc->parameter_annotation_counts[index]);
    }

    template<typename T>
    [[nodiscard]] auto get_parameter_annotation(std::size_t index) const -> Option<const T&>
    {
        return detail::first_annotation<T>(get_parameter_annotations(index));
    }

    /// Whether the method returns an lvalue or rvalue reference. get_return_type()
    /// is then the exact reference type (`T&`), while invoke() yields a *copy* of
    /// the referent; use invoke_ref() to reach the referent itself.
    [[nodiscard]] auto returns_reference() const -> bool
    {
        return m_desc->return_type != nullptr && m_desc->return_type->reference_kind != RefQualifier::None;
    }

    /// Whether invoke_ref() can run this method: it returns a reference and the
    /// arguments can be passed (independent of whether the referent is copyable).
    [[nodiscard]] auto is_ref_invokable() const -> bool { return m_desc->invoke_ref != nullptr; }

    /**
     * @brief Invokes the method, boxing the return value (empty Object for void)
     *
     * The boxed value is always of the `remove_cvref` of the declared return
     * type: a method returning `T&` / `const T&` yields an Object of type `T`
     * holding a **copy** of the referent (`result.as<T>()` works, `as<T&>()`
     * does not, and writes go to the copy). When the referent is not copyable
     * the call is `NotSupported`. Use invoke_ref() to get the referent itself.
     */
    [[nodiscard]] auto invoke(ObjectRef obj, Args args = {}) const -> Outcome<Object>
    {
        const Status checked = check_call("Method::invoke", obj, args);
        if (checked.is_err())
        {
            return checked.unwrap_err();
        }

        if (m_desc->invoke == nullptr)
        {
            const char* reason = is_deleted() ? "the function is deleted"
                                 : is_ref_invokable() ? "the referenced result is not copyable (use invoke_ref())"
                                                      : "the method is not available";
            return Error{ErrorKind::NotSupported, std::format("{}: {}", context("Method::invoke"), reason)};
        }

        const detail::ArgumentPointers pointers = argument_pointers(args);

        // A non-const method on a const receiver was rejected above.
        void* receiver = is_static_member() ? nullptr : const_cast<void*>(obj.raw_data());
        void* result = m_desc->invoke(receiver, pointers.data());
        if (result == nullptr)
        {
            return Object{};
        }

        return Object::from_raw(result, m_desc->return_type->decayed);
    }

    /**
     * @brief Invokes a reference-returning method and views the referent (no copy)
     *
     * The returned view's type() is the referent's object type `T` (handles do not
     * carry reference types), and it is const / volatile exactly when the referenced
     * type is. Writing through it modifies the original object. The view lives only
     * as long as the reference the callee returned (a member of the receiver, a
     * static, ...); the library cannot extend it. Works for non-copyable referents
     * (e.g. `const std::unique_ptr<T>&`), which invoke() cannot box.
     *
     * Methods that do not return a reference are `NotSupported`; use invoke().
     */
    [[nodiscard]] auto invoke_ref(ObjectRef obj, Args args = {}) const -> Outcome<ObjectRef>
    {
        const Status checked = check_call("Method::invoke_ref", obj, args);
        if (checked.is_err())
        {
            return checked.unwrap_err();
        }

        if (!returns_reference())
        {
            return Error{ErrorKind::NotSupported,
                         std::format("{}: the method does not return a reference; use invoke()",
                                     context("Method::invoke_ref"))};
        }

        if (m_desc->invoke_ref == nullptr)
        {
            return Error{ErrorKind::NotSupported,
                         std::format("{}: {}", context("Method::invoke_ref"),
                                     is_deleted() ? "the function is deleted" : "the method is not available")};
        }

        const detail::ArgumentPointers pointers = argument_pointers(args);

        void* receiver = is_static_member() ? nullptr : const_cast<void*>(obj.raw_data());
        const void* address = m_desc->invoke_ref(receiver, pointers.data());
        const detail::TypeDescriptor* referent = m_desc->return_type->referenced;

        return ObjectRef(address, m_desc->return_type->decayed, referent->is_const_qualified,
                         referent->is_volatile_qualified);
    }

    [[nodiscard]] auto operator==(const Method& other) const noexcept -> bool { return m_desc == other.m_desc; }
    [[nodiscard]] auto operator!=(const Method& other) const noexcept -> bool { return m_desc != other.m_desc; }

private:
    /// Validates the receiver and the arguments, shared by invoke() / invoke_ref().
    [[nodiscard]] auto check_call(std::string_view operation, const ObjectRef& obj, const Args& args) const -> Status
    {
        const std::size_t count = m_desc->parameter_count;
        if (args.size() != count)
        {
            return Error{ErrorKind::InvalidArgument,
                         std::format("{}: expected {} argument(s), got {}", context(operation), count, args.size())};
        }

        if (!is_static_member() && obj.is_null())
        {
            return Error{ErrorKind::NullInstance, std::format("{}: the receiver is null", context(operation))};
        }

        if (!is_static_member() && obj.descriptor() != m_desc->declaring_class->decayed)
        {
            return detail::receiver_mismatch(context(operation), m_desc->declaring_class, obj);
        }

        if (!is_static_member() && !is_const() && obj.is_const())
        {
            return Error{ErrorKind::ReadOnly,
                         std::format("{}: a non-const method was called on a const object", context(operation))};
        }

        if (!is_static_member() && !is_volatile() && obj.is_volatile())
        {
            return Error{ErrorKind::NotSupported,
                         std::format("{}: a non-volatile method was called on a volatile object", context(operation))};
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

    [[nodiscard]] static auto argument_pointers(const Args& args) -> detail::ArgumentPointers
    {
        return detail::ArgumentPointers(args);
    }

    /// "Method::invoke(int Calc::add(int, int))": the operation plus the full signature.
    [[nodiscard]] auto context(std::string_view operation) const -> std::string
    {
        return std::format("{}({})", operation, m_desc->signature);
    }

    friend struct detail::Access;

    explicit constexpr Method(const detail::MethodDesc* desc) noexcept
        : m_desc(desc)
    {
    }

    detail::CheckedDesc<detail::MethodDesc> m_desc;
};

/**
 * @brief cv / ref qualification of a member function
 *
 * Picks between overloads whose parameter lists are identical, e.g.
 * `void f(int)` vs `void f(int) const`, or `void g() &` vs `void g() &&`.
 */
struct MemberQualifiers
{
    bool         is_const = false;
    bool         is_volatile = false;
    RefQualifier ref = RefQualifier::None;
};

/**
 * @brief One direct base class (Java {@code Class#getSuperclass} generalization)
 * @note Base offsets are only exposed for non-virtual bases: a virtual base has
 *       no constant offset (see @.agents/api_design.md §6), so pointer
 *       adjustment there still goes through Class::cast_to.
 */
struct BaseClass
{
    const Class* type = nullptr;
    Modifier     modifiers; ///< PUBLIC / PROTECTED / PRIVATE / VIRTUAL

    /// Type-erased derived -> base pointer adjustment (virtual bases included).
    /// nullptr when the base is not accessible (protected/private).
    detail::UpcastFn cast = nullptr;

    /// Byte offset from the derived object's start to this base subobject.
    /// nullptr for virtual bases (no static offset) and inaccessible bases.
    detail::BaseOffsetFn offset = nullptr;

    const Annotation* annotations = nullptr;
    std::size_t annotation_count = 0;

    [[nodiscard]] auto is_virtual() const noexcept -> bool { return modifiers.is_virtual(); }

    /**
     * @brief Byte offset of this base subobject inside the derived object
     * @return Some(offset) for non-virtual, accessible bases; None for virtual
     *         bases (their offset varies with the complete object) and for
     *         protected / private bases.
     * @note The offset is computed while materializing (from
     *       `std::meta::offset_of` on the base reflection), so the thunk folds
     *       to a constant -- there is nothing to compute at run time.
     */
    [[nodiscard]] auto get_offset() const -> Option<std::size_t>
    {
        if (offset == nullptr)
        {
            return None{};
        }

        return offset();
    }

    [[nodiscard]] auto get_annotations() const -> std::span<const Annotation>
    {
        return std::span<const Annotation>(annotations, annotation_count);
    }

    template<typename T>
    [[nodiscard]] auto get_annotation() const -> Option<const T&>
    {
        return detail::first_annotation<T>(get_annotations());
    }
};

/**
 * @brief Handles describing one constructor (Java {@code java.lang.reflect.Constructor})
 */
class Constructor
{
public:
    constexpr Constructor() noexcept = default;

    [[nodiscard]] auto get_declaring_class() const -> const Class& { return *m_desc->declaring_class->self; }
    [[nodiscard]] auto get_parameter_count() const -> std::size_t { return m_desc->parameter_count; }

    [[nodiscard]] auto get_parameter_types() const -> std::span<const Class* const>
    {
        return std::span<const Class* const>(m_desc->parameter_types, m_desc->parameter_count);
    }

    [[nodiscard]] auto modifiers() const -> Modifier { return Modifier(m_desc->modifiers); }

    [[nodiscard]] auto is_default_constructor() const -> bool
    {
        return m_desc->parameter_count == 0 && !m_desc->is_copy && !m_desc->is_move;
    }

    [[nodiscard]] auto is_copy_constructor() const -> bool { return m_desc->is_copy; }
    [[nodiscard]] auto is_move_constructor() const -> bool { return m_desc->is_move; }
    [[nodiscard]] auto is_explicit() const -> bool { return modifiers().is_explicit(); }
    [[nodiscard]] auto is_deleted() const -> bool { return modifiers().is_deleted(); }
    [[nodiscard]] auto is_defaulted() const -> bool { return modifiers().is_defaulted(); }
    [[nodiscard]] auto is_noexcept() const -> bool { return modifiers().is_noexcept(); }

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
            crab_cpp::panic("speculo: Constructor::get_parameter_annotations index is out of range");
        }

        const auto* spans = static_cast<const Annotation* const*>(m_desc->parameter_annotations);
        return std::span<const Annotation>(spans[index], m_desc->parameter_annotation_counts[index]);
    }

    template<typename T>
    [[nodiscard]] auto get_parameter_annotation(std::size_t index) const -> Option<const T&>
    {
        return detail::first_annotation<T>(get_parameter_annotations(index));
    }

    /// Heap-constructs an owning Object by invoking this constructor.
    [[nodiscard]] auto new_instance(Args args = {}) const -> Outcome<Object>
    {
        if (is_deleted())
        {
            return Error{ErrorKind::NotSupported, std::format("{}: the constructor is deleted", context("Constructor::new_instance"))};
        }

        const Status checked = validate("Constructor::new_instance", args);
        if (checked.is_err())
        {
            return checked.unwrap_err();
        }

        if (m_desc->new_instance == nullptr)
        {
            return Error{ErrorKind::NotSupported,
                         std::format("{}: this constructor cannot be invoked", context("Constructor::new_instance"))};
        }

        const detail::ArgumentPointers pointers = argument_pointers(args);
        return Object::from_raw(m_desc->new_instance(pointers.data()), m_desc->declaring_class);
    }

    /// Placement-constructs into caller-provided storage.
    [[nodiscard]] auto construct_at(void* storage, Args args = {}) const -> Status
    {
        if (storage == nullptr)
        {
            return Error{ErrorKind::InvalidArgument,
                         std::format("{}: the storage pointer is null", context("Constructor::construct_at"))};
        }

        if (is_deleted())
        {
            return Error{ErrorKind::NotSupported, std::format("{}: the constructor is deleted", context("Constructor::construct_at"))};
        }

        const Status checked = validate("Constructor::construct_at", args);
        if (checked.is_err())
        {
            return checked;
        }

        if (m_desc->construct_at == nullptr)
        {
            return Error{ErrorKind::NotSupported,
                         std::format("{}: this constructor cannot be invoked", context("Constructor::construct_at"))};
        }

        const detail::ArgumentPointers pointers = argument_pointers(args);
        m_desc->construct_at(storage, pointers.data());

        return None{};
    }

    /// Destroys an object previously constructed in the given storage.
    [[nodiscard]] auto destroy_at(void* storage) const -> Status
    {
        if (storage == nullptr)
        {
            return Error{ErrorKind::InvalidArgument,
                         std::format("{}: the storage pointer is null", context("Constructor::destroy_at"))};
        }

        if (m_desc->destroy_at == nullptr)
        {
            return Error{ErrorKind::NotSupported,
                         std::format("{}: the type '{}' is not destructible", context("Constructor::destroy_at"),
                                     m_desc->declaring_class->qualified_name)};
        }

        m_desc->destroy_at(storage);

        return None{};
    }

    /**
     * @brief Whether Class::new_instance may select this constructor for `args`
     * @note Arguments are lvalue views, so a `T&&` parameter is never selected
     *       automatically; call this constructor's new_instance() directly to
     *       opt into a move.
     */
    [[nodiscard]] auto accepts(const Args& args) const -> bool
    {
        if (args.size() != m_desc->parameter_count)
        {
            return false;
        }

        for (std::size_t i = 0; i < m_desc->parameter_count; ++i)
        {
            if (!detail::accepts_argument(m_desc->parameter_types[i], m_desc->parameter_decays[i], args[i], false))
            {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] auto operator==(const Constructor& other) const noexcept -> bool { return m_desc == other.m_desc; }
    [[nodiscard]] auto operator!=(const Constructor& other) const noexcept -> bool { return m_desc != other.m_desc; }

private:
    [[nodiscard]] auto validate(std::string_view operation, const Args& args) const -> Status
    {
        if (args.size() != m_desc->parameter_count)
        {
            return Error{ErrorKind::InvalidArgument,
                         std::format("{}: expected {} argument(s), got {}", context(operation),
                                     m_desc->parameter_count, args.size())};
        }

        for (std::size_t i = 0; i < m_desc->parameter_count; ++i)
        {
            if (!detail::accepts_argument(m_desc->parameter_types[i], m_desc->parameter_decays[i], args[i], true))
            {
                return detail::argument_mismatch(context(operation), i, m_desc->parameter_types[i],
                                                 m_desc->parameter_decays[i], args[i], true);
            }
        }

        return None{};
    }

    [[nodiscard]] static auto argument_pointers(const Args& args) -> detail::ArgumentPointers
    {
        return detail::ArgumentPointers(args);
    }

    /// "Constructor::new_instance(Point(int, int))": the operation plus the signature.
    [[nodiscard]] auto context(std::string_view operation) const -> std::string
    {
        return std::format("{}({})", operation, m_desc->signature);
    }

    friend struct detail::Access;

    explicit constexpr Constructor(const detail::CtorDesc* desc) noexcept
        : m_desc(desc)
    {
    }

    detail::CheckedDesc<detail::CtorDesc> m_desc;
};

/**
 * @brief Handle describing one enumerator (Java has no direct counterpart)
 */
class Enumerator
{
public:
    constexpr Enumerator() noexcept = default;

    [[nodiscard]] auto get_name() const -> std::string_view { return m_desc->name; }
    [[nodiscard]] auto get_declaring_enum() const -> const Class& { return *m_desc->declaring_enum->self; }

    /**
     * @brief The enumerator's value, boxed as the enum's underlying integer type
     * @note `enum : int` gives an `int`, `enum : unsigned char` an `unsigned char`
     *       (the type reported by Class::get_underlying_type()). Use to_int64() /
     *       to_uint64() when the exact type does not matter.
     */
    [[nodiscard]] auto value() const -> Outcome<Object>
    {
        const detail::TypeDescriptor* enum_desc = m_desc->declaring_enum;
        if (enum_desc == nullptr || enum_desc->enum_box == nullptr || enum_desc->underlying == nullptr)
        {
            return Error{ErrorKind::NotSupported,
                         std::format("Enumerator::value({}): the enum type has no boxing support", m_desc->name)};
        }

        return Object::from_raw(enum_desc->enum_box(m_desc->uvalue), enum_desc->underlying);
    }

    [[nodiscard]] auto to_int64() const -> std::int64_t { return m_desc->value; }
    [[nodiscard]] auto to_uint64() const -> std::uint64_t { return m_desc->uvalue; }

    [[nodiscard]] auto get_annotations() const -> std::span<const Annotation>
    {
        return detail::annotation_span(m_desc->annotations, m_desc->annotation_count);
    }

    template<typename T>
    [[nodiscard]] auto get_annotation() const -> Option<const T&>
    {
        return detail::first_annotation<T>(get_annotations());
    }

    [[nodiscard]] auto operator==(const Enumerator& other) const noexcept -> bool { return m_desc == other.m_desc; }
    [[nodiscard]] auto operator!=(const Enumerator& other) const noexcept -> bool { return m_desc != other.m_desc; }

private:
    friend struct detail::Access;

    explicit constexpr Enumerator(const detail::EnumDesc* desc) noexcept
        : m_desc(desc)
    {
    }

    detail::CheckedDesc<detail::EnumDesc> m_desc;
};

} // namespace speculo

namespace speculo::detail
{

/// One member found while walking the bases (Class::get_field_recursive), with the
/// identity of the subobject it lives in: the chain of classes after the last
/// virtual edge (a virtual base reached by several paths is a single subobject,
/// a non-virtual one is not).
template<typename Member>
struct MemberHit
{
    Member                    member;
    std::vector<const Class*> subobject;
};

} // namespace speculo::detail

export namespace speculo
{

/**
 * @brief One template argument of a class or function template specialization
 *        (Java erases generics; this is C++-only information)
 *
 * Obtained from `Class::get_template_arguments()` / `Function::get_template_arguments()`.
 * One `kind()` selects the meaningful accessor; the other three return `None`
 * (or an empty span for `as_pack()`).
 *
 * @note Parameter packs are flattened by the reflection layer, so one pack
 *       argument shows up as several independent arguments and `Kind::Pack` is
 *       never produced by GCC 16.2 (see @.agents/api_design.md §1.2).
 */
class TemplateArgument
{
public:
    using Kind = TemplateArgumentKind;

    constexpr TemplateArgument() noexcept = default;

    [[nodiscard]] auto kind() const -> Kind { return m_desc->kind; }

    /// Kind::Type only: the argument type (`std::vector<int>` -> `int`).
    [[nodiscard]] auto as_type() const -> Option<const Class&>
    {
        if (m_desc->kind != Kind::Type)
        {
            return None{};
        }

        return *m_desc->type->self;
    }

    /**
     * @brief Kind::Value only: the non-type argument, boxed as its own type
     * @note `std::array<int, 3>` boxes an `unsigned long long` holding 3 (the
     *       argument's declared type, exactly as the compiler spells it).
     *       None when the kind differs or when the value cannot be extracted on
     *       this compiler (pointer / member-pointer arguments).
     */
    [[nodiscard]] auto as_value() const -> Option<Object>
    {
        if (m_desc->kind != Kind::Value || m_desc->value == nullptr || m_desc->type == nullptr
            || m_desc->type->clone == nullptr)
        {
            return None{};
        }

        return Object::from_raw(m_desc->type->clone(m_desc->value), m_desc->type->decayed);
    }

    /// Kind::Template only: the template-template argument's primary template.
    [[nodiscard]] auto as_template() const -> Option<const Class&>
    {
        if (m_desc->kind != Kind::Template || m_desc->primary_template == nullptr)
        {
            return None{};
        }

        return *m_desc->primary_template->self;
    }

    /// Kind::Pack only (never produced by GCC 16.2): the pack's arguments.
    [[nodiscard]] auto as_pack() const -> std::span<const TemplateArgument>
    {
        if (m_desc->kind != Kind::Pack)
        {
            return {};
        }

        return std::span<const TemplateArgument>(static_cast<const TemplateArgument*>(m_desc->pack),
                                                 m_desc->pack_count);
    }

    /// The compiler's spelling: `int` / `3` / `std::vector` / `Config{7, 9}`.
    [[nodiscard]] auto get_spelling() const -> std::string_view { return m_desc->spelling; }

    [[nodiscard]] auto operator==(const TemplateArgument& other) const noexcept -> bool
    {
        return m_desc == other.m_desc;
    }

    [[nodiscard]] auto operator!=(const TemplateArgument& other) const noexcept -> bool
    {
        return m_desc != other.m_desc;
    }

private:
    friend struct detail::Access;

    explicit constexpr TemplateArgument(const detail::TemplateArgDesc* desc) noexcept
        : m_desc(desc)
    {
    }

    detail::CheckedDesc<detail::TemplateArgDesc> m_desc;
};

/**
 * @brief Runtime handle describing one type (Java {@code java.lang.Class})
 *
 * A lightweight, copyable, comparable handle into the static descriptor table.
 * Phase 3 exposes identity, classification, size/alignment and the public
 * member views (fields / methods, own class).
 */
class Class
{
public:
    constexpr Class() noexcept = default;

    [[nodiscard]] auto get_name() const -> std::string_view { return m_desc->name; }
    [[nodiscard]] auto get_simple_name() const -> std::string_view { return m_desc->name; }
    /// The compiler's spelling of the type (GCC's `display_string_of`), e.g.
    /// `long long unsigned int`. Tied to the compiler version; prefer the
    /// normalized name for anything you write down or compare.
    [[nodiscard]] auto get_qualified_name() const -> std::string_view { return m_desc->qualified_name; }

    /// The library-defined spelling: `unsigned long long`, `const int* const`,
    /// `int[3]`, `std::string_view`, ... (see detail::normalize_type_name).
    [[nodiscard]] auto get_normalized_name() const -> std::string_view { return m_desc->normalized_name; }
    [[nodiscard]] auto has_canonical_name() const -> bool { return m_desc->has_canonical_name; }

    /**
     * @brief Looks up a type by name (Java {@code Class.forName})
     * @param qualified_name Either name of the type: get_qualified_name() or
     *        get_normalized_name(). Any spelling that normalizes to the same
     *        string also matches, so whitespace and the short fundamental names
     *        (`unsigned long long`) do not have to be guessed exactly.
     * @return Some(class) when the name is registered, otherwise None
     * @note Types register automatically the first time class_of<T>() is called
     *       for them; register_class() / SPECULO_REGISTER force registration
     *       without materializing the type. Only types with a canonical name
     *       are registrable, so anonymous-namespace and function-local types
     *       can never be found here (see @.agents/design.md §11.5).
     * @note A miss does not say whether the type is unregistered or misspelled;
     *       registered_names() lists what the table holds.
     * @note Do not call it during static initialization: SPECULO_REGISTER
     *       registers from static initializers, whose order across translation
     *       units is unspecified, so the type may not be there yet. From main()
     *       on there is no such problem.
     * @note Defined in the `speculo:registry` partition (Phase 6).
     */
    [[nodiscard]] static auto for_name(std::string_view qualified_name) -> Option<const Class&>;

    /// Every name the registry currently knows (both spellings of each type),
    /// sorted. Meant for diagnostics; defined in the `speculo:registry` partition.
    [[nodiscard]] static auto registered_names() -> std::vector<std::string_view>;

    [[nodiscard]] auto kind() const -> TypeKind { return m_desc->kind; }

    [[nodiscard]] auto is_void() const -> bool { return m_desc->kind == TypeKind::Void; }
    [[nodiscard]] auto is_fundamental() const -> bool { return m_desc->kind == TypeKind::Fundamental; }
    [[nodiscard]] auto is_pointer() const -> bool { return m_desc->kind == TypeKind::Pointer; }
    [[nodiscard]] auto is_member_pointer() const -> bool { return m_desc->kind == TypeKind::MemberPointer; }
    [[nodiscard]] auto is_array() const -> bool { return m_desc->kind == TypeKind::Array; }
    [[nodiscard]] auto is_enum() const -> bool { return m_desc->kind == TypeKind::Enum; }
    [[nodiscard]] auto is_union() const -> bool { return m_desc->kind == TypeKind::Union; }
    [[nodiscard]] auto is_class() const -> bool { return m_desc->kind == TypeKind::Class; }
    [[nodiscard]] auto is_function() const -> bool { return m_desc->kind == TypeKind::Function; }
    [[nodiscard]] auto is_reference() const -> bool { return m_desc->kind == TypeKind::Reference; }

    [[nodiscard]] auto is_polymorphic() const -> bool { return m_desc->is_polymorphic; }
    [[nodiscard]] auto is_abstract() const -> bool { return m_desc->is_abstract; }
    [[nodiscard]] auto is_final() const -> bool { return m_desc->is_final; }
    [[nodiscard]] auto is_aggregate() const -> bool { return m_desc->is_aggregate; }
    [[nodiscard]] auto is_empty() const -> bool { return m_desc->is_empty; }
    [[nodiscard]] auto is_scoped_enum() const -> bool { return m_desc->is_scoped_enum; }

    [[nodiscard]] auto get_size() const -> std::size_t { return m_desc->size; }
    [[nodiscard]] auto get_alignment() const -> std::size_t { return m_desc->alignment; }
    [[nodiscard]] auto modifiers() const -> Modifier { return Modifier(m_desc->modifiers); }

    // ── C++-specific composition / qualification (Phase 5) ──

    /// T* -> T; None for non-pointer types.
    [[nodiscard]] auto get_pointee_type() const -> Option<const Class&>
    {
        if (m_desc->pointee == nullptr)
        {
            return None{};
        }

        return *m_desc->pointee->self;
    }

    /// T[N] -> T; None for non-array types.
    [[nodiscard]] auto get_element_type() const -> Option<const Class&>
    {
        if (m_desc->element == nullptr)
        {
            return None{};
        }

        return *m_desc->element->self;
    }

    /// T[N] -> N; T[] -> Some(0); None for non-array types and for the runtime
    /// dynamic arrays made by Array::new_instance (their length is per object:
    /// use Array::get_length).
    [[nodiscard]] auto get_array_extent() const -> Option<std::size_t>
    {
        if (m_desc->kind != TypeKind::Array || m_desc->is_dynamic_array)
        {
            return None{};
        }

        return m_desc->array_extent;
    }

    /// Number of array dimensions; 0 for non-array types.
    [[nodiscard]] auto get_array_rank() const -> std::size_t { return m_desc->array_rank; }

    /// M C::* -> C; None for non-member-pointer types.
    [[nodiscard]] auto get_pointee_class_type() const -> Option<const Class&>
    {
        if (m_desc->pointee_class == nullptr)
        {
            return None{};
        }

        return *m_desc->pointee_class->self;
    }

    [[nodiscard]] auto is_const_qualified() const -> bool { return m_desc->is_const_qualified; }
    [[nodiscard]] auto is_volatile_qualified() const -> bool { return m_desc->is_volatile_qualified; }

    /// The type with top-level cv stripped; None when the type is not cv-qualified.
    [[nodiscard]] auto get_unqualified_type() const -> Option<const Class&>
    {
        if (m_desc->unqualified == nullptr)
        {
            return None{};
        }

        return *m_desc->unqualified->self;
    }

    /// Lvalue(&) / Rvalue(&&) for reference types; None otherwise.
    [[nodiscard]] auto get_reference_kind() const -> RefQualifier { return m_desc->reference_kind; }

    /// T& / T&& -> T (cv of the referred type is preserved); None for non-references.
    [[nodiscard]] auto get_referenced_type() const -> Option<const Class&>
    {
        if (m_desc->referenced == nullptr)
        {
            return None{};
        }

        return *m_desc->referenced->self;
    }

    /// enum -> underlying integer type; None for non-enum types.
    [[nodiscard]] auto get_underlying_type() const -> Option<const Class&>
    {
        if (m_desc->underlying == nullptr)
        {
            return None{};
        }

        return *m_desc->underlying->self;
    }

    /// Annotations on this type. Empty for pointers, references, arrays, and
    /// fundamentals. A cv-qualified class reports the unqualified type's annotations.
    /// Annotations are not inherited.
    [[nodiscard]] auto get_annotations() const -> std::span<const Annotation>
    {
        return detail::annotation_span(m_desc->annotations, m_desc->annotation_count);
    }

    /// The first annotation of type T, or None. Further copies of the same type
    /// stay in get_annotations().
    template<typename T>
    [[nodiscard]] auto get_annotation() const -> Option<const T&>
    {
        return detail::first_annotation<T>(get_annotations());
    }

    /// Enumerators of an enum type; empty for non-enum types.
    [[nodiscard]] auto get_enumerators() const -> std::span<const Enumerator>
    {
        return std::span<const Enumerator>(static_cast<const Enumerator*>(m_desc->enumerators),
                                           m_desc->enumerator_count);
    }

    /// Name -> enumerator value (aligned with Java Enum.valueOf); None when not found.
    [[nodiscard]] auto enum_from_name(std::string_view name) const -> Option<Object>
    {
        if (!is_enum() || m_desc->enum_make == nullptr)
        {
            return None{};
        }

        for (const Enumerator& value : get_enumerators())
        {
            if (value.get_name() == name)
            {
                return Object::from_raw(m_desc->enum_make(value.to_uint64()), m_desc.get());
            }
        }

        return None{};
    }

    /**
     * @brief Numeric value -> enumerator (None when the value is not an enumerator)
     * @tparam T Any integer type (bool and character types excluded); the value is
     *         compared by its mathematical value, so `-1` never matches an unsigned
     *         enumerator and `0xFFFFFFFFFFFFFFFFull` needs no signed cast.
     */
    template<typename T>
        requires (std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
    [[nodiscard]] auto enum_from_value(T value) const -> Option<Object>
    {
        if constexpr (std::is_signed_v<T>)
        {
            return enum_from_integer(value < 0, static_cast<std::int64_t>(value), static_cast<std::uint64_t>(value));
        }
        else
        {
            return enum_from_integer(false, 0, static_cast<std::uint64_t>(value));
        }
    }

    /// Enumerator value -> name (None when the type or value does not match).
    [[nodiscard]] auto enum_name_of(ObjectRef value) const -> Option<std::string_view>
    {
        if (!is_enum() || value.is_null() || value.descriptor() != m_desc || m_desc->enum_read == nullptr)
        {
            return None{};
        }

        const std::uint64_t bits = m_desc->enum_read(value.raw_data());
        for (const Enumerator& candidate : get_enumerators())
        {
            if (candidate.to_uint64() == bits)
            {
                return candidate.get_name();
            }
        }

        return None{};
    }

    /// Public data members declared by this class.
    [[nodiscard]] auto get_fields() const -> std::span<const Field>
    {
        return std::span<const Field>(static_cast<const Field*>(m_desc->fields), m_desc->field_count);
    }

    /// Looks up a public data member by name.
    [[nodiscard]] auto get_field(std::string_view name) const -> Option<Field>
    {
        for (const Field& field : get_fields())
        {
            if (field.get_name() == name)
            {
                return field;
            }
        }

        return None{};
    }

    /// Public member functions declared by this class.
    [[nodiscard]] auto get_methods() const -> std::span<const Method>
    {
        return std::span<const Method>(static_cast<const Method*>(m_desc->methods), m_desc->method_count);
    }

    /**
     * @brief Looks up the public member function with the given name
     * @return NotFound when there is none; Ambiguous when the name is overloaded
     *         (pick an overload with one of the parameter-typed lookups).
     * @note All method lookups share this contract: they never silently choose
     *       among several candidates. A unique name is returned whatever its
     *       parameters are: this is "the method called `name`", **not** "the
     *       overload without parameters". For the nullary overload write
     *       `get_method("name", {})` or `get_method<>("name")`.
     */
    [[nodiscard]] auto get_method(std::string_view name) const -> Result<Method, Error>
    {
        return find_method_by_name(get_methods(), name);
    }

    /// All data members declared by this class (including private/protected).
    [[nodiscard]] auto get_declared_fields() const -> std::span<const Field>
    {
        return std::span<const Field>(static_cast<const Field*>(m_desc->declared_fields), m_desc->declared_field_count);
    }

    /// Looks up a declared data member by name (any access level).
    [[nodiscard]] auto get_declared_field(std::string_view name) const -> Option<Field>
    {
        for (const Field& field : get_declared_fields())
        {
            if (field.get_name() == name)
            {
                return field;
            }
        }

        return None{};
    }

    /// All member functions declared by this class (including private/protected).
    [[nodiscard]] auto get_declared_methods() const -> std::span<const Method>
    {
        return std::span<const Method>(static_cast<const Method*>(m_desc->declared_methods), m_desc->declared_method_count);
    }

    /// Like get_method(name) but over the declared view (any access level).
    [[nodiscard]] auto get_declared_method(std::string_view name) const -> Result<Method, Error>
    {
        return find_method_by_name(get_declared_methods(), name);
    }

    /**
     * @brief Looks up a public member function by exact parameter types (Class* compare)
     * @note cv / ref qualifiers are not part of this key: overloads that differ
     *       only there (`f(int)` / `f(int) const`) are Ambiguous. Use the
     *       MemberQualifiers overload to select one.
     */
    [[nodiscard]] auto get_method(std::string_view name,
                                  std::initializer_list<const Class*> param_types) const -> Result<Method, Error>
    {
        return find_method(get_methods(), name, as_span(param_types));
    }

    /// Looks up a declared member function by exact parameter types (any access level).
    [[nodiscard]] auto get_declared_method(std::string_view name,
                                           std::initializer_list<const Class*> param_types) const -> Result<Method, Error>
    {
        return find_method(get_declared_methods(), name, as_span(param_types));
    }

    /// Looks up a public member function by parameter types and cv / ref qualification (exact).
    [[nodiscard]] auto get_method(std::string_view name,
                                  std::initializer_list<const Class*> param_types,
                                  MemberQualifiers qualifiers) const -> Result<Method, Error>
    {
        return find_method(get_methods(), name, as_span(param_types), qualifiers);
    }

    /// @copydoc get_method(std::string_view, std::initializer_list<const Class*>, MemberQualifiers)
    [[nodiscard]] auto get_declared_method(std::string_view name,
                                           std::initializer_list<const Class*> param_types,
                                           MemberQualifiers qualifiers) const -> Result<Method, Error>
    {
        return find_method(get_declared_methods(), name, as_span(param_types), qualifiers);
    }

    /**
     * @brief Looks up a public member function by parameter *type names*
     * @param name Member function name
     * @param param_type_names Qualified names, resolved through the registry
     * @return The method on an exact match; NotFound when nothing matches or a
     *         name is unregistered; Ambiguous as for the Class* overload.
     * @note Slower than the Class* overload (string -> Class* resolution
     *       followed by pointer comparison); cache the result on hot paths.
     * @note Deliberately a template with a defaulted parameter: it never takes
     *       part in deduction, but as a template it loses to the non-template
     *       `Class*` overload on an empty list, so `get_method("f", {})` means
     *       "the overload with no parameters" instead of being ambiguous.
     */
    template<typename = void>
    [[nodiscard]] auto get_method(std::string_view name,
                                  std::initializer_list<std::string_view> param_type_names) const -> Result<Method, Error>
    {
        return find_method_by_type_names(get_methods(), name, param_type_names);
    }

    /// @copydoc get_method(std::string_view, std::initializer_list<std::string_view>)
    template<typename = void>
    [[nodiscard]] auto get_declared_method(std::string_view name,
                                           std::initializer_list<std::string_view> param_type_names) const -> Result<Method, Error>
    {
        return find_method_by_type_names(get_declared_methods(), name, param_type_names);
    }

    /**
     * @brief Compile-time overload resolution for public member functions
     * @tparam Params Formal parameter types (exact pointer match, no conversions)
     */
    template<typename... Params>
    [[nodiscard]] auto get_method(std::string_view name) const -> Result<Method, Error>
    {
        const std::array<const Class*, sizeof...(Params)> types{ std::addressof(class_of<Params>())... };
        return find_method(get_methods(), name, as_span(types));
    }

    template<typename... Params>
    [[nodiscard]] auto get_declared_method(std::string_view name) const -> Result<Method, Error>
    {
        const std::array<const Class*, sizeof...(Params)> types{ std::addressof(class_of<Params>())... };
        return find_method(get_declared_methods(), name, as_span(types));
    }

    /// Same as above but with the name as a compile-time string constant.
    template<FixedString Name, typename... Params>
    [[nodiscard]] auto get_method() const -> Result<Method, Error>
    {
        return get_method<Params...>(std::string_view(Name));
    }

    template<FixedString Name, typename... Params>
    [[nodiscard]] auto get_declared_method() const -> Result<Method, Error>
    {
        return get_declared_method<Params...>(std::string_view(Name));
    }

    /// Field lookup validated against an expected type (mismatch -> None).
    template<typename T>
    [[nodiscard]] auto get_field(std::string_view name) const -> Option<Field>
    {
        Option<Field> field = get_field(name);
        if (field.is_none() || field.unwrap().get_type() != class_of<T>())
        {
            return None{};
        }

        return field;
    }

    /// @copydoc get_field
    template<typename T>
    [[nodiscard]] auto get_declared_field(std::string_view name) const -> Option<Field>
    {
        Option<Field> field = get_declared_field(name);
        if (field.is_none() || field.unwrap().get_type() != class_of<T>())
        {
            return None{};
        }

        return field;
    }

    // ── Inherited members ──
    //
    // get_fields() / get_methods() list only this class's own public members.
    // The *_recursive lookups walk the accessible (public) bases as well, with C++
    // name-lookup semantics:
    //  - a member found in a class hides members of the same name in its bases;
    //  - several bases offering different members, or the same non-static member in
    //    different subobjects (a non-virtual diamond), are Ambiguous; a shared
    //    virtual base is one subobject, so its member is found once;
    //  - the dominance rule for virtual bases is not applied: a base overriding a
    //    member of a shared virtual base next to a sibling that inherits it is Ambiguous.
    // The member found belongs to get_declaring_class(); reaching it from an object of
    // this class goes through cast_to(), e.g.
    //   `field.get(cls.cast_to(obj, field.get_declaring_class()).unwrap())`.
    // Protected / private bases are not traversed (the same graph as cast_to()).

private:
    // Per-class lookups handed to find_recursive (NotFound = keep walking the bases).
    struct OwnFieldByName
    {
        std::string_view name;

        [[nodiscard]] auto operator()(const Class& cls) const -> Result<Field, Error>
        {
            Option<Field> own = cls.get_field(name);
            if (own.is_none())
            {
                return Error{ErrorKind::NotFound,
                             std::format("class '{}' has no data member '{}'", cls.get_qualified_name(), name)};
            }

            return own.unwrap();
        }
    };

    struct OwnMethodByName
    {
        std::string_view name;

        [[nodiscard]] auto operator()(const Class& cls) const -> Result<Method, Error>
        {
            return cls.get_method(name);
        }
    };

    struct OwnMethodByParameters
    {
        std::string_view                 name;
        std::span<const Class* const>    param_types;

        [[nodiscard]] auto operator()(const Class& cls) const -> Result<Method, Error>
        {
            return cls.find_method(cls.get_methods(), name, param_types);
        }
    };

public:
    /**
     * @brief Looks up a public data member here or in the public bases
     * @return NotFound, or Ambiguous (see the rules above)
     */
    [[nodiscard]] auto get_field_recursive(std::string_view name) const -> Result<Field, Error>
    {
        return find_recursive<Field>(OwnFieldByName{ name });
    }

    /**
     * @brief Looks up the public member function called `name` here or in the public bases
     * @return NotFound; Ambiguous when the nearest class declaring the name overloads it
     *         (use the parameter-typed overload) or several bases offer it.
     */
    [[nodiscard]] auto get_method_recursive(std::string_view name) const -> Result<Method, Error>
    {
        return find_recursive<Method>(OwnMethodByName{ name });
    }

    /**
     * @brief Like get_method_recursive(name), selecting by exact parameter types
     * @note The parameter list is part of the key at every level, so (unlike C++ name
     *       lookup) a same-named function with other parameters in a derived class does
     *       not hide the base overload.
     */
    [[nodiscard]] auto get_method_recursive(std::string_view name,
                                            std::initializer_list<const Class*> param_types) const -> Result<Method, Error>
    {
        return find_recursive<Method>(OwnMethodByParameters{ name, as_span(param_types) });
    }

    // ── Constructors (Java-style double view) ──

    /// Public constructors declared by this class.
    [[nodiscard]] auto get_constructors() const -> std::span<const Constructor>
    {
        return std::span<const Constructor>(static_cast<const Constructor*>(m_desc->ctors), m_desc->ctor_count);
    }

    /// All constructors declared by this class (including private/protected).
    [[nodiscard]] auto get_declared_constructors() const -> std::span<const Constructor>
    {
        return std::span<const Constructor>(static_cast<const Constructor*>(m_desc->declared_ctors),
                                            m_desc->declared_ctor_count);
    }

    /// Looks up a public constructor by exact parameter types (Class* compare).
    [[nodiscard]] auto get_constructor(std::initializer_list<const Class*> param_types = {}) const -> Option<Constructor>
    {
        return find_constructor(get_constructors(), as_span(param_types));
    }

    /// Looks up a declared constructor by exact parameter types (any access level).
    [[nodiscard]] auto get_declared_constructor(std::initializer_list<const Class*> param_types = {}) const -> Option<Constructor>
    {
        return find_constructor(get_declared_constructors(), as_span(param_types));
    }

    /**
     * @brief Looks up a public constructor by parameter *type names*
     * @param param_type_names Qualified names, resolved through the registry
     * @return Some(constructor) on an exact match; None when a name is unregistered
     */
    [[nodiscard]] auto get_constructor(std::initializer_list<std::string_view> param_type_names) const -> Option<Constructor>
    {
        return find_constructor_by_type_names(get_constructors(), param_type_names);
    }

    /// @copydoc get_constructor(std::initializer_list<std::string_view>)
    [[nodiscard]] auto get_declared_constructor(std::initializer_list<std::string_view> param_type_names) const -> Option<Constructor>
    {
        return find_constructor_by_type_names(get_declared_constructors(), param_type_names);
    }

    /// Compile-time overload resolution for public constructors.
    template<typename... Params>
    [[nodiscard]] auto get_constructor() const -> Option<Constructor>
    {
        const std::array<const Class*, sizeof...(Params)> types{ std::addressof(class_of<Params>())... };
        return find_constructor(get_constructors(), as_span(types));
    }

    template<typename... Params>
    [[nodiscard]] auto get_declared_constructor() const -> Option<Constructor>
    {
        const std::array<const Class*, sizeof...(Params)> types{ std::addressof(class_of<Params>())... };
        return find_constructor(get_declared_constructors(), as_span(types));
    }

    /**
     * @brief Constructs an owning instance by exact-matching a public constructor
     * @param args Actual arguments (lvalue views), matched on the parameters'
     *             decayed types and reference binding: a const argument does
     *             not bind to `T&`, and `T&&` is never chosen automatically
     *             (use get_constructor<T&&>() and call it directly to move).
     * @return Ambiguous when several usable constructors match; deleted
     *         candidates are skipped (NotSupported only if nothing else
     *         matches); NotFound when no constructor matches.
     */
    [[nodiscard]] auto new_instance(Args args = {}) const -> Outcome<Object>
    {
        if (is_abstract())
        {
            return Error{ErrorKind::NotSupported,
                         std::format("Class::new_instance({}): cannot instantiate an abstract class",
                                     get_qualified_name())};
        }

        const Constructor* chosen = nullptr;
        bool deleted_match = false;
        for (const Constructor& ctor : get_constructors())
        {
            if (!ctor.accepts(args))
            {
                continue;
            }

            if (ctor.is_deleted())
            {
                deleted_match = true; // does not shadow a usable candidate
                continue;
            }

            if (chosen != nullptr)
            {
                return Error{ErrorKind::Ambiguous,
                             std::format("Class::new_instance({}): several constructors accept the arguments {}: "
                                         "'{}' and '{}'; call one of them directly",
                                         get_qualified_name(), detail::describe_arguments(args),
                                         chosen->get_signature(), ctor.get_signature())};
            }

            chosen = &ctor;
        }

        if (chosen != nullptr)
        {
            return chosen->new_instance(std::move(args));
        }

        if (deleted_match)
        {
            return Error{ErrorKind::NotSupported,
                         std::format("Class::new_instance({}): the constructor matching the arguments {} is deleted",
                                     get_qualified_name(), detail::describe_arguments(args))};
        }

        return Error{ErrorKind::NotFound,
                     std::format("Class::new_instance({}): no public constructor accepts the arguments {} "
                                 "(argument types must match the parameters exactly)",
                                 get_qualified_name(), detail::describe_arguments(args))};
    }

    /// Direct base classes, in declaration order (C++-specific; supports multiple inheritance).
    [[nodiscard]] auto get_bases() const -> std::span<const BaseClass>
    {
        return std::span<const BaseClass>(static_cast<const BaseClass*>(m_desc->bases), m_desc->base_count);
    }

    /**
     * @brief Java-compatible convenience: the sole non-virtual direct base class
     * @return Some(base) if there is exactly one and it is non-virtual, else None
     * @note Use get_bases() for the general (multiple / virtual inheritance) case.
     */
    [[nodiscard]] auto get_superclass() const -> Option<const Class&>
    {
        const std::span<const BaseClass> bases = get_bases();
        if (bases.size() != 1 || bases[0].is_virtual())
        {
            return None{};
        }

        return *bases[0].type;
    }

    /**
     * @brief Finds a direct base class by exact type
     * @tparam Base The base type to look for (e.g. B in `A : B, C`)
     * @return Some(base) if Base is a direct base of this class, else None
     */
    template<typename Base>
    [[nodiscard]] auto get_base() const -> Option<const Class&>
    {
        const Class& wanted = class_of<Base>();
        for (const BaseClass& base : get_bases())
        {
            if (*base.type == wanted)
            {
                return *base.type;
            }
        }

        return None{};
    }

    /**
     * @brief Finds a direct base class by Class handle (exact match)
     */
    [[nodiscard]] auto get_base(const Class& base) const -> Option<const Class&>
    {
        for (const BaseClass& candidate : get_bases())
        {
            if (*candidate.type == base)
            {
                return *candidate.type;
            }
        }

        return None{};
    }

    /**
     * @brief Finds a direct base class by qualified type name
     * @param qualified_name Base class name, resolved through the registry
     * @return Some(base) when the name resolves to a direct base, else None
     */
    [[nodiscard]] auto get_base(std::string_view qualified_name) const -> Option<const Class&>
    {
        Option<const Class&> resolved = for_name(qualified_name);
        if (resolved.is_none())
        {
            return None{};
        }

        return get_base(resolved.unwrap());
    }

    /**
     * @brief Whether `other` can be assigned to this class (this is an accessible base of other)
     * @note Type-level test over the same graph as cast_to(): a path through a
     *       protected / private base does not count, so a true result means
     *       cast_to() can reach this class (unless several paths make it
     *       Ambiguous). Use is_base_of_ignoring_access() for the plain graph.
     */
    [[nodiscard]] auto is_assignable_from(const Class& other) const -> bool
    {
        return reaches(other, *this, true);
    }

    /// Whether this class is a base of `other` through any path, regardless of access.
    [[nodiscard]] auto is_base_of_ignoring_access(const Class& other) const -> bool
    {
        return reaches(other, *this, false);
    }

    /**
     * @brief Static instance test: whether obj's (static) type derives from this
     * @note Unlike Java this is not a dynamic type test (speculo has no RTTI);
     *       it answers "can obj's type be assigned to this class".
     */
    [[nodiscard]] auto is_instance(ObjectRef obj) const -> bool
    {
        return !obj.is_null() && is_assignable_from(obj.type());
    }

    /**
     * @brief Upcast: adjusts obj's pointer to the subobject of type `base`
     * @param obj  Must be typed exactly as this class (obj.type() == *this); no dynamic adjustment
     * @param base A base class of this class (direct or indirect)
     * @return A view referring to the base subobject; constness is preserved.
     *         Ambiguous when multiple paths reach `base`; `base` is not a base
     *         of this class -> TypeMismatch; `base` is a base only through
     *         protected / private inheritance -> NotSupported.
     * @note Only *upcasts* are supported (down/cross casts need RTTI). A path
     *       through an inaccessible (protected/private) base is not traversable.
     */
    [[nodiscard]] auto cast_to(ObjectRef obj, const Class& base) const -> Result<ObjectRef, Error>
    {
        if (obj.is_null())
        {
            return Error{ErrorKind::NullInstance,
                         std::format("Class::cast_to({} -> {}): the object is null", get_qualified_name(),
                                     base.get_qualified_name())};
        }

        if (obj.descriptor() != m_desc)
        {
            return Error{ErrorKind::TypeMismatch,
                         std::format("Class::cast_to({} -> {}): the object has type '{}', not '{}'",
                                     get_qualified_name(), base.get_qualified_name(),
                                     obj.descriptor()->qualified_name, get_qualified_name())};
        }

        void* found = nullptr;
        int   paths = 0;
        find_upcast(*this, base, const_cast<void*>(obj.raw_data()), found, paths);

        if (paths == 0)
        {
            if (reaches(*this, base, false))
            {
                return Error{ErrorKind::NotSupported,
                             std::format("Class::cast_to({} -> {}): the target is a base only through protected or "
                                         "private inheritance",
                                         get_qualified_name(), base.get_qualified_name())};
            }

            return Error{ErrorKind::TypeMismatch,
                         std::format("Class::cast_to({} -> {}): '{}' is not a base of '{}'", get_qualified_name(),
                                     base.get_qualified_name(), base.get_qualified_name(), get_qualified_name())};
        }

        if (paths > 1)
        {
            return Error{ErrorKind::Ambiguous,
                         std::format("Class::cast_to({} -> {}): several inheritance paths reach the target",
                                     get_qualified_name(), base.get_qualified_name())};
        }

        return ObjectRef(found, base.descriptor(), obj.is_const(), obj.is_volatile());
    }

    // ── Type-system structure: templates, function types, aggregates, nesting ──
    // (v2, @.agents/api_design.md §1–§4)

    /**
     * @brief Whether this type is an instance of a class template
     * @return true for `std::vector<int>`; false for ordinary classes, pointers,
     *         arrays, and cv-qualified or reference types (`const std::vector<int>`
     *         is a cv-qualified type, not a specialization -- query the
     *         unqualified type through get_unqualified_type()).
     */
    [[nodiscard]] auto is_template_specialization() const -> bool { return m_desc->primary_template != nullptr; }

    /**
     * @brief The primary template this type was instantiated from
     * @return Some(template) with `kind() == TypeKind::Template`; None for
     *         non-specializations
     * @note A primary template has a declaration-level descriptor: name only,
     *       size / alignment 0, and empty member tables (its members' types
     *       depend on the arguments, so there is nothing to materialize).
     */
    [[nodiscard]] auto get_primary_template() const -> Option<const Class&>
    {
        if (m_desc->primary_template == nullptr)
        {
            return None{};
        }

        return *m_desc->primary_template->self;
    }

    /// Template arguments in declaration order, defaulted arguments included
    /// (`std::vector<int>` has two: `int` and `std::allocator<int>`). Empty for
    /// non-specializations.
    [[nodiscard]] auto get_template_arguments() const -> std::span<const TemplateArgument>
    {
        return std::span<const TemplateArgument>(static_cast<const TemplateArgument*>(m_desc->template_arguments),
                                                 m_desc->template_argument_count);
    }

    /// The argument at `index` when it is a *type* argument; None when the index
    /// is out of range or the argument is of another kind.
    [[nodiscard]] auto get_template_argument_type(std::size_t index) const -> Option<const Class&>
    {
        const std::span<const TemplateArgument> arguments = get_template_arguments();
        if (index >= arguments.size() || arguments[index].kind() != TemplateArgumentKind::Type)
        {
            return None{};
        }

        return arguments[index].as_type();
    }

    // ── Function types (api_design §2.2) ──

    /// The return type of a function type; None unless `kind() == TypeKind::Function`.
    [[nodiscard]] auto get_return_type() const -> Option<const Class&>
    {
        if (m_desc->function_return == nullptr)
        {
            return None{};
        }

        return *m_desc->function_return->self;
    }

    /// The declared parameter types of a function type (`...` is not a parameter);
    /// empty unless `kind() == TypeKind::Function`.
    [[nodiscard]] auto get_parameter_types() const -> std::span<const Class* const>
    {
        return std::span<const Class* const>(m_desc->function_parameters, m_desc->function_parameter_count);
    }

    /// The number of fixed parameters of a function type; 0 for other types.
    [[nodiscard]] auto get_parameter_count() const -> std::size_t { return m_desc->function_parameter_count; }

    /// Whether a function type takes C-style `...`; false for other types.
    [[nodiscard]] auto is_variadic() const -> bool { return m_desc->function_is_variadic; }

    /// Whether a function type is `noexcept`; false for other types.
    [[nodiscard]] auto is_noexcept() const -> bool { return m_desc->function_is_noexcept; }

    // ── Aggregate construction (api_design §3) ──

    /**
     * @brief Constructs an object by aggregate initialization (`T{a, b, c}`)
     * @param args One argument per initializer slot: public bases in declaration
     *             order first, then this class's non-static data members in
     *             declaration order
     * @return NotSupported when the type is not an aggregate, or when some slot
     *         cannot be initialized from a value (protected / private base, array
     *         or rvalue-reference member, union); InvalidArgument for a
     *         wrong argument count; TypeMismatch for a mismatched argument
     * @note Default member initializers are not simulated: every slot must be
     *       given (see @.agents/api_design.md §3.2).
     */
    [[nodiscard]] auto new_aggregate(Args args = {}) const -> Outcome<Object>
    {
        if (!is_aggregate() || m_desc->aggregate_initializer == nullptr)
        {
            return Error{ErrorKind::NotSupported,
                         std::format("Class::new_aggregate({}): the type is not an aggregate, or a base / member "
                                     "cannot be initialized from a value",
                                     get_qualified_name())};
        }

        return aggregate_initializer().new_instance(std::move(args));
    }

    /**
     * @brief A pseudo constructor describing the aggregate initialization slots
     * @return Some(constructor) whose parameter list is the initializer list
     *         (public bases first, then non-static data members); None when the
     *         type is not an aggregate, or a slot cannot be initialized
     * @note It is a real Constructor handle: new_instance() / construct_at() /
     *       destroy_at() behave exactly like new_aggregate().
     */
    [[nodiscard]] auto get_aggregate_initializer() const -> Option<Constructor>
    {
        if (m_desc->aggregate_initializer == nullptr)
        {
            return None{};
        }

        return aggregate_initializer();
    }

    // ── Nested types (api_design §4) ──

    /// The classes, unions and enums declared inside this type, in declaration
    /// order (own declarations only; inherited names are not included). Type
    /// aliases and uninstantiated member class templates are skipped, and the
    /// access level of each nested type is its own `modifiers()`.
    [[nodiscard]] auto get_declared_classes() const -> std::span<const Class* const>
    {
        return std::span<const Class* const>(m_desc->nested_types, m_desc->nested_type_count);
    }

    /// The class this type is declared in; None at namespace scope.
    [[nodiscard]] auto get_enclosing_class() const -> Option<const Class&>
    {
        if (m_desc->enclosing_class == nullptr)
        {
            return None{};
        }

        return *m_desc->enclosing_class->self;
    }

    // ── Layout and capability predicates (api_design §5) ──
    //
    // Every predicate is evaluated while the type is materialized and stored as
    // one bit; a query is a bit test. Non-class types answer false (they never
    // panic). The four predicates that need *both* types at compile time are free
    // function templates instead (see below the class), because a runtime
    // `const Class&` cannot be turned back into a template argument.

    [[nodiscard]] auto is_trivially_copyable() const -> bool
    {
        return detail::has_capability(m_desc->capabilities, detail::Capability::TriviallyCopyable);
    }

    [[nodiscard]] auto is_trivially_default_constructible() const -> bool
    {
        return detail::has_capability(m_desc->capabilities, detail::Capability::TriviallyDefaultConstructible);
    }

    [[nodiscard]] auto is_trivially_destructible() const -> bool
    {
        return detail::has_capability(m_desc->capabilities, detail::Capability::TriviallyDestructible);
    }

    /// Whether deleting through a pointer to this class runs the derived destructor.
    [[nodiscard]] auto has_virtual_destructor() const -> bool
    {
        return detail::has_capability(m_desc->capabilities, detail::Capability::VirtualDestructor);
    }

    [[nodiscard]] auto is_standard_layout() const -> bool
    {
        return detail::has_capability(m_desc->capabilities, detail::Capability::StandardLayout);
    }

    [[nodiscard]] auto has_unique_object_representations() const -> bool
    {
        return detail::has_capability(m_desc->capabilities, detail::Capability::UniqueObjectRepresentations);
    }

    /**
     * @brief Library-synthesized: same size and both trivially copyable
     * @note There is no standard trait for this; it is the condition for
     *       `std::bit_cast` in both directions.
     */
    [[nodiscard]] auto is_bit_castable_to(const Class& other) const -> bool
    {
        return m_desc->size == other.m_desc->size && is_trivially_copyable() && other.is_trivially_copyable();
    }

    [[nodiscard]] auto operator==(const Class& other) const noexcept -> bool { return m_desc == other.m_desc; }
    [[nodiscard]] auto operator!=(const Class& other) const noexcept -> bool { return m_desc != other.m_desc; }

private:
    [[nodiscard]] static auto as_span(std::initializer_list<const Class*> values) noexcept -> std::span<const Class* const>
    {
        return std::span<const Class* const>(values.begin(), values.size());
    }

    template<std::size_t N>
    [[nodiscard]] static auto as_span(const std::array<const Class*, N>& values) noexcept -> std::span<const Class* const>
    {
        return std::span<const Class* const>(values.data(), values.size());
    }

    [[nodiscard]] static auto types_match(std::span<const Class* const> lhs,
                                          std::span<const Class* const> rhs) noexcept -> bool
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }

        for (std::size_t i = 0; i < lhs.size(); ++i)
        {
            if (lhs[i] != rhs[i])
            {
                return false;
            }
        }

        return true;
    }

    /// Collects the candidates of a method lookup: NotFound when none was
    /// offered, Ambiguous when several were, the method otherwise.
    struct MethodSelector
    {
        const Method* match = nullptr;
        bool          ambiguous = false;

        void offer(const Method& method) noexcept
        {
            if (match != nullptr)
            {
                ambiguous = true;
            }
            else
            {
                match = &method;
            }
        }

        [[nodiscard]] auto finish(std::string_view owner, std::string_view name) const -> Result<Method, Error>
        {
            if (ambiguous)
            {
                return Error{ErrorKind::Ambiguous,
                             std::format("method '{}::{}': the lookup matches several overloads; pass the parameter "
                                         "types (and cv / ref qualifiers) to select one",
                                         owner, name)};
            }

            if (match == nullptr)
            {
                return Error{ErrorKind::NotFound,
                             std::format("class '{}' has no member function '{}' matching the lookup", owner, name)};
            }

            return *match;
        }
    };

    [[nodiscard]] auto find_method(std::span<const Method> methods,
                                   std::string_view name,
                                   std::span<const Class* const> param_types) const -> Result<Method, Error>
    {
        MethodSelector selector;
        for (const Method& method : methods)
        {
            if (method.get_name() == name && types_match(method.get_parameter_types(), param_types))
            {
                selector.offer(method);
            }
        }

        return selector.finish(get_qualified_name(), name);
    }

    [[nodiscard]] auto find_method(std::span<const Method> methods,
                                   std::string_view name,
                                   std::span<const Class* const> param_types,
                                   MemberQualifiers qualifiers) const -> Result<Method, Error>
    {
        MethodSelector selector;
        for (const Method& method : methods)
        {
            if (method.get_name() == name
                && types_match(method.get_parameter_types(), param_types)
                && method.is_const() == qualifiers.is_const
                && method.is_volatile() == qualifiers.is_volatile
                && method.get_ref_qualifier() == qualifiers.ref)
            {
                selector.offer(method);
            }
        }

        return selector.finish(get_qualified_name(), name);
    }

    [[nodiscard]] auto find_method_by_name(std::span<const Method> methods, std::string_view name) const -> Result<Method, Error>
    {
        MethodSelector selector;
        for (const Method& method : methods)
        {
            if (method.get_name() == name)
            {
                selector.offer(method);
            }
        }

        return selector.finish(get_qualified_name(), name);
    }
    [[nodiscard]] static auto find_constructor(std::span<const Constructor> ctors,
                                               std::span<const Class* const> param_types) -> Option<Constructor>
    {
        for (const Constructor& ctor : ctors)
        {
            if (types_match(ctor.get_parameter_types(), param_types))
            {
                return ctor;
            }
        }

        return None{};
    }

    [[nodiscard]] auto find_method_by_type_names(std::span<const Method> methods,
                                                 std::string_view name,
                                                 std::initializer_list<std::string_view> names) const -> Result<Method, Error>
    {
        // Resolve once, so that an unregistered (or misspelled) name is reported as
        // such instead of looking like "no overload has these parameters".
        std::vector<const Class*> resolved;
        for (std::string_view type_name : names)
        {
            Option<const Class&> found = for_name(type_name);
            if (found.is_none())
            {
                return unregistered_name_error(type_name);
            }

            resolved.push_back(std::addressof(found.unwrap()));
        }

        return find_method(methods, name, std::span<const Class* const>(resolved));
    }

    /// NotFound naming the type name and (a prefix of) what the registry holds.
    [[nodiscard]] static auto unregistered_name_error(std::string_view type_name) -> Error
    {
        constexpr std::size_t kListed = 16;

        const std::vector<std::string_view> known = registered_names();
        std::string message = "type name \"";
        message += type_name;
        message += "\" is not registered (misspelled, or the type was never used with class_of / SPECULO_REGISTER); registered: ";
        for (std::size_t i = 0; i < known.size() && i < kListed; ++i)
        {
            message += i == 0 ? "" : ", ";
            message += known[i];
        }

        if (known.size() > kListed)
        {
            message += ", ... (";
            message += std::to_string(known.size() - kListed);
            message += " more)";
        }

        return Error{ErrorKind::NotFound, std::move(message)};
    }

    [[nodiscard]] static auto find_constructor_by_type_names(std::span<const Constructor> ctors,
                                                             std::initializer_list<std::string_view> names) -> Option<Constructor>
    {
        // Resolve each name once (one registry lock each), then compare the Class
        // handles by pointer identity; an unregistered name matches nothing.
        std::vector<const Class*> resolved;
        resolved.reserve(names.size());
        for (std::string_view type_name : names)
        {
            Option<const Class&> found = for_name(type_name);
            if (found.is_none())
            {
                return None{};
            }

            resolved.push_back(std::addressof(found.unwrap()));
        }

        return find_constructor(ctors, std::span<const Class* const>(resolved));
    }

    /// Whether `target` is `from` or one of its bases; with `accessible_only`
    /// only public bases (those with an upcast thunk) are followed.
    [[nodiscard]] static auto reaches(const Class& from, const Class& target, bool accessible_only) -> bool
    {
        if (from == target)
        {
            return true;
        }

        for (const BaseClass& base : from.get_bases())
        {
            if (base.type == nullptr || (accessible_only && base.cast == nullptr))
            {
                continue;
            }

            if (reaches(*base.type, target, accessible_only))
            {
                return true;
            }
        }

        return false;
    }

    static void find_upcast(const Class& from, const Class& target, void* pointer, void*& found, int& paths)
    {
        if (from == target)
        {
            if (paths == 0)
            {
                found = pointer;
                paths = 1;
            }
            else if (pointer != found)
            {
                // Paths that land on the same subobject (a shared virtual base)
                // are one path; distinct subobjects of the type are ambiguous.
                ++paths;
            }
            return;
        }

        for (const BaseClass& base : from.get_bases())
        {
            if (base.cast != nullptr)
            {
                find_upcast(*base.type, target, base.cast(pointer), found, paths);
            }
        }
    }

    /// enum_from_value() core: `negative` says whether the caller's value is below
    /// zero, in which case `svalue` holds it; otherwise `uvalue` holds it.
    [[nodiscard]] auto enum_from_integer(bool negative, std::int64_t svalue, std::uint64_t uvalue) const -> Option<Object>
    {
        if (!is_enum() || m_desc->enum_make == nullptr)
        {
            return None{};
        }

        for (const Enumerator& candidate : get_enumerators())
        {
            bool equal = false;
            if (m_desc->enum_is_unsigned)
            {
                equal = !negative && candidate.to_uint64() == uvalue;
            }
            else if (negative)
            {
                equal = candidate.to_int64() == svalue;
            }
            else
            {
                equal = candidate.to_int64() >= 0 && candidate.to_uint64() == uvalue;
            }

            if (equal)
            {
                return Object::from_raw(m_desc->enum_make(candidate.to_uint64()), m_desc.get());
            }
        }

        return None{};
    }

    // ── Recursive member lookup ──

    [[nodiscard]] static auto member_is_static(const Field& field) -> bool { return field.is_static(); }
    [[nodiscard]] static auto member_is_static(const Method& method) -> bool { return method.is_static_member(); }

    template<typename Member, typename FindOwn>
    static void collect_members(const Class& cls, const std::vector<const Class*>& subobject,
                                std::vector<detail::MemberHit<Member>>& hits, bool& ambiguous, const FindOwn& find_own)
    {
        Result<Member, Error> own = find_own(cls);
        if (own.is_ok())
        {
            Member member = own.unwrap();
            for (const detail::MemberHit<Member>& hit : hits)
            {
                if (hit.member == member && hit.subobject == subobject)
                {
                    return;
                }
            }

            hits.push_back(detail::MemberHit<Member>{ member, subobject });
            return;
        }

        if (own.unwrap_err().kind != ErrorKind::NotFound)
        {
            ambiguous = true; // the name exists here, but does not pick one member
            return;
        }

        for (const BaseClass& base : cls.get_bases())
        {
            if (base.type == nullptr || base.cast == nullptr)
            {
                continue;
            }

            std::vector<const Class*> next;
            if (!base.is_virtual())
            {
                next = subobject;
            }
            next.push_back(base.type);
            collect_members(*base.type, next, hits, ambiguous, find_own);
        }
    }

    template<typename Member, typename FindOwn>
    [[nodiscard]] auto find_recursive(const FindOwn& find_own) const -> Result<Member, Error>
    {
        std::vector<detail::MemberHit<Member>> hits;
        bool ambiguous = false;
        collect_members<Member>(*this, std::vector<const Class*>{}, hits, ambiguous, find_own);

        if (ambiguous)
        {
            return Error{ErrorKind::Ambiguous,
                         std::format("lookup in '{}': the name is overloaded in the nearest class that declares it",
                                     get_qualified_name())};
        }

        if (hits.empty())
        {
            return Error{ErrorKind::NotFound,
                         std::format("lookup in '{}': no member matches in this class or its public bases",
                                     get_qualified_name())};
        }

        bool same_static_member = member_is_static(hits[0].member);
        for (const detail::MemberHit<Member>& hit : hits)
        {
            same_static_member = same_static_member && hit.member == hits[0].member;
        }

        if (hits.size() > 1 && !same_static_member)
        {
            return Error{ErrorKind::Ambiguous,
                         std::format("lookup in '{}': several bases (or base subobjects) provide the member",
                                     get_qualified_name())};
        }

        return hits[0].member;
    }

    friend struct detail::Access;
    friend class ObjectRef;
    friend class Array;

    /// The backing descriptor (materialization layer only).
    [[nodiscard]] auto descriptor() const noexcept -> const detail::TypeDescriptor* { return m_desc.get(); }

    /// The pseudo constructor handle stored in the descriptor (non-null when set).
    [[nodiscard]] auto aggregate_initializer() const -> Constructor
    {
        return *static_cast<const Constructor*>(m_desc->aggregate_initializer);
    }

    explicit constexpr Class(const detail::TypeDescriptor* descriptor) noexcept
        : m_desc(descriptor)
    {
    }

    detail::CheckedDesc<detail::TypeDescriptor> m_desc;
};

// ── Two-type predicates (api_design §5.2) ──
//
// Unlike the single-type predicates above, these four need both types as
// *compile-time* operands, so they are free function templates rather than
// member functions: `class_of<A>().is_layout_compatible<B>()` could only work if
// the handle knew `A`, which a runtime handle does not. See
// @.agents/api_design.md §5.2 and @.agents/review.md「Phase 9」.

/**
 * @brief Whether two standard-layout types can be read through each other
 * @note Same answer as `std::is_layout_compatible_v<A, B>`.
 */
template<typename A, typename B>
    requires (std::is_class_v<A> || std::is_union_v<A>) && (std::is_class_v<B> || std::is_union_v<B>)
[[nodiscard]] consteval auto is_layout_compatible() -> bool
{
    return detail::layout_compatible_types<A, B>();
}

/**
 * @brief Whether a base subobject is pointer-interconvertible with the object
 * @note Same answer as `std::is_pointer_interconvertible_base_of_v<Base, Derived>`;
 *       the operand order here is (derived, base), matching `get_bases()`.
 */
template<typename Derived, typename Base>
    requires std::is_class_v<Derived> && std::is_class_v<Base>
[[nodiscard]] consteval auto is_pointer_interconvertible_with() -> bool
{
    return detail::pointer_interconvertible_types<Derived, Base>();
}

/// Whether `From` converts to `To` implicitly (same as `std::is_convertible_v`).
template<typename From, typename To>
[[nodiscard]] consteval auto is_convertible_to() -> bool
{
    return detail::convertible_types<From, To>();
}

/// Whether `From` converts to `To` implicitly and without throwing anything
/// (same as `std::is_nothrow_convertible_v`).
template<typename From, typename To>
[[nodiscard]] consteval auto is_nothrow_convertible_to() -> bool
{
    return detail::nothrow_convertible_types<From, To>();
}

/**
 * @brief Dynamic operations on C-style arrays (Java {@code java.lang.reflect.Array})
 *
 * Operates on views of `T[N]` objects (whose Class reports the array kind) as
 * well as on owning arrays produced by Array::new_instance.
 */
class Array
{
public:
    /**
     * @brief Allocates an owning dynamic array of `length` default-constructed elements
     * @return The owning array; NotSupported when the element type is not
     *         default-constructible/destructible.
     */
    [[nodiscard]] static auto new_instance(const Class& element_type, std::size_t length) -> Outcome<Object>
    {
        const detail::TypeDescriptor* element = element_type.descriptor();
        if (element == nullptr || element->dynamic_array == nullptr || element->new_array == nullptr)
        {
            return Error{ErrorKind::NotSupported,
                         std::format("Array::new_instance: the element type '{}' cannot back a dynamic array "
                                     "(it must be default-constructible and destructible)",
                                     element_type.get_qualified_name())};
        }

        void* data = element->new_array(length);
        if (data == nullptr)
        {
            return Error{ErrorKind::InvalidArgument,
                         std::format("Array::new_instance: a length of {} elements of '{}' is too large", length,
                                     element_type.get_qualified_name())};
        }

        return Object::from_raw(data, element->dynamic_array);
    }

    /// The array length; TypeMismatch when obj is not an array.
    [[nodiscard]] static auto get_length(ObjectRef array) -> Result<std::size_t, Error>
    {
        const auto view = view_of(array);
        if (view.is_err())
        {
            return view.unwrap_err();
        }

        return view.unwrap().length;
    }

    /// Reads element `index` as a boxed copy (NotSupported for non-copyable elements).
    [[nodiscard]] static auto get(ObjectRef array, std::size_t index) -> Outcome<Object>
    {
        const auto view = bounds_checked(array, index);
        if (view.is_err())
        {
            return view.unwrap_err();
        }

        const detail::TypeDescriptor* element = view.unwrap().element;
        if (array.is_volatile() || view.unwrap().element_is_volatile)
        {
            return Error{ErrorKind::NotSupported,
                         "Array::get() on volatile elements is not supported; use get_ref() and as<volatile T>()"};
        }

        if (element->clone == nullptr)
        {
            return Error{ErrorKind::NotSupported,
                         std::format("Array::get: the element type '{}' is not copy-constructible",
                                     element->qualified_name)};
        }

        return Object::from_raw(element->clone(element_address(array, index)), element);
    }

    /// Writes element `index` from `value` (exact element type required).
    [[nodiscard]] static auto set(ObjectRef array, std::size_t index, ObjectRef value) -> Status
    {
        if (array.is_const())
        {
            return Error{ErrorKind::ReadOnly, "Array::set() called on a const array"};
        }

        const auto view = bounds_checked(array, index);
        if (view.is_err())
        {
            return view.unwrap_err();
        }

        const detail::TypeDescriptor* element = view.unwrap().element;
        if (view.unwrap().element_is_const)
        {
            return Error{ErrorKind::ReadOnly, "Array::set() called on an array of const elements"};
        }

        if (array.is_volatile() || view.unwrap().element_is_volatile)
        {
            return Error{ErrorKind::NotSupported,
                         "Array::set() on volatile elements is not supported; use get_ref() and as<volatile T>()"};
        }

        if (value.is_null() || value.descriptor() != element)
        {
            return Error{ErrorKind::TypeMismatch,
                         value.is_null()
                             ? std::format("Array::set: the value is null, expected '{}'", element->qualified_name)
                             : std::format("Array::set: the value has type '{}', expected '{}' (types must match "
                                           "exactly)",
                                           value.descriptor()->qualified_name, element->qualified_name)};
        }

        if (element->copy_assign == nullptr)
        {
            return Error{ErrorKind::NotSupported,
                         std::format("Array::set: the element type '{}' is not copy-assignable",
                                     element->qualified_name)};
        }

        element->copy_assign(element_address(array, index), value.raw_data());

        return None{};
    }

    /// A view of element `index`; the view's type is the element's unqualified
    /// type, and its is_const() / is_volatile() also cover a cv-qualified element type.
    [[nodiscard]] static auto get_ref(ObjectRef array, std::size_t index) -> Result<ObjectRef, Error>
    {
        const auto view = bounds_checked(array, index);
        if (view.is_err())
        {
            return view.unwrap_err();
        }

        return ObjectRef(element_address(array, index), view.unwrap().element,
                         array.is_const() || view.unwrap().element_is_const,
                         array.is_volatile() || view.unwrap().element_is_volatile);
    }

    /// The element type of an array object; None for non-array objects.
    [[nodiscard]] static auto get_element_type(ObjectRef array) -> Option<const Class&>
    {
        if (array.is_null() || array.descriptor() == nullptr || !array.type().is_array())
        {
            return None{};
        }

        const detail::TypeDescriptor* element = array.descriptor()->element;
        return element != nullptr ? Option<const Class&>(*element->self) : Option<const Class&>(None{});
    }

private:
    struct View
    {
        const detail::TypeDescriptor* element = nullptr; ///< Unqualified (decayed) element type
        bool                          element_is_const = false;
        bool                          element_is_volatile = false;
        std::size_t                   length = 0;
    };

    /// Validates that obj is an array and reports its element type and length.
    [[nodiscard]] static auto view_of(ObjectRef array) -> Result<View, Error>
    {
        if (array.is_null() || array.descriptor() == nullptr)
        {
            return Error{ErrorKind::NullInstance, "Array operation called on a null object"};
        }

        const detail::TypeDescriptor* desc = array.descriptor();
        if (desc->kind != TypeKind::Array || desc->element == nullptr)
        {
            return Error{ErrorKind::TypeMismatch,
                         std::format("Array operation: the object has type '{}', which is not an array",
                                     desc->qualified_name)};
        }

        std::size_t length = desc->array_extent;
        if (desc->is_dynamic_array)
        {
            const char* base = static_cast<const char*>(array.raw_data());
            length = *reinterpret_cast<const std::size_t*>(base - desc->array_data_offset);
        }

        // Object handles are keyed by the unqualified type; the element's top-level
        // cv is folded into the view's constness / volatility instead of the descriptor.
        const detail::TypeDescriptor* element = desc->element;
        return View{ element->decayed, element->is_const_qualified, element->is_volatile_qualified, length };
    }

    /// view_of + range check [0, length).
    [[nodiscard]] static auto bounds_checked(ObjectRef array, std::size_t index) -> Result<View, Error>
    {
        auto view = view_of(array);
        if (view.is_err())
        {
            return view.unwrap_err();
        }

        if (index >= view.unwrap().length)
        {
            return Error{ErrorKind::InvalidArgument,
                         std::format("Array: index {} is out of range (length {})", index, view.unwrap().length)};
        }

        return view;
    }

    [[nodiscard]] static auto element_address(ObjectRef array, std::size_t index) -> void*
    {
        const detail::TypeDescriptor* element = array.descriptor()->element->decayed;
        char* base = const_cast<char*>(static_cast<const char*>(array.raw_data()));
        return base + index * element->size;
    }
};

/**
 * @brief A callable *value*: a function pointer or a member function pointer
 *
 * `Field::get()` on a field holding `void (*)(int)` boxes the pointer in an
 * `Object`, which offers no way to call it. `Callable::of()` turns such a value
 * into a handle that can: argument matching, result boxing and virtual dispatch
 * (a member function pointer called through the object dispatches virtually)
 * match `Method::invoke`.
 *
 * @note Only bare function pointers and member function pointers are handled;
 *       lambdas / `std::function` would need type information the library does
 *       not have (no RTTI).
 */
class Callable
{
public:
    constexpr Callable() noexcept = default;

    /**
     * @brief Views a value whose type is a function pointer or member pointer
     * @return Some(callable) for such a value, None for anything else (and for a
     *         null reference)
     */
    [[nodiscard]] static auto of(ObjectRef value) -> Option<Callable>
    {
        if (value.is_null() || value.descriptor() == nullptr || value.descriptor()->callable == nullptr)
        {
            return None{};
        }

        return Callable(*value.descriptor()->callable, value);
    }

    /// The pointer type itself, e.g. `void (*)(int)` or `void (C::*)(int)`.
    [[nodiscard]] auto get_type() const -> const Class& { return *m_desc->type->self; }

    /// The function type behind the pointer, e.g. `void (int)`; use
    /// `get_return_type()` / `get_parameter_types()` on it for the signature.
    [[nodiscard]] auto get_function_type() const -> const Class& { return *m_desc->function_type->self; }

    [[nodiscard]] auto is_member_function_pointer() const -> bool { return m_desc->is_member_function_pointer; }

    /// The class a member function pointer points into; None for free functions.
    [[nodiscard]] auto get_declaring_class() const -> Option<const Class&>
    {
        if (m_desc->declaring_class == nullptr)
        {
            return None{};
        }

        return *m_desc->declaring_class->self;
    }

    [[nodiscard]] auto returns_reference() const -> bool
    {
        return m_desc->function_type->function_return != nullptr
            && m_desc->function_type->function_return->reference_kind != RefQualifier::None;
    }

    /// Whether invoke() can run this value (false when the result cannot be boxed).
    [[nodiscard]] auto is_invokable() const -> bool { return m_desc->invoke != nullptr; }

    /// Whether invoke_ref() can run this value (reference results only).
    [[nodiscard]] auto is_ref_invokable() const -> bool { return m_desc->invoke_ref != nullptr; }

    /**
     * @brief Calls a free function pointer, boxing the result
     * @return Ok(boxed result); an empty Object for a void function
     * @note NotSupported when this is a member function pointer (use the
     *       two-argument overload).
     */
    [[nodiscard]] auto invoke(Args args = {}) const -> Outcome<Object>
    {
        return box_impl("Callable::invoke", ObjectRef{}, args);
    }

    /**
     * @brief Calls a member function pointer on `self`
     * @param self An object of the pointer's declaring class (exact type; the
     *             handle does not adjust a derived object's pointer)
     * @return Ok(boxed result); NotSupported for a free function pointer
     */
    [[nodiscard]] auto invoke(ObjectRef self, Args args) const -> Outcome<Object>
    {
        return box_impl("Callable::invoke", self, args);
    }

    /// Calls the pointer and views the referent of a reference result (no copy).
    [[nodiscard]] auto invoke_ref(Args args = {}) const -> Outcome<ObjectRef>
    {
        return ref_impl("Callable::invoke_ref", ObjectRef{}, args);
    }

    /// @copydoc invoke_ref(Args) const
    [[nodiscard]] auto invoke_ref(ObjectRef self, Args args) const -> Outcome<ObjectRef>
    {
        return ref_impl("Callable::invoke_ref", self, args);
    }

    /// Same pointer type and the same pointer *value* (byte compare). Two handles
    /// over different storage holding the same function are equal.
    [[nodiscard]] auto operator==(const Callable& other) const noexcept -> bool
    {
        if (m_desc != other.m_desc || m_desc->type->size != other.m_desc->type->size)
        {
            return false;
        }

        return std::memcmp(m_value.raw_data(), other.m_value.raw_data(), m_desc->type->size) == 0;
    }

    [[nodiscard]] auto operator!=(const Callable& other) const noexcept -> bool { return !(*this == other); }

private:
    friend struct detail::Access;

    constexpr Callable(const detail::CallableDesc& desc, ObjectRef value) noexcept
        : m_desc(&desc)
        , m_value(value)
    {
    }

    [[nodiscard]] auto box_impl(std::string_view operation, ObjectRef self, const Args& args) const -> Outcome<Object>
    {
        const Status checked = check_call(operation, self, args);
        if (checked.is_err())
        {
            return checked.unwrap_err();
        }

        if (m_desc->invoke == nullptr)
        {
            return Error{ErrorKind::NotSupported,
                         std::format("{}: the result of '{}' cannot be boxed (use invoke_ref())", operation,
                                     m_desc->type->qualified_name)};
        }

        const detail::ArgumentPointers pointers(args);
        void* const receiver = self.is_null() ? nullptr : const_cast<void*>(self.raw_data());
        void* result = m_desc->invoke(m_value.raw_data(), receiver, pointers.data());
        if (result == nullptr)
        {
            return Object{};
        }

        return Object::from_raw(result, m_desc->function_type->function_return->decayed);
    }

    [[nodiscard]] auto ref_impl(std::string_view operation, ObjectRef self, const Args& args) const -> Outcome<ObjectRef>
    {
        const Status checked = check_call(operation, self, args);
        if (checked.is_err())
        {
            return checked.unwrap_err();
        }

        if (!returns_reference())
        {
            return Error{ErrorKind::NotSupported,
                         std::format("{}: the callable does not return a reference; use invoke()",
                                     context(operation))};
        }

        if (m_desc->invoke_ref == nullptr)
        {
            return Error{ErrorKind::NotSupported, std::format("{}: the callable cannot be invoked", context(operation))};
        }

        const detail::ArgumentPointers pointers(args);
        void* const receiver = self.is_null() ? nullptr : const_cast<void*>(self.raw_data());
        const void* address = m_desc->invoke_ref(m_value.raw_data(), receiver, pointers.data());
        const detail::TypeDescriptor* referent = m_desc->function_type->function_return->referenced;

        return ObjectRef(address, m_desc->function_type->function_return->decayed, referent->is_const_qualified,
                         referent->is_volatile_qualified);
    }

    /// Argument count -> receiver -> left-to-right arguments (api_design §0.1).
    [[nodiscard]] auto check_call(std::string_view operation, const ObjectRef& self, const Args& args) const -> Status
    {
        if (!m_desc->is_member_function_pointer && !self.is_null())
        {
            return Error{ErrorKind::NotSupported,
                         std::format("{}: a free function pointer takes no receiver", context(operation))};
        }

        const std::size_t count = m_desc->function_type->function_parameter_count;
        if (args.size() != count)
        {
            return Error{ErrorKind::InvalidArgument,
                         std::format("{}: expected {} argument(s), got {}", context(operation), count, args.size())};
        }

        if (m_desc->is_member_function_pointer)
        {
            if (self.is_null())
            {
                return Error{ErrorKind::NullInstance,
                             std::format("{}: the receiver is null", context(operation))};
            }

            if (self.descriptor() != m_desc->declaring_class->decayed)
            {
                return detail::receiver_mismatch(context(operation), m_desc->declaring_class, self);
            }
        }

        for (std::size_t i = 0; i < count; ++i)
        {
            const Class* parameter = m_desc->function_type->function_parameters[i];
            const detail::TypeDescriptor* decay = m_desc->function_type->function_parameter_decays[i];
            if (!detail::accepts_argument(parameter, decay, args[i], true))
            {
                return detail::argument_mismatch(context(operation), i, parameter, decay, args[i], true);
            }
        }

        return None{};
    }

    /// "Callable::invoke(void (*)(int))": the operation plus the pointer type.
    [[nodiscard]] auto context(std::string_view operation) const -> std::string
    {
        return std::format("{}({})", operation, m_desc->type->qualified_name);
    }

    detail::CheckedDesc<detail::CallableDesc> m_desc;
    ObjectRef                                 m_value;
};

inline auto ObjectRef::type() const -> const Class&
{
    if (m_desc == nullptr)
    {
        panic("speculo::ObjectRef::type() called on a null reference");
    }

    return *m_desc->self;
}

inline auto Object::type() const -> const Class&
{
    if (m_desc == nullptr)
    {
        panic("speculo::Object::type() called on an empty object");
    }

    return *m_desc->self;
}

inline auto ObjectRef::clone() const -> Result<Object, Error>
{
    if (m_data == nullptr || m_desc == nullptr)
    {
        return Error{ErrorKind::NullInstance, "ObjectRef::clone(): the reference is null"};
    }

    if (m_desc->clone == nullptr)
    {
        return Error{ErrorKind::NotSupported,
                     std::format("ObjectRef::clone(): '{}' is not copy-constructible", m_desc->qualified_name)};
    }

    if (m_is_volatile)
    {
        return Error{ErrorKind::NotSupported,
                     std::format("ObjectRef::clone(): a volatile '{}' cannot be copied through a reflective handle",
                                 m_desc->qualified_name)};
    }

    return Object::from_raw(m_desc->clone(m_data), m_desc);
}

} // namespace speculo

namespace speculo::detail
{

/// The descriptor accessors and the descriptor-taking constructors are private
/// to the handle classes; the materialization layer and the registry reach them
/// through this struct, which is not exported.
struct Access
{
    [[nodiscard]] static auto descriptor(const Class& cls) noexcept -> const TypeDescriptor* { return cls.descriptor(); }
    [[nodiscard]] static auto descriptor(const ObjectRef& ref) noexcept -> const TypeDescriptor* { return ref.descriptor(); }

    template<typename Handle, typename Desc>
    [[nodiscard]] static constexpr auto make(const Desc* desc) noexcept -> Handle { return Handle(desc); }

    /// Adopts a thunk's heap result. The handles' from_raw / descriptor constructors stay private.
    [[nodiscard]] static auto adopt_object(void* data, const TypeDescriptor* desc) noexcept -> Object
    {
        return Object::from_raw(data, desc);
    }

    [[nodiscard]] static auto view_object(const void* data, const TypeDescriptor* desc, bool is_const,
                                          bool is_volatile) noexcept -> ObjectRef
    {
        return ObjectRef(data, desc, is_const, is_volatile);
    }
};

inline auto accepts_argument(const Class* parameter, const TypeDescriptor* decay,
                             const ObjectRef& arg, bool explicit_choice) -> bool
{
    if (arg.is_null() || Access::descriptor(arg) != decay)
    {
        return false;
    }

    const TypeDescriptor* exact = Access::descriptor(*parameter);
    if (exact->reference_kind == RefQualifier::None)
    {
        // By value: copies from the argument, which would read a volatile object
        // as a non-volatile one.
        return !arg.is_volatile();
    }

    if (!exact->referenced->is_const_qualified && arg.is_const())
    {
        return false; // cannot bind a const object to a non-const reference
    }

    if (!exact->referenced->is_volatile_qualified && arg.is_volatile())
    {
        return false; // cannot bind a volatile object to a non-volatile reference
    }

    return exact->reference_kind == RefQualifier::Lvalue || explicit_choice;
}

inline auto argument_mismatch(std::string_view callee, std::size_t index, const Class* parameter,
                              const TypeDescriptor* decay, const ObjectRef& arg,
                              bool explicit_choice) -> Error
{
    const TypeDescriptor* exact = Access::descriptor(*parameter);
    const std::string_view wanted = parameter->get_qualified_name();

    if (arg.is_null())
    {
        return Error{ErrorKind::TypeMismatch,
                     std::format("{}: argument {} is null, expected '{}'", callee, index, wanted)};
    }

    const TypeDescriptor* actual = Access::descriptor(arg);
    if (actual != decay)
    {
        return Error{ErrorKind::TypeMismatch,
                     std::format("{}: argument {} has type '{}', expected '{}' (types must match exactly)", callee, index,
                                 actual->qualified_name, wanted)};
    }

    const char* rule = "the argument cannot bind to the parameter";
    if (exact->reference_kind == RefQualifier::None)
    {
        rule = "a volatile object cannot be copied into a by-value parameter";
    }
    else if (!exact->referenced->is_const_qualified && arg.is_const())
    {
        rule = "a const object cannot bind to a non-const reference parameter";
    }
    else if (!exact->referenced->is_volatile_qualified && arg.is_volatile())
    {
        rule = "a volatile object cannot bind to a non-volatile reference parameter";
    }
    else if (!explicit_choice)
    {
        rule = "an rvalue-reference parameter is only selectable through a Method, Constructor, or Function handle";
    }

    return Error{ErrorKind::TypeMismatch,
                 std::format("{}: argument {} of type '{}' was rejected for parameter '{}': {}", callee, index,
                             actual->qualified_name, wanted, rule)};
}

inline auto describe_arguments(const Args& args) -> std::string
{
    std::string text = "(";
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        text += i == 0 ? "" : ", ";
        text += args[i].is_null() ? std::string_view("<null>") : Access::descriptor(args[i])->qualified_name;
    }

    text += ")";
    return text;
}

inline auto receiver_mismatch(std::string_view callee, const TypeDescriptor* declaring, const ObjectRef& obj) -> Error
{
    return Error{ErrorKind::TypeMismatch,
                 std::format("{}: the receiver has type '{}', not the declaring class '{}'", callee,
                             Access::descriptor(obj)->qualified_name, declaring->qualified_name)};
}

} // namespace speculo::detail

// ─────────────────────────────────────────────────────────────────────────────
// Reflection-driven materialization (the only place that touches <meta>)
// ─────────────────────────────────────────────────────────────────────────────

export namespace speculo::detail
{

/**
 * @brief Annotation attached to SPECULO_IMPL(_VIRTUAL)'s synthesized accessor
 *
 * The one entity of speculo::detail that is exported: the macros expand in user
 * translation units and name it. Everything else in speculo::detail is private to
 * the module.
 */
struct reflect_impl_tag
{
};

} // namespace speculo::detail

namespace speculo::detail
{

namespace meta = std::meta;

/**
 * @brief Auto-registration hook (defined in the `speculo:registry` partition)
 *
 * Called once per type from `class_of<T>()` to add the type to the global
 * name table. Kept out of `speculo:object` so the registry can depend on the
 * object API without creating a partition cycle. The descriptor table itself
 * stays read-only; this is the single one-time side effect (see design §8).
 */
void register_type(const Class& cls);

/// Public-only member access context (invoked at namespace scope).
consteval auto public_context() -> meta::access_context
{
    return meta::access_context::current();
}

/// Reflection of T (bundle is only instantiated with canonical types).
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

/// Whether C names an unqualified, non-array object type (value thunk carrier).
consteval auto is_unqualified_object(meta::info C) -> bool
{
    return meta::is_object_type(C) && !meta::is_const_type(C) && !meta::is_volatile_type(C)
        && !meta::is_array_type(C);
}

/**
 * @brief Whether C has a canonical, globally addressable qualified name
 *
 * GCC's `has_identifier` is false for fundamental / pointer / template
 * specialization types (which nonetheless have a usable display name such as
 * "int" or "std::vector<int>"), and true for anonymous-namespace and
 * function-local types (whose display names are not addressable). So instead of
 * `has_identifier` we reject internal linkage (anonymous namespace) and any
 * enclosing function (local types); everything else keeps its qualified name.
 */
consteval auto has_canonical_name(meta::info C) -> bool
{
    if (meta::has_internal_linkage(C))
    {
        return false;
    }

    if (!meta::has_parent(C))
    {
        return true;
    }

    const meta::info parent = meta::parent_of(C);
    if (meta::is_function(parent))
    {
        return false;
    }

    return has_canonical_name(parent);
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

template<meta::info C>
consteval auto public_field_count() -> std::size_t
{
    if constexpr (is_unqualified_class(C) && !is_std_type(C))
    {
        return meta::nonstatic_data_members_of(C, public_context()).size()
             + meta::static_data_members_of(C, public_context()).size();
    }
    else
    {
        return 0;
    }
}

template<meta::info C, std::size_t I>
consteval auto public_field_member() -> meta::info
{
    if constexpr (I < meta::nonstatic_data_members_of(C, public_context()).size())
    {
        return meta::nonstatic_data_members_of(C, public_context())[I];
    }
    else
    {
        return meta::static_data_members_of(C, public_context())[I - meta::nonstatic_data_members_of(C, public_context()).size()];
    }
}

/**
 * @brief Annotation attached to SPECULO_IMPL(_VIRTUAL)'s synthesized accessor
 *
 * The accessor (`get_class()`) is reflection plumbing: it exists to bridge a
 * concrete type to its Class. Reflecting it would drag in `const Class&` (and
 * hence `Class`'s whole signature closure, which reaches crab_cpp's move-only
 * variants), so annotated methods are excluded from the member tables. The
 * annotation is a plain `[[= detail::reflect_impl_tag{}]]` attribute.
 */
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

template<meta::info C>
consteval auto public_method_count() -> std::size_t
{
    if constexpr (is_unqualified_class(C) && !is_std_type(C))
    {
        std::size_t count = 0;
        for (auto member : meta::members_of(C, public_context()))
        {
            if (is_reflectable_method(member))
            {
                ++count;
            }
        }
        return count;
    }
    else
    {
        return 0;
    }
}

template<meta::info C, std::size_t I>
consteval auto public_method_member() -> meta::info
{
    std::size_t index = 0;
    for (auto member : meta::members_of(C, public_context()))
    {
        if (is_reflectable_method(member))
        {
            if (index == I)
            {
                return member;
            }
            ++index;
        }
    }
    return meta::info{};
}

template<meta::info C, std::size_t I>
consteval auto method_parameter_count() -> std::size_t
{
    return meta::parameters_of(public_method_member<C, I>()).size();
}

template<meta::info C, std::size_t... I>
consteval auto max_method_parameters(std::index_sequence<I...>) -> std::size_t
{
    std::size_t result = 0;
    ((result = method_parameter_count<C, I>() > result ? method_parameter_count<C, I>() : result), ...);
    return result;
}

template<meta::info C>
consteval auto max_parameters() -> std::size_t
{
    return max_method_parameters<C>(std::make_index_sequence<public_method_count<C>()>{});
}

// ── Declared view (all members, including private/protected; own class) ──

consteval auto unchecked_context() -> meta::access_context
{
    return meta::access_context::unchecked();
}

/**
 * @brief Whether C is a Java-style interface: an abstract class with no
 *        non-static data members (itself or in any base), whose bases are
 *        interfaces too, and whose non-static member functions are all pure
 *        virtual. Special members (the usual `virtual ~I() = default;`) and
 *        static functions are allowed.
 */
consteval auto is_interface_class(meta::info C) -> bool
{
    if (!meta::is_class_type(C) || !meta::is_abstract_type(C) || is_std_type(C))
    {
        return false;
    }

    if (!meta::nonstatic_data_members_of(C, unchecked_context()).empty())
    {
        return false;
    }

    for (meta::info base : meta::bases_of(C, unchecked_context()))
    {
        if (!is_interface_class(meta::type_of(base)))
        {
            return false;
        }
    }

    for (meta::info member : meta::members_of(C, unchecked_context()))
    {
        if (meta::is_function(member) && !meta::is_special_member_function(member)
            && !meta::is_static_member(member) && !meta::is_pure_virtual(member))
        {
            return false;
        }
    }

    return true;
}

template<meta::info C>
consteval auto declared_field_count() -> std::size_t
{
    if constexpr (is_unqualified_class(C) && !is_std_type(C))
    {
        return meta::nonstatic_data_members_of(C, unchecked_context()).size()
             + meta::static_data_members_of(C, unchecked_context()).size();
    }
    else
    {
        return 0;
    }
}

template<meta::info C, std::size_t I>
consteval auto declared_field_member() -> meta::info
{
    if constexpr (I < meta::nonstatic_data_members_of(C, unchecked_context()).size())
    {
        return meta::nonstatic_data_members_of(C, unchecked_context())[I];
    }
    else
    {
        return meta::static_data_members_of(C, unchecked_context())[I - meta::nonstatic_data_members_of(C, unchecked_context()).size()];
    }
}

template<meta::info C>
consteval auto declared_method_count() -> std::size_t
{
    if constexpr (is_unqualified_class(C) && !is_std_type(C))
    {
        std::size_t count = 0;
        for (auto member : meta::members_of(C, unchecked_context()))
        {
            if (is_reflectable_method(member))
            {
                ++count;
            }
        }
        return count;
    }
    else
    {
        return 0;
    }
}

template<meta::info C, std::size_t I>
consteval auto declared_method_member() -> meta::info
{
    std::size_t index = 0;
    for (auto member : meta::members_of(C, unchecked_context()))
    {
        if (is_reflectable_method(member))
        {
            if (index == I)
            {
                return member;
            }
            ++index;
        }
    }
    return meta::info{};
}

template<meta::info C, std::size_t I>
consteval auto declared_method_parameter_count() -> std::size_t
{
    return meta::parameters_of(declared_method_member<C, I>()).size();
}

template<meta::info C, std::size_t... I>
consteval auto declared_max_method_parameters(std::index_sequence<I...>) -> std::size_t
{
    std::size_t result = 0;
    ((result = declared_method_parameter_count<C, I>() > result ? declared_method_parameter_count<C, I>() : result), ...);
    return result;
}

template<meta::info C>
consteval auto declared_max_parameters() -> std::size_t
{
    return declared_max_method_parameters<C>(std::make_index_sequence<declared_method_count<C>()>{});
}

// ── Direct base classes ──

template<meta::info C>
consteval auto base_count() -> std::size_t
{
    if constexpr (is_unqualified_class(C) && !is_std_type(C))
    {
        return meta::bases_of(C, unchecked_context()).size();
    }
    else
    {
        return 0;
    }
}

template<meta::info C, std::size_t I>
consteval auto base_member() -> meta::info
{
    return meta::bases_of(C, unchecked_context())[I];
}

template<typename T>
auto release_thunk(void* data) -> void
{
    delete static_cast<T*>(data);
}

template<typename T>
auto destroy_thunk(void* data) -> void
{
    std::destroy_at(static_cast<T*>(data));
}

template<typename T>
auto clone_thunk(const void* data) -> void*
{
    return new T(*static_cast<const T*>(data));
}

template<meta::info M>
consteval auto make_field_get() -> FieldGetFn
{
    using F = std::remove_cvref_t<typename[: meta::type_of(M) :]>;
    // An array has no by-value copy; a non-copy-constructible member (e.g. a
    // move-only Object / variant) has no value getter either. Both keep a null
    // thunk, matching the "nullptr == capability missing" convention.
    if constexpr (std::is_array_v<F> || !std::is_copy_constructible_v<F>)
    {
        return nullptr;
    }
    else if constexpr (meta::is_static_member(M))
    {
        return +[](const void*) -> void* { return new F([:M:]); };
    }
    else
    {
        using C = typename[: meta::parent_of(M) :];
        return +[](const void* self) -> void* { return new F(static_cast<const C*>(self)->[:M:]); };
    }
}

template<meta::info M>
consteval auto make_field_set() -> FieldSetFn
{
    using FieldType = typename[: meta::type_of(M) :];
    using F = std::remove_cvref_t<FieldType>;
    constexpr bool writable = !std::is_const_v<std::remove_reference_t<FieldType>>
        && !std::is_array_v<F> && std::is_copy_assignable_v<F>;

    if constexpr (!writable)
    {
        return nullptr;
    }
    else if constexpr (meta::is_static_member(M))
    {
        return +[](void*, const void* value) -> bool
        {
            [:M:] = *static_cast<const F*>(value);
            return true;
        };
    }
    else
    {
        using C = typename[: meta::parent_of(M) :];
        return +[](void* self, const void* value) -> bool
        {
            static_cast<C*>(self)->[:M:] = *static_cast<const F*>(value);
            return true;
        };
    }
}

template<typename C, meta::info M>
auto field_ref_thunk(void* self) -> void*
{
    return const_cast<void*>(static_cast<const volatile void*>(std::addressof(static_cast<C*>(self)->[:M:])));
}

template<meta::info M>
consteval auto make_field_ref() -> FieldRefFn
{
    // A bitfield has no address. A reference member does: the expression below
    // names the referent, so the view is the referenced object.
    if constexpr (meta::is_bit_field(M))
    {
        return nullptr;
    }
    else if constexpr (meta::is_static_member(M))
    {
        return +[](void*) -> void* { return const_cast<void*>(static_cast<const volatile void*>(std::addressof([:M:]))); };
    }
    else
    {
        using C = typename[: meta::parent_of(M) :];
        return &field_ref_thunk<C, M>;
    }
}

/**
 * @brief Whether an annotation value can be `extract`ed on GCC 16.2
 *
 * A pointer, member pointer, reference, or function type cannot: `extract` goes
 * through `reflect_constant`, which rejects those and would make materializing
 * the annotated declaration ill-formed. Arrays are readable when their element
 * is; classes and unions are readable when every non-static data member and
 * (for classes) every base is.
 */
consteval auto annotation_value_is_readable(meta::info type) -> bool
{
    type = meta::remove_const(type);
    if (meta::is_pointer_type(type) || meta::is_member_pointer_type(type) || meta::is_reference_type(type)
        || meta::is_function_type(type))
    {
        return false;
    }

    if (meta::is_array_type(type))
    {
        if (meta::is_unbounded_array_type(type))
        {
            return false;
        }

        return annotation_value_is_readable(meta::remove_extent(type));
    }

    if ((meta::is_class_type(type) || meta::is_union_type(type)) && meta::is_complete_type(type))
    {
        for (meta::info member : meta::nonstatic_data_members_of(type, unchecked_context()))
        {
            if (!annotation_value_is_readable(meta::type_of(member)))
            {
                return false;
            }
        }

        if (meta::is_class_type(type))
        {
            for (meta::info base : meta::bases_of(type, unchecked_context()))
            {
                if (!annotation_value_is_readable(meta::type_of(base)))
                {
                    return false;
                }
            }
        }

        return true;
    }

    return meta::is_class_type(type) || meta::is_union_type(type) ? false : true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 9: templates, function types, nested types, aggregate slots,
// capabilities, and callable values (api_design §1–§5)
// ─────────────────────────────────────────────────────────────────────────────

/// Whether a non-type template argument can be `extract`ed (same rules as an
/// annotation value: pointers, members pointers and references cannot).
consteval auto constant_value_is_readable(meta::info type) -> bool
{
    return annotation_value_is_readable(type);
}

/// A non-type template argument's value, held in static storage.
template<meta::info A>
struct TemplateArgumentValue
{
    using type = std::remove_const_t<typename[: meta::type_of(A) :]>;
    static constexpr type value = meta::extract<type>(meta::constant_of(A));
};

template<meta::info A>
consteval auto template_argument_value() -> const void*
{
    if constexpr (constant_value_is_readable(meta::type_of(A)))
    {
        return std::addressof(TemplateArgumentValue<A>::value);
    }
    else
    {
        return nullptr;
    }
}

/**
 * @brief Whether C is an instance of a class template
 *
 * cv-qualified and reference types are *not* specializations even though the
 * compiler still reports their template arguments (S9 probe: `const
 * std::vector<int>` has two), so that they read as "the plain type is a
 * specialization" (api_design §1.2).
 */
consteval auto is_template_specialization_info(meta::info C) -> bool
{
    return meta::is_type(C) && meta::has_template_arguments(C) && !meta::is_const_type(C)
        && !meta::is_volatile_type(C) && !meta::is_reference_type(C);
}

template<meta::info C>
consteval auto template_argument_count() -> std::size_t
{
    if constexpr (is_template_specialization_info(C))
    {
        return meta::template_arguments_of(C).size();
    }
    else
    {
        return 0;
    }
}

template<meta::info C, std::size_t I>
consteval auto template_argument_info() -> meta::info
{
    return meta::template_arguments_of(C)[I];
}

/**
 * @brief What one argument is
 *
 * Class-type non-type arguments are `is_object` rather than `is_value` on GCC
 * 16.2 (S9 probe), so both count as Kind::Value. A parameter pack is flattened
 * into independent arguments and never reaches Kind::Pack.
 */
template<meta::info A>
consteval auto template_argument_kind() -> TemplateArgumentKind
{
    if constexpr (meta::is_type(A))
    {
        return TemplateArgumentKind::Type;
    }
    else if constexpr (meta::is_template(A))
    {
        return TemplateArgumentKind::Template;
    }
    else if constexpr (meta::is_value(A) || meta::is_object(A))
    {
        return TemplateArgumentKind::Value;
    }
    else
    {
        return TemplateArgumentKind::Pack;
    }
}

/// The spelling of an entity: its identifier when it has one, else the compiler's
/// display string (a template primary has no identifier usable as a type name).
consteval auto entity_name(meta::info entity) -> std::string
{
    return std::string(meta::has_identifier(entity) ? meta::identifier_of(entity) : meta::display_string_of(entity));
}

/**
 * @brief Qualified name of a template primary
 *
 * `display_string_of` on a class template is
 * `template<class _Tp, class _Alloc> class std::vector`, which is not a name, so
 * the qualified name is spliced from the identifier and the parent's display
 * string (S8.1).
 */
consteval auto template_qualified_name(meta::info entity) -> std::string
{
    if (!meta::has_parent(entity))
    {
        return entity_name(entity);
    }

    const meta::info parent = meta::parent_of(entity);
    if (meta::is_namespace(parent) && !meta::has_identifier(parent))
    {
        return entity_name(entity); // global namespace
    }

    std::string qualified(meta::display_string_of(parent));
    qualified += "::";
    qualified += entity_name(entity);
    return qualified;
}

/**
 * @brief Declaration-level descriptor of a template primary
 *
 * Keyed on the template *entity* (`meta::info`), because a primary template is
 * not a type and therefore has no place in the type-keyed bundles. Exactly one
 * instance per primary template, so `get_primary_template()` compares equal for
 * every specialization of it.
 */
template<meta::info K>
struct TemplateBundle
{
    TypeDescriptor desc;
    Class          cls;
};

template<meta::info K>
inline constexpr TemplateBundle<K> template_bundle = []() consteval -> TemplateBundle<K>
{
    TemplateBundle<K> bundle{};

    TypeDescriptor desc{};
    desc.name = std::define_static_string(meta::has_identifier(K) ? meta::identifier_of(K) : meta::display_string_of(K));
    desc.qualified_name = std::define_static_string(template_qualified_name(K));
    desc.normalized_name = std::define_static_string(normalize_type_name(template_qualified_name(K)));
    desc.has_canonical_name = false; // never registered: it is not a type
    desc.kind = TypeKind::Template;
    desc.modifiers = Modifier::PUBLIC;
    desc.self = std::addressof(template_bundle<K>.cls);
    desc.decayed = std::addressof(template_bundle<K>.desc);

    bundle.cls = Access::make<Class>(std::addressof(template_bundle<K>.desc));
    bundle.desc = desc;
    return bundle;
}();

// ── Function types (api_design §2.2) ──

template<meta::info C>
consteval auto function_parameter_count() -> std::size_t
{
    if constexpr (meta::is_function_type(C))
    {
        return meta::parameters_of(C).size();
    }
    else
    {
        return 0;
    }
}

/// A parameter of a function *type* is a type reflection; a function *declaration*
/// reports parameter entities instead, so both are handled (S8.4).
template<meta::info C, std::size_t J>
consteval auto function_parameter_type() -> meta::info
{
    constexpr meta::info P = meta::parameters_of(C)[J];
    if constexpr (meta::is_type(P))
    {
        return P;
    }
    else
    {
        return meta::type_of(P);
    }
}

// ── Nested types (api_design §4) ──

/**
 * @brief Whether one member is a nested type to report
 *
 * The alias test must come first: `is_class_type` penetrates an alias, so
 * `using Alias = Inner;` would otherwise be listed as a class of its own (S8.5).
 * Incomplete nested types and uninstantiated member templates are skipped,
 * because neither can be materialized.
 */
consteval auto is_nested_type_member(meta::info member) -> bool
{
    if (!meta::is_type(member) || meta::is_type_alias(member) || meta::is_template(member))
    {
        return false;
    }

    const meta::info target = meta::dealias(member);
    if (!meta::is_complete_type(target))
    {
        return false;
    }

    return meta::is_class_type(target) || meta::is_union_type(target) || meta::is_enum_type(target);
}

template<meta::info C>
consteval auto nested_type_count() -> std::size_t
{
    if constexpr (is_unqualified_class(C) && !is_std_type(C))
    {
        std::size_t count = 0;
        for (meta::info member : meta::members_of(C, unchecked_context()))
        {
            if (is_nested_type_member(member))
            {
                ++count;
            }
        }
        return count;
    }
    else
    {
        return 0;
    }
}

template<meta::info C, std::size_t I>
consteval auto nested_type_member() -> meta::info
{
    std::size_t index = 0;
    for (meta::info member : meta::members_of(C, unchecked_context()))
    {
        if (is_nested_type_member(member))
        {
            if (index == I)
            {
                return member;
            }
            ++index;
        }
    }
    return meta::info{};
}

/// The class a type is declared in (None at namespace scope / for non-nested types).
template<meta::info C>
consteval auto enclosing_class_info() -> meta::info
{
    if constexpr (meta::has_parent(C) && meta::is_type(meta::parent_of(C))
                  && (meta::is_class_type(meta::parent_of(C)) || meta::is_union_type(meta::parent_of(C))))
    {
        return meta::parent_of(C);
    }
    else
    {
        return meta::info{};
    }
}

// ── Capability bits (api_design §5.2) ──

template<meta::info C>
consteval auto capabilities_of() -> std::uint32_t
{
    std::uint32_t bits = 0;
    if constexpr ((meta::is_class_type(C) || meta::is_union_type(C)) && meta::is_complete_type(C))
    {
        if (meta::is_trivially_copyable_type(C))             { bits |= static_cast<std::uint32_t>(Capability::TriviallyCopyable); }
        if (meta::is_trivially_default_constructible_type(C)) { bits |= static_cast<std::uint32_t>(Capability::TriviallyDefaultConstructible); }
        if (meta::is_trivially_destructible_type(C))         { bits |= static_cast<std::uint32_t>(Capability::TriviallyDestructible); }
        if (meta::has_virtual_destructor(C))                 { bits |= static_cast<std::uint32_t>(Capability::VirtualDestructor); }
        if (meta::is_standard_layout_type(C))                { bits |= static_cast<std::uint32_t>(Capability::StandardLayout); }
        if (meta::has_unique_object_representations(C))      { bits |= static_cast<std::uint32_t>(Capability::UniqueObjectRepresentations); }
    }
    return bits;
}

// ── Aggregate initialization slots (api_design §3.2) ──

/**
 * @brief The initializer list of an aggregate: public bases, then data members
 *
 * A base slot holds the *base class itself* (aggregate initialization
 * copy-initializes the base subobject), not the base's members (S8.6).
 */
consteval auto aggregate_slot_types(meta::info C) -> std::vector<meta::info>
{
    std::vector<meta::info> slots;
    for (meta::info base : meta::bases_of(C, unchecked_context()))
    {
        if (meta::is_public(base) && !meta::is_virtual(base))
        {
            slots.push_back(meta::type_of(base));
        }
    }

    for (meta::info member : meta::nonstatic_data_members_of(C, unchecked_context()))
    {
        slots.push_back(meta::type_of(member));
    }

    return slots;
}

/// Whether one slot can be filled from a *value*: an lvalue of the slot type is
/// passed to the aggregate initializer, so a copy is needed (except for lvalue
/// references), and neither an array nor an rvalue-reference member can bind to
/// it (api_design §3.2).
consteval auto aggregate_slot_is_usable(meta::info type) -> bool
{
    if (meta::is_rvalue_reference_type(type) || meta::is_array_type(type))
    {
        return false;
    }

    if (meta::is_reference_type(type))
    {
        return true;
    }

    return meta::is_copy_constructible_type(type);
}

/**
 * @brief Whether C can be built through new_aggregate()
 *
 * Only an unqualified class type qualifies: a cv-qualified copy cannot be built,
 * and a union aggregate initializes a single member (there is no positional list
 * to fill). Beyond "is an aggregate" (which already excludes private data members
 * since C++20) the bases must all be public, and every slot must be fillable.
 */
consteval auto is_aggregate_initializable(meta::info C) -> bool
{
    if (!is_unqualified_class(C) || meta::is_union_type(C) || !meta::is_complete_type(C)
        || !meta::is_aggregate_type(C))
    {
        return false;
    }

    for (meta::info base : meta::bases_of(C, unchecked_context()))
    {
        if (!meta::is_public(base) || meta::is_virtual(base))
        {
            return false;
        }
    }

    for (meta::info slot : aggregate_slot_types(C))
    {
        if (!aggregate_slot_is_usable(slot))
        {
            return false;
        }
    }

    return true;
}

template<meta::info C>
consteval auto aggregate_slot_count() -> std::size_t
{
    if constexpr (is_aggregate_initializable(C))
    {
        return aggregate_slot_types(C).size();
    }
    else
    {
        return 0;
    }
}

template<meta::info C>
consteval auto aggregate_slot_tuple() -> meta::info
{
    return meta::substitute(^^std::tuple, aggregate_slot_types(C));
}

template<meta::info C, std::size_t J>
consteval auto aggregate_slot_type() -> meta::info
{
    return aggregate_slot_types(C)[J];
}

/// `Ty(int, double)`: the pseudo constructor's signature (the slot types).
template<meta::info C>
consteval auto aggregate_signature() -> std::string
{
    std::string signature(meta::display_string_of(C));
    signature += "(";
    const std::vector<meta::info> slots = aggregate_slot_types(C);
    for (std::size_t i = 0; i < slots.size(); ++i)
    {
        signature += i == 0 ? "" : ", ";
        signature += meta::display_string_of(slots[i]);
    }
    signature += ")";
    return signature;
}

/// Reads slot I of a call's arguments as an lvalue of the slot type.
template<std::size_t I, typename Slots>
auto aggregate_slot_ref(const void* const* args) -> std::remove_reference_t<std::tuple_element_t<I, Slots>>&
{
    using Slot = std::tuple_element_t<I, Slots>;
    using Value = std::remove_reference_t<Slot>;
    return *static_cast<Value*>(const_cast<void*>(args[I]));
}

template<meta::info C, std::size_t... I>
consteval auto make_aggregate_new_instance(std::index_sequence<I...>) -> CtorFn
{
    using Self = typename[: C :];
    using Slots = typename[: aggregate_slot_tuple<C>() :];

    return +[](const void* const* args) -> void*
    {
        return new Self(aggregate_slot_ref<I, Slots>(args)...);
    };
}

template<meta::info C, std::size_t... I>
consteval auto make_aggregate_construct_at(std::index_sequence<I...>) -> PlacementCtorFn
{
    using Self = typename[: C :];
    using Slots = typename[: aggregate_slot_tuple<C>() :];

    return +[](void* storage, const void* const* args) -> void
    {
        ::new (storage) Self(aggregate_slot_ref<I, Slots>(args)...);
    };
}

// ── Callable values (api_design §2.3) ──

/// One argument of a call, as the parameter category expects it.
template<typename P, std::size_t I>
decltype(auto) parameter_argument(const void* const* args)
{
    using Value = std::remove_reference_t<P>;
    Value& ref = *static_cast<Value*>(const_cast<void*>(args[I]));

    if constexpr (std::is_rvalue_reference_v<P>)
    {
        return static_cast<Value&&>(ref);
    }
    else
    {
        return static_cast<Value&>(ref);
    }
}

/// A by-value parameter needs a copy the argument type may not have.
template<typename P>
inline constexpr bool callable_parameter_loadable = std::is_reference_v<P> || std::is_constructible_v<P, P&>;

template<typename... P>
inline constexpr bool callable_parameters_loadable = (callable_parameter_loadable<P> && ...);

/**
 * @brief Calling a free function pointer of one exact type
 *
 * `R (*)(P...)` is the only pair of specializations needed (the `noexcept`
 * variant is separate because it is a distinct type).
 */
template<typename Pointer>
struct function_pointer_call;

#define SPECULO_FUNCTION_POINTER_CALL(Qual)                                                          \
    template<typename R, typename... P>                                                              \
    struct function_pointer_call<R (*)(P...) Qual>                                                   \
    {                                                                                                \
        using return_type = R;                                                                       \
        using function_type = R(P...) Qual;                                                          \
        using pointer_type = R (*)(P...) Qual;                                                       \
                                                                                                     \
        static constexpr bool can_box = !std::is_reference_v<R>                                      \
            ? callable_parameters_loadable<P...>                                                     \
            : (callable_parameters_loadable<P...> && std::is_constructible_v<std::remove_cvref_t<R>, R>); \
                                                                                                     \
        static auto box(const void* callable, void*, const void* const* args) -> void*               \
        {                                                                                            \
            const pointer_type function = *static_cast<const pointer_type*>(callable);                \
            return box_with(function, args, std::index_sequence_for<P...>{});                        \
        }                                                                                            \
                                                                                                     \
        static auto ref(const void* callable, void*, const void* const* args) -> const void*         \
        {                                                                                            \
            const pointer_type function = *static_cast<const pointer_type*>(callable);                \
            return ref_with(function, args, std::index_sequence_for<P...>{});                        \
        }                                                                                            \
                                                                                                     \
    private:                                                                                         \
        template<std::size_t... I>                                                                   \
        static auto box_with(pointer_type function, const void* const* args,                          \
                             std::index_sequence<I...>) -> void*                                     \
        {                                                                                            \
            if constexpr (std::is_void_v<R>)                                                         \
            {                                                                                        \
                function(parameter_argument<P, I>(args)...);                                         \
                return nullptr;                                                                      \
            }                                                                                        \
            else                                                                                     \
            {                                                                                        \
                return new std::remove_cvref_t<R>(function(parameter_argument<P, I>(args)...));      \
            }                                                                                        \
        }                                                                                            \
                                                                                                     \
        template<std::size_t... I>                                                                   \
        static auto ref_with(pointer_type function, const void* const* args,                         \
                             std::index_sequence<I...>) -> const void*                               \
        {                                                                                            \
            R result = function(parameter_argument<P, I>(args)...);                                  \
            return const_cast<const void*>(static_cast<const volatile void*>(std::addressof(result))); \
        }                                                                                            \
    };

SPECULO_FUNCTION_POINTER_CALL()
SPECULO_FUNCTION_POINTER_CALL(noexcept)

#undef SPECULO_FUNCTION_POINTER_CALL

/// A C-variadic function pointer is reflected (its fixed parameters are listed)
/// but cannot be called from a fixed argument array -- same rule as Method.
template<typename R, typename... P>
struct function_pointer_call<R (*)(P..., ...)>
{
    using return_type = R;
    using function_type = R(P..., ...);

    static constexpr bool can_box = false;
};

/**
 * @brief Calling a member function pointer of one exact qualification
 *
 * A member-function-pointer *type* cannot be decomposed with `std::meta` (S8.4:
 * `return_type_of` / `parameters_of` / `parent_of` all throw), so the parameter
 * list can only be recovered by deduction -- hence one specialization per
 * cv / ref / noexcept combination, like `member_function_pointer` above.
 */
template<typename Pointer>
struct member_pointer_call;

#define SPECULO_MEMBER_POINTER_CALL(Qual)                                                            \
    template<typename R, typename C, typename... P>                                                  \
    struct member_pointer_call<R (C::*)(P...) Qual>                                                  \
    {                                                                                                \
        using return_type = R;                                                                       \
        using caller_type = C;                                                                       \
        using function_type = R(P...) Qual;                                                          \
        using pointer_type = R (C::*)(P...) Qual;                                                    \
                                                                                                     \
        static constexpr bool can_box = !std::is_reference_v<R>                                      \
            ? callable_parameters_loadable<P...>                                                     \
            : (callable_parameters_loadable<P...> && std::is_constructible_v<std::remove_cvref_t<R>, R>); \
                                                                                                     \
        static auto box(const void* callable, void* self, const void* const* args) -> void*          \
        {                                                                                            \
            const pointer_type function = *static_cast<const pointer_type*>(callable);                \
            return box_with(function, static_cast<C*>(self), args, std::index_sequence_for<P...>{});  \
        }                                                                                            \
                                                                                                     \
        static auto ref(const void* callable, void* self, const void* const* args) -> const void*     \
        {                                                                                            \
            const pointer_type function = *static_cast<const pointer_type*>(callable);                \
            return ref_with(function, static_cast<C*>(self), args, std::index_sequence_for<P...>{});  \
        }                                                                                            \
                                                                                                     \
    private:                                                                                         \
        template<std::size_t... I>                                                                   \
        static auto box_with(pointer_type function, C* object, const void* const* args,              \
                             std::index_sequence<I...>) -> void*                                     \
        {                                                                                            \
            if constexpr (std::is_void_v<R>)                                                         \
            {                                                                                        \
                (object->*function)(parameter_argument<P, I>(args)...);                               \
                return nullptr;                                                                      \
            }                                                                                        \
            else                                                                                     \
            {                                                                                        \
                return new std::remove_cvref_t<R>((object->*function)(parameter_argument<P, I>(args)...)); \
            }                                                                                        \
        }                                                                                            \
                                                                                                     \
        template<std::size_t... I>                                                                   \
        static auto ref_with(pointer_type function, C* object, const void* const* args,              \
                             std::index_sequence<I...>) -> const void*                               \
        {                                                                                            \
            R result = (object->*function)(parameter_argument<P, I>(args)...);                        \
            return const_cast<const void*>(static_cast<const volatile void*>(std::addressof(result))); \
        }                                                                                            \
    };

SPECULO_MEMBER_POINTER_CALL()
SPECULO_MEMBER_POINTER_CALL(const)
SPECULO_MEMBER_POINTER_CALL(volatile)
SPECULO_MEMBER_POINTER_CALL(const volatile)
SPECULO_MEMBER_POINTER_CALL(&)
SPECULO_MEMBER_POINTER_CALL(const &)
SPECULO_MEMBER_POINTER_CALL(volatile &)
SPECULO_MEMBER_POINTER_CALL(const volatile &)
SPECULO_MEMBER_POINTER_CALL(&&)
SPECULO_MEMBER_POINTER_CALL(const &&)
SPECULO_MEMBER_POINTER_CALL(volatile &&)
SPECULO_MEMBER_POINTER_CALL(const volatile &&)
SPECULO_MEMBER_POINTER_CALL(noexcept)
SPECULO_MEMBER_POINTER_CALL(const noexcept)
SPECULO_MEMBER_POINTER_CALL(volatile noexcept)
SPECULO_MEMBER_POINTER_CALL(const volatile noexcept)
SPECULO_MEMBER_POINTER_CALL(& noexcept)
SPECULO_MEMBER_POINTER_CALL(const & noexcept)
SPECULO_MEMBER_POINTER_CALL(volatile & noexcept)
SPECULO_MEMBER_POINTER_CALL(const volatile & noexcept)
SPECULO_MEMBER_POINTER_CALL(&& noexcept)
SPECULO_MEMBER_POINTER_CALL(const && noexcept)
SPECULO_MEMBER_POINTER_CALL(volatile && noexcept)
SPECULO_MEMBER_POINTER_CALL(const volatile && noexcept)

#undef SPECULO_MEMBER_POINTER_CALL

/// A base offset that folds to a constant (api_design §6: no run-time work).
template<std::size_t Offset>
auto constant_offset() -> std::size_t
{
    return Offset;
}

/// Annotations of one reflected entity (`annotations_of(Entity)`).
template<meta::info Entity>
struct DirectAnnotations
{
    static consteval auto count() -> std::size_t { return meta::annotations_of(Entity).size(); }

    template<std::size_t I>
    static consteval auto get() -> meta::info { return meta::annotations_of(Entity)[I]; }
};

/// Annotations of parameter `Index` of a function.
template<meta::info Function, std::size_t Index>
struct ParameterAnnotations
{
    static consteval auto count() -> std::size_t
    {
        return meta::annotations_of(meta::parameters_of(Function)[Index]).size();
    }

    template<std::size_t I>
    static consteval auto get() -> meta::info
    {
        return meta::annotations_of(meta::parameters_of(Function)[Index])[I];
    }
};

template<typename Source, std::size_t I>
struct HeldAnnotation
{
    using type = std::remove_const_t<typename[: meta::type_of(Source::template get<I>()) :]>;
    static constexpr type value = meta::extract<type>(Source::template get<I>());
};

template<typename Source, std::size_t I>
consteval auto held_annotation_ptr() -> const void*
{
    constexpr meta::info type = meta::type_of(Source::template get<I>());
    if constexpr (annotation_value_is_readable(type))
    {
        return std::addressof(HeldAnnotation<Source, I>::value);
    }
    else
    {
        return nullptr;
    }
}

template<typename T>
consteval auto annotation_type_descriptor() -> const TypeDescriptor*;

template<typename Source, std::size_t I>
consteval auto make_annotation_desc() -> AnnotationDesc
{
    constexpr meta::info type = meta::remove_const(meta::type_of(Source::template get<I>()));
    using T = typename[: type :];

    AnnotationDesc desc{};
    desc.type = annotation_type_descriptor<T>();
    desc.value = held_annotation_ptr<Source, I>();
    return desc;
}

template<typename Source>
struct AnnotationSet
{
    static constexpr std::size_t count = Source::count();

    std::array<AnnotationDesc, count> descs{};
    std::array<Annotation, count> handles{};
};

template<typename Source>
inline constexpr AnnotationSet<Source> annotation_set = []() consteval -> AnnotationSet<Source>
{
    AnnotationSet<Source> set{};
    [&set]<std::size_t... I>(std::index_sequence<I...>)
    {
        (([&set]()
        {
            set.descs[I] = make_annotation_desc<Source, I>();
            set.handles[I] = Access::make<Annotation>(std::addressof(annotation_set<Source>.descs[I]));
        }()), ...);
    }(std::make_index_sequence<AnnotationSet<Source>::count>{});
    return set;
}();

template<meta::info Function>
struct ParameterAnnotationIndex
{
    static constexpr std::size_t count = meta::parameters_of(Function).size();

    std::array<const Annotation*, count> data{};
    std::array<std::size_t, count> counts{};
};

template<meta::info Function>
inline constexpr ParameterAnnotationIndex<Function> parameter_annotation_index = []() consteval -> ParameterAnnotationIndex<Function>
{
    ParameterAnnotationIndex<Function> index{};
    [&index]<std::size_t... J>(std::index_sequence<J...>)
    {
        ((index.data[J] = annotation_set<ParameterAnnotations<Function, J>>.handles.data()), ...);
        ((index.counts[J] = annotation_set<ParameterAnnotations<Function, J>>.count), ...);
    }(std::make_index_sequence<ParameterAnnotationIndex<Function>::count>{});
    return index;
}();

template<meta::info Entity>
consteval auto own_annotations() -> const AnnotationSet<DirectAnnotations<Entity>>&
{
    return annotation_set<DirectAnnotations<Entity>>;
}

template<meta::info M>
consteval auto make_field_desc(const TypeDescriptor* type, const TypeDescriptor* declaring) -> FieldDesc
{
    using FieldType = typename[: meta::type_of(M) :];
    using F = std::remove_cvref_t<FieldType>;

    FieldDesc desc{};
    desc.name = std::define_static_string(meta::has_identifier(M) ? meta::identifier_of(M) : meta::display_string_of(M));
    desc.type = type;
    desc.declaring_class = declaring;
    desc.size = sizeof(F);
    desc.alignment = alignof(F);

    std::uint32_t mods = 0;
    if (meta::is_public(M))         { mods |= Modifier::PUBLIC; }
    else if (meta::is_protected(M)) { mods |= Modifier::PROTECTED; }
    else if (meta::is_private(M))   { mods |= Modifier::PRIVATE; }
    if (meta::is_static_member(M))  { mods |= Modifier::STATIC; }
    if (meta::is_mutable_member(M)) { mods |= Modifier::MUTABLE; }
    if (meta::is_bit_field(M))      { mods |= Modifier::BITFIELD; }
    if (meta::is_reference_type(meta::type_of(M))) { mods |= Modifier::REFERENCE; }
    if (meta::is_const_type(meta::type_of(M)))     { mods |= Modifier::CONST; }
    if (meta::is_volatile_type(meta::type_of(M)))  { mods |= Modifier::VOLATILE; }
    desc.modifiers = mods;

    desc.is_bitfield = meta::is_bit_field(M);
    desc.is_static = meta::is_static_member(M);
    desc.is_mutable = meta::is_mutable_member(M);
    desc.is_reference = meta::is_reference_type(meta::type_of(M));

    if (!desc.is_static)
    {
        desc.offset = static_cast<std::size_t>(meta::offset_of(M).bytes);
        desc.bit_offset = static_cast<std::size_t>(meta::offset_of(M).total_bits());
    }
    if (desc.is_bitfield)
    {
        desc.bit_width = meta::bit_size_of(M);
    }

    desc.get = make_field_get<M>();
    desc.set = make_field_set<M>();
    desc.get_ref = make_field_ref<M>();
    desc.annotations = own_annotations<M>().handles.data();
    desc.annotation_count = own_annotations<M>().count;

    return desc;
}

template<meta::info M, std::size_t I>
decltype(auto) load_argument(const void* const* args)
{
    using PT = typename[: meta::type_of(meta::parameters_of(M)[I]) :];
    using P = std::remove_reference_t<PT>;
    P& ref = *static_cast<P*>(const_cast<void*>(args[I]));
    if constexpr (meta::is_rvalue_reference_type(meta::type_of(meta::parameters_of(M)[I])))
    {
        return static_cast<P&&>(ref);
    }
    else
    {
        return static_cast<P&>(ref);
    }
}

// Rebuilds the exact member-function-pointer type from the reflected function
// type (which carries cv / ref / noexcept). `extract` needs the exact type, and
// unlike a splice call it also works for private/protected member functions.
template<typename Class, typename Fn>
struct member_function_pointer;
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...)> { using type = R (Class::*)(P...); };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) const> { using type = R (Class::*)(P...) const; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) volatile> { using type = R (Class::*)(P...) volatile; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) const volatile> { using type = R (Class::*)(P...) const volatile; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) &> { using type = R (Class::*)(P...) &; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) const &> { using type = R (Class::*)(P...) const &; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) volatile &> { using type = R (Class::*)(P...) volatile &; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) const volatile &> { using type = R (Class::*)(P...) const volatile &; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) &&> { using type = R (Class::*)(P...) &&; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) const &&> { using type = R (Class::*)(P...) const &&; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) volatile &&> { using type = R (Class::*)(P...) volatile &&; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) const volatile &&> { using type = R (Class::*)(P...) const volatile &&; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) noexcept> { using type = R (Class::*)(P...) noexcept; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) const noexcept> { using type = R (Class::*)(P...) const noexcept; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) volatile noexcept> { using type = R (Class::*)(P...) volatile noexcept; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) const volatile noexcept> { using type = R (Class::*)(P...) const volatile noexcept; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) & noexcept> { using type = R (Class::*)(P...) & noexcept; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) const & noexcept> { using type = R (Class::*)(P...) const & noexcept; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) volatile & noexcept> { using type = R (Class::*)(P...) volatile & noexcept; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) const volatile & noexcept> { using type = R (Class::*)(P...) const volatile & noexcept; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) && noexcept> { using type = R (Class::*)(P...) && noexcept; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) const && noexcept> { using type = R (Class::*)(P...) const && noexcept; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) volatile && noexcept> { using type = R (Class::*)(P...) volatile && noexcept; };
template<typename Class, typename R, typename... P> struct member_function_pointer<Class, R(P...) const volatile && noexcept> { using type = R (Class::*)(P...) const volatile && noexcept; };

template<typename Fn>
struct static_function_pointer;
template<typename R, typename... P> struct static_function_pointer<R(P...)> { using type = R (*)(P...); };
template<typename R, typename... P> struct static_function_pointer<R(P...) noexcept> { using type = R (*)(P...) noexcept; };
// The thunks pass every non-rvalue-reference argument as an lvalue, so a
// by-value parameter is copied from the caller's object (never silently moved).
template<meta::info M, std::size_t I>
consteval auto parameter_is_loadable() -> bool
{
    using PT = typename[: meta::type_of(meta::parameters_of(M)[I]) :];
    if constexpr (std::is_reference_v<PT>)
    {
        return true;
    }
    else
    {
        return std::is_constructible_v<PT, PT&>;
    }
}

template<meta::info M, std::size_t... I>
consteval auto parameters_are_loadable(std::index_sequence<I...>) -> bool
{
    return (parameter_is_loadable<M, I>() && ...);
}

// Whether M returns an lvalue / rvalue reference (invoke_ref() applies).
template<meta::info M>
consteval auto returns_reference() -> bool
{
    return std::is_reference_v<typename[: meta::return_type_of(M) :]>;
}

// The address of a referent, without cv (the descriptor carries the qualifiers).
template<typename T>
auto referent_address(T& referent) noexcept -> const void*
{
    return const_cast<const void*>(static_cast<const volatile void*>(std::addressof(referent)));
}

// A returned reference is boxed by copying the referent into a new Object.
template<meta::info M>
consteval auto result_is_boxable() -> bool
{
    using R = typename[: meta::return_type_of(M) :];
    if constexpr (std::is_reference_v<R>)
    {
        return std::is_constructible_v<std::remove_cvref_t<R>, R>;
    }
    else
    {
        return true; // void, or a prvalue (constructed in place / moved)
    }
}

template<meta::info M, std::size_t... I>
consteval auto make_invoker(std::index_sequence<I...>) -> InvokeFn
{
    using F = typename[: meta::type_of(M) :];
    using R = typename[: meta::return_type_of(M) :];

    if constexpr (meta::is_static_member(M))
    {
        using FP = typename static_function_pointer<F>::type;
        return +[](void*, const void* const* args) -> void*
        {
            constexpr FP function = meta::extract<FP>(M);
            if constexpr (std::is_void_v<R>)
            {
                function(load_argument<M, I>(args)...);
                return nullptr;
            }
            else
            {
                return new std::remove_cvref_t<R>(function(load_argument<M, I>(args)...));
            }
        };
    }
    else
    {
        using C = typename[: meta::parent_of(M) :];
        using MP = typename member_function_pointer<C, F>::type;
        return +[](void* self, const void* const* args) -> void*
        {
            constexpr MP function = meta::extract<MP>(M);
            auto* receiver = static_cast<C*>(self);
            if constexpr (std::is_void_v<R>)
            {
                (receiver->*function)(load_argument<M, I>(args)...);
                return nullptr;
            }
            else
            {
                return new std::remove_cvref_t<R>((receiver->*function)(load_argument<M, I>(args)...));
            }
        };
    }
}

// Reference-returning counterpart of make_invoker: returns the address of the
// referent instead of boxing a copy, so non-copyable referents work too.
template<meta::info M, std::size_t... I>
consteval auto make_ref_invoker(std::index_sequence<I...>) -> InvokeRefFn
{
    using F = typename[: meta::type_of(M) :];
    using R = typename[: meta::return_type_of(M) :];

    if constexpr (meta::is_static_member(M))
    {
        using FP = typename static_function_pointer<F>::type;
        return +[](void*, const void* const* args) -> const void*
        {
            constexpr FP function = meta::extract<FP>(M);
            R result = function(load_argument<M, I>(args)...);
            return referent_address(result);
        };
    }
    else
    {
        using C = typename[: meta::parent_of(M) :];
        using MP = typename member_function_pointer<C, F>::type;
        return +[](void* self, const void* const* args) -> const void*
        {
            constexpr MP function = meta::extract<MP>(M);
            auto* receiver = static_cast<C*>(self);
            R result = (receiver->*function)(load_argument<M, I>(args)...);
            return referent_address(result);
        };
    }
}


template<meta::info M>
consteval auto make_method_desc(const TypeDescriptor* declaring, const TypeDescriptor* return_type,
                                const Class* const* parameters, const TypeDescriptor* const* parameter_decays,
                                std::size_t parameter_count) -> MethodDesc
{
    MethodDesc desc{};
    desc.name = std::define_static_string(meta::has_identifier(M) ? meta::identifier_of(M) : meta::display_string_of(M));
    desc.signature = std::define_static_string(meta::display_string_of(M));
    desc.declaring_class = declaring;
    desc.return_type = return_type;
    desc.parameter_types = parameters;
    desc.parameter_decays = parameter_decays;
    desc.parameter_count = parameter_count;

    std::uint32_t mods = 0;
    if (meta::is_public(M))         { mods |= Modifier::PUBLIC; }
    else if (meta::is_protected(M)) { mods |= Modifier::PROTECTED; }
    else if (meta::is_private(M))   { mods |= Modifier::PRIVATE; }
    if (meta::is_const(M))          { mods |= Modifier::CONST; }
    if (meta::is_volatile(M))       { mods |= Modifier::VOLATILE; }
    if (meta::is_static_member(M))  { mods |= Modifier::STATIC; }
    if (meta::is_noexcept(M))       { mods |= Modifier::NOEXCEPT; }
    if (meta::is_deleted(M))        { mods |= Modifier::DELETED; }
    if (meta::is_defaulted(M))      { mods |= Modifier::DEFAULTED; }
    if (meta::is_virtual(M))
    {
        mods |= Modifier::VIRTUAL;
        if (meta::is_pure_virtual(M)) { mods |= Modifier::ABSTRACT; }
        if (meta::is_override(M))     { mods |= Modifier::OVERRIDE; }
        if (meta::is_final(M))        { mods |= Modifier::FINAL; }
    }
    desc.modifiers = mods;

    if (meta::is_lvalue_reference_qualified(M))       { desc.ref_qualifier = RefQualifier::Lvalue; }
    else if (meta::is_rvalue_reference_qualified(M))  { desc.ref_qualifier = RefQualifier::Rvalue; }

    desc.is_destructor = meta::is_destructor(M);
    desc.is_operator = !desc.is_destructor && meta::is_operator_function(M);

    if constexpr (meta::is_destructor(M))
    {
        // A destructor has no invokable member-function pointer via Method::invoke();
        // object destruction goes through Object's release() / Constructor::destroy_at().
        desc.invoke = nullptr;
    }
    else if constexpr (meta::is_deleted(M))
    {
        // Listed, but there is nothing to call (and nothing to `extract`).
        desc.invoke = nullptr;
    }
    else if constexpr (meta::is_vararg_function(M))
    {
        // A C variadic function (`...`) has no member-function-pointer
        // specialization and cannot be driven from a fixed argument array. The
        // fixed parameters are listed; the `...` only shows up in get_signature().
        desc.invoke = nullptr;
    }
    else if constexpr (meta::is_rvalue_reference_qualified(M))
    {
        // `&&`-qualified member functions require an rvalue receiver; ObjectRef
        // carries no value category, so they are reflected but not invokable.
        desc.invoke = nullptr;
    }
    else
    {
        constexpr auto indices = std::make_index_sequence<meta::parameters_of(M).size()>{};

        // A by-value move-only parameter would need a copy the type does not have.
        if constexpr (parameters_are_loadable<M>(indices))
        {
            // A reference return of a move-only type cannot be boxed by copy, but
            // invoke_ref() can still hand out the referent.
            if constexpr (result_is_boxable<M>())
            {
                desc.invoke = make_invoker<M>(indices);
            }

            if constexpr (returns_reference<M>())
            {
                desc.invoke_ref = make_ref_invoker<M>(indices);
            }
        }
    }

    // GCC 16.2 instantiates a function whose reflection is passed to
    // annotations_of. A deleted function has no body that can be instantiated,
    // so its annotations (and its parameters') are left empty.
    if constexpr (!meta::is_deleted(M))
    {
        desc.annotations = own_annotations<M>().handles.data();
        desc.annotation_count = own_annotations<M>().count;
        if constexpr (meta::parameters_of(M).size() > 0)
        {
            desc.parameter_annotations = parameter_annotation_index<M>.data.data();
            desc.parameter_annotation_counts = parameter_annotation_index<M>.counts.data();
        }
    }

    return desc;
}

// ── Constructor materialization ──

template<meta::info C>
consteval auto public_ctor_count() -> std::size_t
{
    if constexpr (is_unqualified_class(C) && !is_std_type(C))
    {
        std::size_t count = 0;
        for (auto member : meta::members_of(C, public_context()))
        {
            if (meta::is_function(member) && meta::is_constructor(member))
            {
                ++count;
            }
        }
        return count;
    }
    else
    {
        return 0;
    }
}

template<meta::info C, std::size_t I>
consteval auto public_ctor_member() -> meta::info
{
    std::size_t index = 0;
    for (auto member : meta::members_of(C, public_context()))
    {
        if (meta::is_function(member) && meta::is_constructor(member))
        {
            if (index == I)
            {
                return member;
            }
            ++index;
        }
    }
    return meta::info{};
}

template<meta::info C, std::size_t I>
consteval auto public_ctor_parameter_count() -> std::size_t
{
    return meta::parameters_of(public_ctor_member<C, I>()).size();
}

template<meta::info C, std::size_t... I>
consteval auto max_public_ctor_parameters(std::index_sequence<I...>) -> std::size_t
{
    std::size_t result = 0;
    ((result = public_ctor_parameter_count<C, I>() > result ? public_ctor_parameter_count<C, I>() : result), ...);
    return result;
}

template<meta::info C>
consteval auto max_ctor_parameters() -> std::size_t
{
    return max_public_ctor_parameters<C>(std::make_index_sequence<public_ctor_count<C>()>{});
}

template<meta::info C>
consteval auto declared_ctor_count() -> std::size_t
{
    if constexpr (is_unqualified_class(C) && !is_std_type(C))
    {
        std::size_t count = 0;
        for (auto member : meta::members_of(C, unchecked_context()))
        {
            if (meta::is_function(member) && meta::is_constructor(member))
            {
                ++count;
            }
        }
        return count;
    }
    else
    {
        return 0;
    }
}

template<meta::info C, std::size_t I>
consteval auto declared_ctor_member() -> meta::info
{
    std::size_t index = 0;
    for (auto member : meta::members_of(C, unchecked_context()))
    {
        if (meta::is_function(member) && meta::is_constructor(member))
        {
            if (index == I)
            {
                return member;
            }
            ++index;
        }
    }
    return meta::info{};
}

template<meta::info C, std::size_t I>
consteval auto declared_ctor_parameter_count() -> std::size_t
{
    return meta::parameters_of(declared_ctor_member<C, I>()).size();
}

template<meta::info C, std::size_t... I>
consteval auto declared_max_ctor_parameters_seq(std::index_sequence<I...>) -> std::size_t
{
    std::size_t result = 0;
    ((result = declared_ctor_parameter_count<C, I>() > result ? declared_ctor_parameter_count<C, I>() : result), ...);
    return result;
}

template<meta::info C>
consteval auto declared_max_ctor_parameters() -> std::size_t
{
    return declared_max_ctor_parameters_seq<C>(std::make_index_sequence<declared_ctor_count<C>()>{});
}

template<meta::info C, meta::info M, std::size_t... I>
consteval auto make_ctor_new_instance(std::index_sequence<I...>) -> CtorFn
{
    using Self = typename[: C :];
    return +[](const void* const* args) -> void*
    {
        return new Self(load_argument<M, I>(args)...);
    };
}

template<meta::info C, meta::info M, std::size_t... I>
consteval auto make_ctor_construct_at(std::index_sequence<I...>) -> PlacementCtorFn
{
    using Self = typename[: C :];
    return +[](void* storage, const void* const* args) -> void
    {
        ::new (storage) Self(load_argument<M, I>(args)...);
    };
}

template<typename Self>
auto canonical_destroy(void* storage) -> void
{
    std::destroy_at(static_cast<Self*>(storage));
}

template<meta::info C, meta::info M>
consteval auto make_ctor_desc(const TypeDescriptor* declaring,
                              const Class* const* parameters, const TypeDescriptor* const* parameter_decays,
                              std::size_t parameter_count) -> CtorDesc
{
    using Self = typename[: C :];
    CtorDesc desc{};
    desc.signature = std::define_static_string(meta::display_string_of(M));
    desc.declaring_class = declaring;
    desc.parameter_types = parameters;
    desc.parameter_decays = parameter_decays;
    desc.parameter_count = parameter_count;

    std::uint32_t mods = 0;
    if (meta::is_public(M))         { mods |= Modifier::PUBLIC; }
    else if (meta::is_protected(M)) { mods |= Modifier::PROTECTED; }
    else if (meta::is_private(M))   { mods |= Modifier::PRIVATE; }
    if (meta::is_explicit(M))       { mods |= Modifier::EXPLICIT; }
    if (meta::is_deleted(M))        { mods |= Modifier::DELETED; }
    if (meta::is_defaulted(M))      { mods |= Modifier::DEFAULTED; }
    if (meta::is_noexcept(M))       { mods |= Modifier::NOEXCEPT; }
    desc.modifiers = mods;

    desc.is_copy = meta::is_copy_constructor(M);
    desc.is_move = meta::is_move_constructor(M);

    // Non-public / deleted / abstract constructors have no thunk (capability
    // missing). Private/protected construction would need class-internal access
    // that namespace-scope thunks do not have.
    constexpr bool invokable = meta::is_public(M)
                            && !meta::is_deleted(M)
                            && !meta::is_abstract_type(C)
                            && parameters_are_loadable<M>(std::make_index_sequence<meta::parameters_of(M).size()>{});
    if constexpr (invokable)
    {
        constexpr auto args = std::make_index_sequence<meta::parameters_of(M).size()>{};
        desc.new_instance = make_ctor_new_instance<C, M>(args);
        desc.construct_at = make_ctor_construct_at<C, M>(args);
    }

    if constexpr (meta::is_destructible_type(C))
    {
        desc.destroy_at = &canonical_destroy<Self>;
    }

    if constexpr (!meta::is_deleted(M))
    {
        desc.annotations = own_annotations<M>().handles.data();
        desc.annotation_count = own_annotations<M>().count;
        if constexpr (meta::parameters_of(M).size() > 0)
        {
            desc.parameter_annotations = parameter_annotation_index<M>.data.data();
            desc.parameter_annotation_counts = parameter_annotation_index<M>.counts.data();
        }
    }

    return desc;
}

template<meta::info C>
consteval auto classify() -> TypeKind
{
    if constexpr (meta::is_void_type(C))                { return TypeKind::Void; }
    else if constexpr (meta::is_fundamental_type(C))    { return TypeKind::Fundamental; }
    else if constexpr (meta::is_pointer_type(C))        { return TypeKind::Pointer; }
    else if constexpr (meta::is_member_pointer_type(C)) { return TypeKind::MemberPointer; }
    else if constexpr (meta::is_reference_type(C))      { return TypeKind::Reference; }
    else if constexpr (meta::is_array_type(C))          { return TypeKind::Array; }
    else if constexpr (meta::is_enum_type(C))           { return TypeKind::Enum; }
    else if constexpr (meta::is_union_type(C))          { return TypeKind::Union; }
    else if constexpr (meta::is_function_type(C))       { return TypeKind::Function; }
    else                                                { return TypeKind::Class; }
}

// ── Phase 5: pointers / cv / references / enums / arrays ──

/// Extracts the class of a member pointer type `M C::*`.
template<typename T>
struct member_pointer_class;
template<typename M, typename C>
struct member_pointer_class<M C::*> { using type = C; };

template<typename E>
auto enum_read_thunk(const void* data) -> std::uint64_t
{
    using Underlying = std::underlying_type_t<E>;
    return static_cast<std::uint64_t>(static_cast<Underlying>(*static_cast<const E*>(data)));
}

template<typename E>
auto enum_make_thunk(std::uint64_t bits) -> void*
{
    using Underlying = std::underlying_type_t<E>;
    return new E(static_cast<E>(static_cast<Underlying>(bits)));
}

template<typename E>
auto enum_box_thunk(std::uint64_t bits) -> void*
{
    using Underlying = std::underlying_type_t<E>;
    return new Underlying(static_cast<Underlying>(bits));
}

/// Whether E can back a dynamically allocated array (E[n]).
template<typename E>
consteval auto can_dynamic_array() -> bool
{
    return std::is_default_constructible_v<E> && std::is_destructible_v<E>
        && !std::is_array_v<E> && !std::is_reference_v<E>
        && !std::is_const_v<E> && !std::is_volatile_v<E>;
}

/// Byte offset of element 0 inside a dynamic array block (length header + align).
template<typename E>
consteval auto dynamic_array_offset() -> std::size_t
{
    constexpr std::size_t header = sizeof(std::size_t);
    constexpr std::size_t alignment = alignof(E);
    return (header + alignment - 1) / alignment * alignment;
}

/// Whether a dynamic array block of E needs the aligned operator new / delete.
template<typename E>
inline constexpr bool dynamic_array_over_aligned = alignof(E) > __STDCPP_DEFAULT_NEW_ALIGNMENT__;

/// Raw storage for a dynamic array block; the block start is aligned to alignof(E),
/// so (as Offset is a multiple of alignof(E)) element 0 is aligned too.
template<typename E>
auto dynamic_array_allocate(std::size_t bytes) -> void*
{
    if constexpr (dynamic_array_over_aligned<E>)
    {
        return ::operator new(bytes, std::align_val_t(alignof(E)));
    }
    else
    {
        return ::operator new(bytes);
    }
}

template<typename E>
auto dynamic_array_deallocate(void* block) -> void
{
    if constexpr (dynamic_array_over_aligned<E>)
    {
        ::operator delete(block, std::align_val_t(alignof(E)));
    }
    else
    {
        ::operator delete(block);
    }
}

/// Allocates and default-constructs `count` elements; nullptr when the byte
/// size (header + count * sizeof(E)) would overflow std::size_t.
template<typename E, std::size_t Offset>
auto new_array_thunk(std::size_t count) -> void*
{
    if (count > (std::numeric_limits<std::size_t>::max() - Offset) / sizeof(E))
    {
        return nullptr;
    }

    char* block = static_cast<char*>(dynamic_array_allocate<E>(Offset + count * sizeof(E)));
    *reinterpret_cast<std::size_t*>(block) = count;
    E* elements = reinterpret_cast<E*>(block + Offset);
    for (std::size_t i = 0; i < count; ++i)
    {
        ::new (static_cast<void*>(elements + i)) E();
    }
    return elements;
}

template<typename E, std::size_t Offset>
auto dynamic_array_release(void* data) -> void
{
    char* block = static_cast<char*>(data) - Offset;
    const std::size_t count = *reinterpret_cast<const std::size_t*>(block);
    E* elements = static_cast<E*>(data);
    for (std::size_t i = count; i > 0; --i)
    {
        elements[i - 1].~E();
    }
    dynamic_array_deallocate<E>(block);
}

/// Deep copy of a dynamic array: same length, elements copy-constructed.
template<typename E, std::size_t Offset>
auto dynamic_array_clone(const void* data) -> void*
{
    const char* source_block = static_cast<const char*>(data) - Offset;
    const std::size_t count = *reinterpret_cast<const std::size_t*>(source_block);

    // The source block exists, so its byte size cannot overflow.
    char* block = static_cast<char*>(dynamic_array_allocate<E>(Offset + count * sizeof(E)));
    *reinterpret_cast<std::size_t*>(block) = count;
    E* elements = reinterpret_cast<E*>(block + Offset);
    const E* source = static_cast<const E*>(data);
    for (std::size_t i = 0; i < count; ++i)
    {
        ::new (static_cast<void*>(elements + i)) E(source[i]);
    }
    return elements;
}

template<typename E, std::size_t Offset>
auto dynamic_array_destroy(void* data) -> void
{
    char* block = static_cast<char*>(data) - Offset;
    const std::size_t count = *reinterpret_cast<const std::size_t*>(block);
    E* elements = static_cast<E*>(data);
    for (std::size_t i = count; i > 0; --i)
    {
        elements[i - 1].~E();
    }
}

/// Display name of the dynamic array view of E: `E[]` (static storage).
template<typename E>
consteval auto dynamic_array_name(bool simple) -> std::string_view
{
    constexpr meta::info info = info_of<E>();
    const std::string_view element = (simple && meta::has_identifier(info))
        ? std::string_view(meta::identifier_of(info))
        : std::string_view(meta::display_string_of(info));
    std::string name(element);
    name += "[]";
    return std::define_static_string(name);
}

// ── Enumerator materialization ──

template<meta::info E>
consteval auto enumerator_count() -> std::size_t
{
    if constexpr (meta::is_enum_type(E) && !is_std_type(E))
    {
        return meta::enumerators_of(E).size();
    }
    else
    {
        return 0;
    }
}

template<meta::info E, std::size_t I>
consteval auto enumerator_info() -> meta::info
{
    return meta::enumerators_of(E)[I];
}

template<meta::info E, std::size_t I>
consteval auto enumerator_to_int() -> std::int64_t
{
    using Enum = typename[: E :];
    using Underlying = std::underlying_type_t<Enum>;
    const Enum value = meta::extract<Enum>(enumerator_info<E, I>());
    return static_cast<std::int64_t>(static_cast<Underlying>(value));
}

template<meta::info E, std::size_t I>
consteval auto enumerator_to_uint() -> std::uint64_t
{
    using Enum = typename[: E :];
    using Underlying = std::underlying_type_t<Enum>;
    const Enum value = meta::extract<Enum>(enumerator_info<E, I>());
    return static_cast<std::uint64_t>(static_cast<Underlying>(value));
}

template<meta::info E, std::size_t I>
consteval auto make_enum_desc(const TypeDescriptor* declaring) -> EnumDesc
{
    using Enum = typename[: E :];
    using Underlying = std::underlying_type_t<Enum>;

    EnumDesc desc{};
    desc.name = std::define_static_string(meta::identifier_of(enumerator_info<E, I>()));
    desc.declaring_enum = declaring;
    desc.value = enumerator_to_int<E, I>();
    desc.uvalue = enumerator_to_uint<E, I>();
    desc.is_unsigned = std::is_unsigned_v<Underlying>;
    desc.annotations = own_annotations<enumerator_info<E, I>()>().handles.data();
    desc.annotation_count = own_annotations<enumerator_info<E, I>()>().count;
    return desc;
}

template<meta::info C>
consteval auto make_descriptor(const Class* self, const void* fields, std::size_t field_count,
                               const void* methods, std::size_t method_count,
                               const void* declared_fields, std::size_t declared_field_count,
                               const void* declared_methods, std::size_t declared_method_count,
                               const void* ctors, std::size_t ctor_count,
                               const void* declared_ctors, std::size_t declared_ctor_count,
                               const void* bases, std::size_t base_count,
                               const void* enumerators, std::size_t enumerator_count) -> TypeDescriptor
{
    using Self = typename[: C :];

    TypeDescriptor desc{};
    desc.name = std::define_static_string(meta::has_identifier(C) ? meta::identifier_of(C) : meta::display_string_of(C));
    desc.qualified_name = std::define_static_string(meta::display_string_of(C));
    desc.normalized_name = std::define_static_string(normalize_type_name(meta::display_string_of(C)));
    desc.has_canonical_name = has_canonical_name(C);
    desc.kind = classify<C>();
    desc.self = self;

    if constexpr (meta::is_object_type(C) && meta::is_complete_type(C))
    {
        desc.size = meta::size_of(C);
        desc.alignment = meta::alignment_of(C);
    }
    else if constexpr (meta::is_reference_type(C))
    {
        // sizeof(T&) == sizeof(T); the object at the address has the referred type.
        using Referred = std::remove_reference_t<Self>;
        desc.size = sizeof(Referred);
        desc.alignment = alignof(Referred);
    }

    if constexpr (meta::is_class_type(C) || meta::is_union_type(C))
    {
        desc.is_polymorphic = meta::is_polymorphic_type(C);
        desc.is_abstract = meta::is_abstract_type(C);
        desc.is_final = meta::is_final_type(C);
        desc.is_aggregate = meta::is_aggregate_type(C);
        desc.is_empty = meta::is_empty_type(C);
        desc.capabilities = capabilities_of<C>();

        // A nested class carries its declared access; a namespace-scope class
        // (for which none of the three predicates holds) is public.
        std::uint32_t mods = Modifier::PUBLIC;
        if (meta::is_protected(C))     { mods = Modifier::PROTECTED; }
        else if (meta::is_private(C))  { mods = Modifier::PRIVATE; }
        if (meta::is_abstract_type(C)) { mods |= Modifier::ABSTRACT; }
        if (meta::is_final_type(C))    { mods |= Modifier::FINAL; }
        if (is_interface_class(C))     { mods |= Modifier::INTERFACE; }
        desc.modifiers = mods;
    }

    if constexpr (meta::is_enum_type(C))
    {
        using Underlying = std::underlying_type_t<Self>;
        desc.is_scoped_enum = meta::is_scoped_enum_type(C);
        desc.enum_is_unsigned = std::is_unsigned_v<Underlying>;
        desc.enum_read = &enum_read_thunk<Self>;
        desc.enum_box = &enum_box_thunk<Self>;
        if constexpr (!meta::is_const_type(C) && !meta::is_volatile_type(C))
        {
            desc.enum_make = &enum_make_thunk<Self>;
        }
    }

    if constexpr (is_unqualified_object(C) && meta::is_destructible_type(C))
    {
        desc.release = &release_thunk<Self>;
        desc.destroy = &destroy_thunk<Self>;
        if constexpr (meta::is_copy_constructible_type(C))
        {
            desc.clone = &clone_thunk<Self>;
        }
    }

    if constexpr (is_unqualified_object(C) && std::is_copy_assignable_v<Self>)
    {
        desc.copy_assign = +[](void* object, const void* value) -> bool
        {
            *static_cast<Self*>(object) = *static_cast<const Self*>(value);
            return true;
        };
    }

    if constexpr (can_dynamic_array<Self>())
    {
        desc.new_array = &new_array_thunk<Self, dynamic_array_offset<Self>()>;
    }

    desc.fields = fields;
    desc.field_count = field_count;
    desc.methods = methods;
    desc.method_count = method_count;
    desc.declared_fields = declared_fields;
    desc.declared_field_count = declared_field_count;
    desc.declared_methods = declared_methods;
    desc.declared_method_count = declared_method_count;
    desc.ctors = ctors;
    desc.ctor_count = ctor_count;
    desc.declared_ctors = declared_ctors;
    desc.declared_ctor_count = declared_ctor_count;
    desc.bases = bases;
    desc.base_count = base_count;
    desc.enumerators = enumerators;
    desc.enumerator_count = enumerator_count;

    return desc;
}

template<meta::info M, std::size_t J>
using parameter_type = typename[: meta::type_of(meta::parameters_of(M)[J]) :];

/**
 * @brief Everything materialized for one type, kept in a single variable so all
 *        internal references are self-references within one constexpr object.
 */
template<typename T>
struct TypeBundle
{
    std::array<FieldDesc, public_field_count<info_of<T>()>()>  field_descs{};
    std::array<Field, public_field_count<info_of<T>()>()>       field_handles{};
    std::array<MethodDesc, public_method_count<info_of<T>()>()> method_descs{};
    std::array<Method, public_method_count<info_of<T>()>()>     method_handles{};
    std::array<std::array<const Class*, max_parameters<info_of<T>()>()>, public_method_count<info_of<T>()>()> parameter_classes{};
    std::array<std::array<const TypeDescriptor*, max_parameters<info_of<T>()>()>, public_method_count<info_of<T>()>()> parameter_decays{};

    std::array<FieldDesc, declared_field_count<info_of<T>()>()>  declared_field_descs{};
    std::array<Field, declared_field_count<info_of<T>()>()>       declared_field_handles{};
    std::array<MethodDesc, declared_method_count<info_of<T>()>()> declared_method_descs{};
    std::array<Method, declared_method_count<info_of<T>()>()>     declared_method_handles{};
    std::array<std::array<const Class*, declared_max_parameters<info_of<T>()>()>, declared_method_count<info_of<T>()>()> declared_parameter_classes{};
    std::array<std::array<const TypeDescriptor*, declared_max_parameters<info_of<T>()>()>, declared_method_count<info_of<T>()>()> declared_parameter_decays{};

    std::array<CtorDesc, public_ctor_count<info_of<T>()>()>  ctor_descs{};
    std::array<Constructor, public_ctor_count<info_of<T>()>()> ctor_handles{};
    std::array<std::array<const Class*, max_ctor_parameters<info_of<T>()>()>, public_ctor_count<info_of<T>()>()> ctor_parameter_classes{};
    std::array<std::array<const TypeDescriptor*, max_ctor_parameters<info_of<T>()>()>, public_ctor_count<info_of<T>()>()> ctor_parameter_decays{};

    std::array<CtorDesc, declared_ctor_count<info_of<T>()>()>  declared_ctor_descs{};
    std::array<Constructor, declared_ctor_count<info_of<T>()>()> declared_ctor_handles{};
    std::array<std::array<const Class*, declared_max_ctor_parameters<info_of<T>()>()>, declared_ctor_count<info_of<T>()>()> declared_ctor_parameter_classes{};
    std::array<std::array<const TypeDescriptor*, declared_max_ctor_parameters<info_of<T>()>()>, declared_ctor_count<info_of<T>()>()> declared_ctor_parameter_decays{};

    std::array<BaseClass, base_count<info_of<T>()>()> bases{};

    std::array<EnumDesc, enumerator_count<info_of<T>()>()> enum_descs{};
    std::array<Enumerator, enumerator_count<info_of<T>()>()> enum_handles{};

    // ── Phase 9: structure ──
    std::array<TemplateArgDesc, template_argument_count<info_of<T>()>()> template_arg_descs{};
    std::array<TemplateArgument, template_argument_count<info_of<T>()>()> template_arg_handles{};

    std::array<const Class*, function_parameter_count<info_of<T>()>()> function_parameter_classes{};
    std::array<const TypeDescriptor*, function_parameter_count<info_of<T>()>()> function_parameter_decays{};

    std::array<const Class*, nested_type_count<info_of<T>()>()> nested_classes{};

    std::array<const Class*, aggregate_slot_count<info_of<T>()>()> aggregate_parameter_classes{};
    std::array<const TypeDescriptor*, aggregate_slot_count<info_of<T>()>()> aggregate_parameter_decays{};
    CtorDesc    aggregate_ctor_desc;
    Constructor aggregate_ctor_handle;

    CallableDesc callable_desc;

    TypeDescriptor dynamic_array_desc;  ///< the E[] view for dynamic arrays of T
    Class          dynamic_array_cls;

    Class          cls;
    TypeDescriptor desc;
};

template<typename T>
inline constexpr TypeBundle<T> bundle = []() consteval -> TypeBundle<T>
{
    constexpr meta::info C = info_of<T>();
    TypeBundle<T> b{};

    [&b]<std::size_t... I>(std::index_sequence<I...>)
    {
        (([&b]()
        {
            constexpr meta::info M = public_field_member<C, I>();
            using FT = typename[: meta::type_of(M) :];
            b.field_descs[I] = make_field_desc<M>(std::addressof(bundle<FT>.desc),
                                                  std::addressof(bundle<T>.desc));
            b.field_handles[I] = Access::make<Field>(std::addressof(bundle<T>.field_descs[I]));
        }()), ...);
    }(std::make_index_sequence<public_field_count<C>()>{});

    [&b]<std::size_t... I>(std::index_sequence<I...>)
    {
        (([&b]()
        {
            constexpr meta::info M = public_method_member<C, I>();
            const TypeDescriptor* return_desc = nullptr;
            if constexpr (!meta::is_destructor(M))
            {
                using RT = typename[: meta::return_type_of(M) :];
                return_desc = std::addressof(bundle<RT>.desc);
            }
            // A destructor has no return-type descriptor (Method::get_return_type()
            // falls back to class_of<void>()). Referencing bundle<void> here would
            // make bundle<void> reference itself through a concrete spelling, which
            // GCC 16.2 mis-initializes to a default descriptor.
            [&b]<std::size_t... J>(std::index_sequence<J...>)
            {
                std::array<const Class*, sizeof...(J)> row{ std::addressof(bundle<parameter_type<M, J>>.cls)... };
                std::array<const TypeDescriptor*, sizeof...(J)> decays{
                    std::addressof(bundle<std::remove_cvref_t<parameter_type<M, J>>>.desc)...
                };
                for (std::size_t j = 0; j < row.size(); ++j)
                {
                    b.parameter_classes[I][j] = row[j];
                    b.parameter_decays[I][j] = decays[j];
                }
            }(std::make_index_sequence<meta::parameters_of(M).size()>{});
            b.method_descs[I] = make_method_desc<M>(std::addressof(bundle<T>.desc),
                                                    return_desc,
                                                    bundle<T>.parameter_classes[I].data(),
                                                    bundle<T>.parameter_decays[I].data(),
                                                    meta::parameters_of(M).size());
            b.method_handles[I] = Access::make<Method>(std::addressof(bundle<T>.method_descs[I]));
        }()), ...);
    }(std::make_index_sequence<public_method_count<C>()>{});

    [&b]<std::size_t... I>(std::index_sequence<I...>)
    {
        (([&b]()
        {
            constexpr meta::info M = declared_field_member<C, I>();
            using FT = typename[: meta::type_of(M) :];
            b.declared_field_descs[I] = make_field_desc<M>(std::addressof(bundle<FT>.desc),
                                                           std::addressof(bundle<T>.desc));
            b.declared_field_handles[I] = Access::make<Field>(std::addressof(bundle<T>.declared_field_descs[I]));
        }()), ...);
    }(std::make_index_sequence<declared_field_count<C>()>{});

    [&b]<std::size_t... I>(std::index_sequence<I...>)
    {
        (([&b]()
        {
            constexpr meta::info M = declared_method_member<C, I>();
            const TypeDescriptor* return_desc = nullptr;
            if constexpr (!meta::is_destructor(M))
            {
                using RT = typename[: meta::return_type_of(M) :];
                return_desc = std::addressof(bundle<RT>.desc);
            }
            // A destructor has no return-type descriptor (Method::get_return_type()
            // falls back to class_of<void>()). Referencing bundle<void> here would
            // make bundle<void> reference itself through a concrete spelling, which
            // GCC 16.2 mis-initializes to a default descriptor.
            [&b]<std::size_t... J>(std::index_sequence<J...>)
            {
                std::array<const Class*, sizeof...(J)> row{ std::addressof(bundle<parameter_type<M, J>>.cls)... };
                std::array<const TypeDescriptor*, sizeof...(J)> decays{
                    std::addressof(bundle<std::remove_cvref_t<parameter_type<M, J>>>.desc)...
                };
                for (std::size_t j = 0; j < row.size(); ++j)
                {
                    b.declared_parameter_classes[I][j] = row[j];
                    b.declared_parameter_decays[I][j] = decays[j];
                }
            }(std::make_index_sequence<meta::parameters_of(M).size()>{});
            b.declared_method_descs[I] = make_method_desc<M>(std::addressof(bundle<T>.desc),
                                                             return_desc,
                                                             bundle<T>.declared_parameter_classes[I].data(),
                                                             bundle<T>.declared_parameter_decays[I].data(),
                                                             meta::parameters_of(M).size());
            b.declared_method_handles[I] = Access::make<Method>(std::addressof(bundle<T>.declared_method_descs[I]));
        }()), ...);
    }(std::make_index_sequence<declared_method_count<C>()>{});

    [&b]<std::size_t... I>(std::index_sequence<I...>)
    {
        (([&b]()
        {
            constexpr meta::info M = public_ctor_member<C, I>();
            [&b]<std::size_t... J>(std::index_sequence<J...>)
            {
                std::array<const Class*, sizeof...(J)> row{ std::addressof(bundle<parameter_type<M, J>>.cls)... };
                std::array<const TypeDescriptor*, sizeof...(J)> decays{
                    std::addressof(bundle<std::remove_cvref_t<parameter_type<M, J>>>.desc)...
                };
                for (std::size_t j = 0; j < row.size(); ++j)
                {
                    b.ctor_parameter_classes[I][j] = row[j];
                    b.ctor_parameter_decays[I][j] = decays[j];
                }
            }(std::make_index_sequence<meta::parameters_of(M).size()>{});
            b.ctor_descs[I] = make_ctor_desc<C, M>(std::addressof(bundle<T>.desc),
                                                   bundle<T>.ctor_parameter_classes[I].data(),
                                                   bundle<T>.ctor_parameter_decays[I].data(),
                                                   meta::parameters_of(M).size());
            b.ctor_handles[I] = Access::make<Constructor>(std::addressof(bundle<T>.ctor_descs[I]));
        }()), ...);
    }(std::make_index_sequence<public_ctor_count<C>()>{});

    [&b]<std::size_t... I>(std::index_sequence<I...>)
    {
        (([&b]()
        {
            constexpr meta::info M = declared_ctor_member<C, I>();
            [&b]<std::size_t... J>(std::index_sequence<J...>)
            {
                std::array<const Class*, sizeof...(J)> row{ std::addressof(bundle<parameter_type<M, J>>.cls)... };
                std::array<const TypeDescriptor*, sizeof...(J)> decays{
                    std::addressof(bundle<std::remove_cvref_t<parameter_type<M, J>>>.desc)...
                };
                for (std::size_t j = 0; j < row.size(); ++j)
                {
                    b.declared_ctor_parameter_classes[I][j] = row[j];
                    b.declared_ctor_parameter_decays[I][j] = decays[j];
                }
            }(std::make_index_sequence<meta::parameters_of(M).size()>{});
            b.declared_ctor_descs[I] = make_ctor_desc<C, M>(std::addressof(bundle<T>.desc),
                                                            bundle<T>.declared_ctor_parameter_classes[I].data(),
                                                            bundle<T>.declared_ctor_parameter_decays[I].data(),
                                                            meta::parameters_of(M).size());
            b.declared_ctor_handles[I] = Access::make<Constructor>(std::addressof(bundle<T>.declared_ctor_descs[I]));
        }()), ...);
    }(std::make_index_sequence<declared_ctor_count<C>()>{});

    [&b]<std::size_t... I>(std::index_sequence<I...>)
    {
        (([&b]()
        {
            constexpr meta::info B = base_member<C, I>();
            using BT = std::remove_cvref_t<typename[: meta::type_of(B) :]>;
            std::uint32_t mods = 0;
            if (meta::is_public(B))         { mods |= Modifier::PUBLIC; }
            else if (meta::is_protected(B)) { mods |= Modifier::PROTECTED; }
            else if (meta::is_private(B))   { mods |= Modifier::PRIVATE; }
            if (meta::is_virtual(B))        { mods |= Modifier::VIRTUAL; }
            b.bases[I].type = std::addressof(bundle<BT>.cls);
            b.bases[I].modifiers = Modifier(mods);
            // Pointer-adjustment thunk (handles multiple / virtual bases). Only
            // public bases are up-castable at namespace scope; inaccessible bases
            // keep a null thunk. `if constexpr` keeps the ill-formed cast for a
            // private/protected base from being instantiated.
            if constexpr (meta::is_public(B))
            {
                b.bases[I].cast = +[](void* pointer) -> void*
                {
                    return static_cast<BT*>(static_cast<T*>(pointer));
                };
            }
            b.bases[I].annotations = own_annotations<B>().handles.data();
            b.bases[I].annotation_count = own_annotations<B>().count;
            // Static offset of the base subobject. A virtual base has no constant
            // offset (it changes with the complete object), and an inaccessible
            // base cannot be reached at namespace scope, so both keep a null thunk.
            if constexpr (meta::is_public(B) && !meta::is_virtual(B))
            {
                b.bases[I].offset = &constant_offset<static_cast<std::size_t>(meta::offset_of(B).bytes)>;
            }
        }()), ...);
    }(std::make_index_sequence<base_count<C>()>{});

    [&b]<std::size_t... I>(std::index_sequence<I...>)
    {
        (([&b]()
        {
            b.enum_descs[I] = make_enum_desc<C, I>(std::addressof(bundle<T>.desc));
            b.enum_handles[I] = Access::make<Enumerator>(std::addressof(bundle<T>.enum_descs[I]));
        }()), ...);
    }(std::make_index_sequence<enumerator_count<C>()>{});

    b.cls = Access::make<Class>(std::addressof(bundle<T>.desc));
    b.desc = make_descriptor<C>(std::addressof(bundle<T>.cls),
                                bundle<T>.field_handles.data(), public_field_count<C>(),
                                bundle<T>.method_handles.data(), public_method_count<C>(),
                                bundle<T>.declared_field_handles.data(), declared_field_count<C>(),
                                bundle<T>.declared_method_handles.data(), declared_method_count<C>(),
                                bundle<T>.ctor_handles.data(), public_ctor_count<C>(),
                                bundle<T>.declared_ctor_handles.data(), declared_ctor_count<C>(),
                                bundle<T>.bases.data(), base_count<C>(),
                                bundle<T>.enum_handles.data(), enumerator_count<C>());

    if constexpr (meta::is_class_type(C) || meta::is_union_type(C) || meta::is_enum_type(C))
    {
        // cv-qualified classes see the unqualified entity's annotations. Pointers
        // and references do not (annotations_of on those is empty, and calling it
        // is not required).
        b.desc.annotations = own_annotations<meta::remove_cv(C)>().handles.data();
        b.desc.annotation_count = own_annotations<meta::remove_cv(C)>().count;
    }

    // ── Phase 9: template arguments (api_design §1) ──
    [&b]<std::size_t... I>(std::index_sequence<I...>)
    {
        (([&b]()
        {
            constexpr meta::info A = template_argument_info<C, I>();
            constexpr TemplateArgumentKind kind = template_argument_kind<A>();

            TemplateArgDesc desc{};
            desc.kind = kind;
            desc.spelling = std::define_static_string(meta::display_string_of(A));
            if constexpr (kind == TemplateArgumentKind::Type)
            {
                desc.type = std::addressof(bundle<typename[: A :]>.desc);
            }
            else if constexpr (kind == TemplateArgumentKind::Template)
            {
                desc.primary_template = std::addressof(template_bundle<A>.desc);
            }
            else if constexpr (kind == TemplateArgumentKind::Value)
            {
                using ValueType = std::remove_cv_t<typename[: meta::type_of(A) :]>;
                desc.type = std::addressof(bundle<ValueType>.desc);
                desc.value = template_argument_value<A>();
            }

            b.template_arg_descs[I] = desc;
            b.template_arg_handles[I] = Access::make<TemplateArgument>(std::addressof(bundle<T>.template_arg_descs[I]));
        }()), ...);
    }(std::make_index_sequence<template_argument_count<C>()>{});

    if constexpr (is_template_specialization_info(C))
    {
        b.desc.primary_template = std::addressof(template_bundle<meta::template_of(C)>.desc);
        b.desc.template_arguments = bundle<T>.template_arg_handles.data();
        b.desc.template_argument_count = template_argument_count<C>();
    }

    // ── Phase 9: function type decomposition (api_design §2.2) ──
    [&b]<std::size_t... K>(std::index_sequence<K...>)
    {
        std::array<const Class*, sizeof...(K)> classes{
            std::addressof(bundle<typename[: function_parameter_type<C, K>() :]>.cls)...
        };
        std::array<const TypeDescriptor*, sizeof...(K)> decays{
            std::addressof(bundle<std::remove_cvref_t<typename[: function_parameter_type<C, K>() :]>>.desc)...
        };
        for (std::size_t k = 0; k < classes.size(); ++k)
        {
            b.function_parameter_classes[k] = classes[k];
            b.function_parameter_decays[k] = decays[k];
        }
    }(std::make_index_sequence<function_parameter_count<C>()>{});

    if constexpr (meta::is_function_type(C))
    {
        using FunctionReturn = typename[: meta::return_type_of(C) :];
        b.desc.function_return = std::addressof(bundle<FunctionReturn>.desc);
        b.desc.function_parameters = bundle<T>.function_parameter_classes.data();
        b.desc.function_parameter_decays = bundle<T>.function_parameter_decays.data();
        b.desc.function_parameter_count = function_parameter_count<C>();
        b.desc.function_is_variadic = meta::is_vararg_function(C);
        b.desc.function_is_noexcept = meta::is_noexcept(C);
    }

    // ── Phase 9: nested types (api_design §4) ──
    [&b]<std::size_t... K>(std::index_sequence<K...>)
    {
        ((b.nested_classes[K] = std::addressof(bundle<typename[: nested_type_member<C, K>() :]>.cls)), ...);
    }(std::make_index_sequence<nested_type_count<C>()>{});

    b.desc.nested_types = bundle<T>.nested_classes.data();
    b.desc.nested_type_count = nested_type_count<C>();

    if constexpr (meta::has_parent(C))
    {
        constexpr meta::info Parent = meta::parent_of(C);
        // A namespace parent is not a type (`is_type` guards is_class_type, which
        // throws for non-types).
        if constexpr (meta::is_type(Parent) && (meta::is_class_type(Parent) || meta::is_union_type(Parent)))
        {
            b.desc.enclosing_class = std::addressof(bundle<typename[: Parent :]>.desc);
        }
    }

    // ── Phase 9: aggregate initializer (api_design §3) ──
    if constexpr (is_aggregate_initializable(C))
    {
        [&b]<std::size_t... K>(std::index_sequence<K...>)
        {
            std::array<const Class*, sizeof...(K)> classes{
                std::addressof(bundle<typename[: aggregate_slot_type<C, K>() :]>.cls)...
            };
            std::array<const TypeDescriptor*, sizeof...(K)> decays{
                std::addressof(bundle<std::remove_cvref_t<typename[: aggregate_slot_type<C, K>() :]>>.desc)...
            };
            for (std::size_t k = 0; k < classes.size(); ++k)
            {
                b.aggregate_parameter_classes[k] = classes[k];
                b.aggregate_parameter_decays[k] = decays[k];
            }
        }(std::make_index_sequence<aggregate_slot_count<C>()>{});

        CtorDesc aggregate_desc{};
        aggregate_desc.signature = std::define_static_string(aggregate_signature<C>());
        aggregate_desc.declaring_class = std::addressof(bundle<T>.desc);
        aggregate_desc.parameter_types = bundle<T>.aggregate_parameter_classes.data();
        aggregate_desc.parameter_decays = bundle<T>.aggregate_parameter_decays.data();
        aggregate_desc.parameter_count = aggregate_slot_count<C>();
        aggregate_desc.modifiers = Modifier::PUBLIC;
        aggregate_desc.new_instance = make_aggregate_new_instance<C>(std::make_index_sequence<aggregate_slot_count<C>()>{});
        aggregate_desc.construct_at = make_aggregate_construct_at<C>(std::make_index_sequence<aggregate_slot_count<C>()>{});
        if constexpr (meta::is_destructible_type(C))
        {
            aggregate_desc.destroy_at = &canonical_destroy<T>;
        }

        b.aggregate_ctor_desc = aggregate_desc;
        b.aggregate_ctor_handle = Access::make<Constructor>(std::addressof(bundle<T>.aggregate_ctor_desc));
        b.desc.aggregate_initializer = std::addressof(bundle<T>.aggregate_ctor_handle);
    }

    // ── Phase 9: callable values (api_design §2.3) ──
    if constexpr (std::is_pointer_v<T> && std::is_function_v<std::remove_pointer_t<T>>)
    {
        using Call = function_pointer_call<T>;
        b.callable_desc.type = std::addressof(bundle<T>.desc);
        b.callable_desc.function_type = std::addressof(bundle<typename Call::function_type>.desc);
        if constexpr (Call::can_box)
        {
            b.callable_desc.invoke = &Call::box;
        }
        if constexpr (std::is_reference_v<typename Call::return_type>)
        {
            b.callable_desc.returns_reference = true;
            b.callable_desc.invoke_ref = &Call::ref;
        }
        b.desc.callable = std::addressof(bundle<T>.callable_desc);
    }
    else if constexpr (std::is_member_function_pointer_v<T>)
    {
        using Call = member_pointer_call<T>;
        b.callable_desc.type = std::addressof(bundle<T>.desc);
        b.callable_desc.function_type = std::addressof(bundle<typename Call::function_type>.desc);
        b.callable_desc.declaring_class = std::addressof(bundle<typename Call::caller_type>.desc);
        b.callable_desc.is_member_function_pointer = true;
        if constexpr (Call::can_box)
        {
            b.callable_desc.invoke = &Call::box;
        }
        if constexpr (std::is_reference_v<typename Call::return_type>)
        {
            b.callable_desc.returns_reference = true;
            b.callable_desc.invoke_ref = &Call::ref;
        }
        b.desc.callable = std::addressof(bundle<T>.callable_desc);
    }

    // ── Decomposition / qualification: cross-bundle references (dependent
    //    spellings only; literal concrete spellings trip a GCC 16.2 bug). ──
    b.desc.decayed = std::addressof(bundle<std::remove_cvref_t<T>>.desc);
    if constexpr (std::is_pointer_v<T>)
    {
        b.desc.pointee = std::addressof(bundle<std::remove_pointer_t<T>>.desc);
    }
    if constexpr (std::is_member_pointer_v<T>)
    {
        using MemberClass = typename member_pointer_class<T>::type;
        b.desc.pointee_class = std::addressof(bundle<MemberClass>.desc);
    }
    if constexpr (std::is_array_v<T>)
    {
        b.desc.element = std::addressof(bundle<std::remove_extent_t<T>>.desc);
        b.desc.array_extent = std::extent_v<T>;
        b.desc.array_rank = std::rank_v<T>;
    }
    if constexpr (std::is_reference_v<T>)
    {
        b.desc.referenced = std::addressof(bundle<std::remove_reference_t<T>>.desc);
        b.desc.reference_kind = std::is_lvalue_reference_v<T> ? RefQualifier::Lvalue : RefQualifier::Rvalue;
    }
    if constexpr (std::is_const_v<T> || std::is_volatile_v<T>)
    {
        b.desc.unqualified = std::addressof(bundle<std::remove_cv_t<T>>.desc);
    }
    b.desc.is_const_qualified = std::is_const_v<T>;
    b.desc.is_volatile_qualified = std::is_volatile_v<T>;
    if constexpr (std::is_enum_v<T>)
    {
        using Underlying = std::underlying_type_t<T>;
        b.desc.underlying = std::addressof(bundle<Underlying>.desc);
    }

    // ── Dynamic-array view for element type T (Array::new_instance) ──
    if constexpr (can_dynamic_array<T>())
    {
        constexpr std::size_t offset = dynamic_array_offset<T>();

        // The length lives in the allocation header, so there is no static size or
        // extent. The name is a display string only: it is not canonical, hence
        // never registered (every `E[]` would otherwise compete for the table).
        b.dynamic_array_desc.name = dynamic_array_name<T>(true);
        b.dynamic_array_desc.qualified_name = dynamic_array_name<T>(false);
        b.dynamic_array_desc.normalized_name = b.dynamic_array_desc.qualified_name;
        b.dynamic_array_desc.has_canonical_name = false;
        b.dynamic_array_desc.decayed = std::addressof(bundle<T>.dynamic_array_desc);
        b.dynamic_array_desc.kind = TypeKind::Array;
        b.dynamic_array_desc.element = std::addressof(bundle<T>.desc);
        b.dynamic_array_desc.array_rank = 1;
        b.dynamic_array_desc.alignment = alignof(T);
        b.dynamic_array_desc.is_dynamic_array = true;
        b.dynamic_array_desc.array_data_offset = offset;
        b.dynamic_array_desc.release = &dynamic_array_release<T, offset>;
        b.dynamic_array_desc.destroy = &dynamic_array_destroy<T, offset>;
        if constexpr (std::is_copy_constructible_v<T>)
        {
            b.dynamic_array_desc.clone = &dynamic_array_clone<T, offset>;
        }
        b.dynamic_array_desc.self = std::addressof(bundle<T>.dynamic_array_cls);
        b.dynamic_array_cls = Access::make<Class>(std::addressof(bundle<T>.dynamic_array_desc));
        b.desc.dynamic_array = std::addressof(bundle<T>.dynamic_array_desc);
    }

    return b;
}();

/// Descriptor for the object (cv/ref-normalized) type of T; object handles
/// (`Object` / `ObjectRef`) never carry references or top-level cv.
template<typename T>
consteval auto descriptor_of_ref() -> const TypeDescriptor&
{
    return bundle<std::remove_cvref_t<T>>.desc;
}

template<typename T>
consteval auto annotation_type_descriptor() -> const TypeDescriptor*
{
    return std::addressof(bundle<T>.desc);
}

} // namespace speculo::detail

// ─────────────────────────────────────────────────────────────────────────────
// Deferred definitions of the class_of-dependent Object / ObjectRef members
// ─────────────────────────────────────────────────────────────────────────────

export namespace speculo
{

auto Annotation::get_type() const -> const Class&
{
    return *m_desc->type->self;
}

template<typename T>
auto Annotation::get() const -> Option<const T&>
{
    using U = std::remove_const_t<T>;
    if (m_desc->value == nullptr || m_desc->type != std::addressof(detail::descriptor_of_ref<U>()))
    {
        return None{};
    }

    return *static_cast<const U*>(m_desc->value);
}

auto Annotation::value() const -> Outcome<Object>
{
    if (m_desc->value == nullptr)
    {
        return Error{ErrorKind::NotSupported,
                     std::format("Annotation::value(): the value of type '{}' cannot be read",
                                 m_desc->type->qualified_name)};
    }

    if (m_desc->type->clone == nullptr)
    {
        return Error{ErrorKind::NotSupported,
                     std::format("Annotation::value(): '{}' cannot be copied", m_desc->type->qualified_name)};
    }

    return Object::from_raw(m_desc->type->clone(m_desc->value), m_desc->type->decayed);
}

template<typename T>
    requires (!std::is_same_v<std::remove_cvref_t<T>, ObjectRef>
           && !std::is_same_v<std::remove_cvref_t<T>, Object>)
ObjectRef::ObjectRef(T& obj) noexcept
    : m_data(const_cast<void*>(static_cast<const volatile void*>(std::addressof(obj))))
    , m_desc(std::addressof(detail::descriptor_of_ref<std::remove_cvref_t<T>>()))
    , m_is_const(std::is_const_v<T>)
    , m_is_volatile(std::is_volatile_v<T>)
{
}

inline ObjectRef::ObjectRef(const void* data, const Class& type) noexcept
    : m_data(data)
    , m_desc(type.descriptor())
    , m_is_const(true)
{
}

inline ObjectRef::ObjectRef(void* data, const Class& type) noexcept
    : m_data(data)
    , m_desc(type.descriptor())
    , m_is_const(false)
{
}

template<typename T>
auto ObjectRef::as() noexcept -> Option<T&>
{
    using U = std::remove_cv_t<T>;
    if ((m_is_const && !std::is_const_v<T>) || (m_is_volatile && !std::is_volatile_v<T>) || m_desc == nullptr
        || m_desc != std::addressof(detail::descriptor_of_ref<U>()))
    {
        return None{};
    }

    return *static_cast<T*>(const_cast<void*>(m_data));
}

template<typename T>
auto ObjectRef::as() const noexcept -> Option<const T&>
{
    using U = std::remove_cv_t<T>;
    if ((m_is_volatile && !std::is_volatile_v<T>) || m_desc == nullptr
        || m_desc != std::addressof(detail::descriptor_of_ref<U>()))
    {
        return None{};
    }

    return *static_cast<const T*>(m_data);
}

template<typename T>
auto Object::from_value(T value) -> Outcome<Object>
{
    using U = std::remove_cvref_t<T>;
    U* data = new U(std::move(value));

    return from_raw(data, std::addressof(detail::descriptor_of_ref<U>()));
}

template<typename T, typename... CtorArgs>
    requires std::is_constructible_v<std::remove_cvref_t<T>, CtorArgs...>
auto Object::make(CtorArgs&&... args) -> Outcome<Object>
{
    using U = std::remove_cvref_t<T>;
    U* data = new U(std::forward<CtorArgs>(args)...);

    return from_raw(data, std::addressof(detail::descriptor_of_ref<U>()));
}

template<typename T>
auto Object::as() noexcept -> Option<T&>
{
    using U = std::remove_cv_t<T>;
    if (m_desc == nullptr || m_desc != std::addressof(detail::descriptor_of_ref<U>()))
    {
        return None{};
    }

    return *static_cast<U*>(m_data);
}

template<typename T>
auto Object::as() const noexcept -> Option<const T&>
{
    using U = std::remove_cv_t<T>;
    if (m_desc == nullptr || m_desc != std::addressof(detail::descriptor_of_ref<U>()))
    {
        return None{};
    }

    return *static_cast<const U*>(m_data);
}

template<typename T>
auto Object::take() && -> Outcome<T>
{
    using U = std::remove_cv_t<T>;
    if (m_data == nullptr || m_desc == nullptr)
    {
        return Error{ErrorKind::NullInstance, "Object::take(): the object is empty"};
    }

    if (m_desc != std::addressof(detail::descriptor_of_ref<U>()))
    {
        return Error{ErrorKind::TypeMismatch,
                     std::format("Object::take(): the object holds '{}', not '{}'", m_desc->qualified_name,
                                 detail::descriptor_of_ref<U>().qualified_name)};
    }

    U value = std::move(*static_cast<U*>(m_data));
    reset();

    return value;
}

/**
 * @brief Main entry point: reflect a type
 * @tparam T Any type (value, cv-qualified, reference, pointer, array, function)
 * @note The first call for a given T registers it in the global name table
 *       (thread-safe magic static, one-time); the descriptor table itself
 *       remains read-only and fully compile-time materialized.
 */
template<typename T>
[[nodiscard]] auto class_of() -> const Class&
{
    static const bool registered = []
    {
        detail::register_type(detail::bundle<T>.cls);
        return true;
    }();
    (void)registered;

    return detail::bundle<T>.cls;
}

/**
 * @brief Convenience alias for class_of<T>()
 */
template<typename T>
[[nodiscard]] auto type_of() -> const Class&
{
    return class_of<T>();
}

} // namespace speculo
