export module speculo:descriptor;

import std;
import crab_cpp;
import :modifier;

export namespace speculo
{
class Class;
}

namespace speculo::detail
{

// ─────────────────────────────────────────────────────────────────────────────
// Type-erased thunk ABI between the descriptor layer and the (reflection-driven)
// materialization layer.
// ─────────────────────────────────────────────────────────────────────────────
using ReleaseFn       = void (*)(void*);                ///< destroy in place and free heap storage
using DestroyFn       = void (*)(void*);                ///< destroy in place only
using CloneFn         = void* (*)(const void*);         ///< allocate and copy-construct
using FieldGetFn      = void* (*)(const void*);         ///< heap copy of a member value
using FieldSetFn      = bool (*)(void*, const void*);   ///< copy-assign a member value
using FieldRefFn      = void* (*)(void*);               ///< address of a subobject
using InvokeFn        = void* (*)(void*, const void* const*); ///< self + argument pointers
using InvokeRefFn     = const void* (*)(void*, const void* const*); ///< self + argument pointers -> address of the referent
using CtorFn          = void* (*)(const void* const*);  ///< heap-construct from argument pointers
using PlacementCtorFn = void (*)(void*, const void* const*);
using UpcastFn        = void* (*)(void*);               ///< derived pointer -> base subobject
using CopyAssignFn    = bool (*)(void*, const void*);   ///< copy-assign a value into an existing object
using NewArrayFn      = void* (*)(std::size_t);         ///< allocate E[n], return the element-0 address
using EnumReadFn      = std::uint64_t (*)(const void*); ///< read an enum object's underlying bits
using EnumMakeFn      = void* (*)(std::uint64_t);       ///< heap-construct an enum from underlying bits
using EnumBoxFn       = void* (*)(std::uint64_t);       ///< heap-construct the *underlying integer* from its bits
using CallableInvokeFn    = void* (*)(const void* callable, void* self, const void* const* args);
using CallableInvokeRefFn = const void* (*)(const void* callable, void* self, const void* const* args);
using BaseOffsetFn        = std::size_t (*)(); ///< base subobject offset; nullptr when there is none

struct TypeDescriptor;
struct TemplateArgDesc;
struct CallableDesc;

/**
 * @brief Bits of TypeDescriptor::capabilities (api_design §5.2)
 *
 * Every bit is evaluated while materializing the type, so a class-level query is
 * one bit test at run time. A bit is only meaningful for class / union types;
 * for everything else the whole set is 0 and the queries answer false.
 */
enum class Capability : std::uint32_t
{
    TriviallyCopyable             = 1u << 0,
    TriviallyDefaultConstructible = 1u << 1,
    TriviallyDestructible         = 1u << 2,
    VirtualDestructor             = 1u << 3,
    StandardLayout                = 1u << 4,
    UniqueObjectRepresentations   = 1u << 5,
};

[[nodiscard]] constexpr auto has_capability(std::uint32_t bits, Capability bit) noexcept -> bool
{
    return (bits & static_cast<std::uint32_t>(bit)) != 0;
}


/**
 * @brief One materialized annotation value
 *
 * `value` is null when the value is not readable on this compiler (its type
 * contains a pointer or a reference). `type` is still set, with top-level const
 * removed.
 */
struct AnnotationDesc
{
    const TypeDescriptor* type = nullptr;
    const void* value = nullptr;
};

struct Access; ///< the one door to the handles' private internals (defined in `speculo:object`)

/**
 * @brief A handle's pointer to its descriptor, checked on every dereference
 *
 * Handles (`Class`, `Field`, `Method`, ...) are default-constructible and then
 * hold no descriptor. Reading through such an empty handle is a programmer
 * error and stops the program with a message instead of dereferencing null.
 */
template<typename Desc>
class CheckedDesc
{
public:
    constexpr CheckedDesc() noexcept = default;

    constexpr CheckedDesc(const Desc* desc) noexcept
        : m_desc(desc)
    {
    }

    [[nodiscard]] auto operator->() const -> const Desc*
    {
        if (m_desc == nullptr)
        {
            crab_cpp::panic("speculo: a default-constructed (empty) handle was used");
        }

        return m_desc;
    }

    /// The raw pointer, null for an empty handle (no check).
    [[nodiscard]] constexpr auto get() const noexcept -> const Desc* { return m_desc; }

    [[nodiscard]] constexpr auto operator==(const CheckedDesc&) const noexcept -> bool = default;

private:
    const Desc* m_desc = nullptr;
};

/**
 * @brief Runtime layout of one data member
 */
struct FieldDesc
{
    std::string_view name;
    const TypeDescriptor* type = nullptr;            ///< field type (references normalized)
    const TypeDescriptor* declaring_class = nullptr;
    std::uint32_t modifiers = 0;

    std::size_t size = 0;         ///< sizeof(field type); for bitfields see bit_width
    std::size_t alignment = 0;
    std::size_t offset = 0;       ///< byte offset inside the host object
    std::size_t bit_offset = 0;   ///< absolute bit offset
    std::size_t bit_width = 0;    ///< bitfield width (valid when is_bitfield)

    bool is_bitfield = false;
    bool is_static = false;
    bool is_mutable = false;
    bool is_reference = false;

    FieldGetFn get = nullptr;
    FieldSetFn set = nullptr;
    FieldRefFn get_ref = nullptr;

    const void* annotations = nullptr; ///< const Annotation[]
    std::size_t annotation_count = 0;
};

/**
 * @brief Runtime description of one (non-overloaded) member function
 */
struct MethodDesc
{
    std::string_view name;
    std::string_view signature;
    const TypeDescriptor* declaring_class = nullptr;
    const TypeDescriptor* return_type = nullptr;
    const Class* const* parameter_types = nullptr;
    const TypeDescriptor* const* parameter_decays = nullptr; ///< cv/ref-normalized parameter types
    std::size_t parameter_count = 0;
    std::uint32_t modifiers = 0;
    RefQualifier ref_qualifier = RefQualifier::None;
    bool is_operator = false;   ///< operator+ / operator= / conversion operator / ...
    bool is_destructor = false; ///< user-declared destructor (not invokable via invoke())

    InvokeFn invoke = nullptr;          ///< boxes a copy of the result (nullptr when it cannot be boxed)
    InvokeRefFn invoke_ref = nullptr;   ///< reference-returning methods only: address of the referent, no copy

    const void* annotations = nullptr; ///< const Annotation[] on the function itself
    std::size_t annotation_count = 0;
    const void* parameter_annotations = nullptr; ///< const Annotation* const[] , one span per parameter
    const std::size_t* parameter_annotation_counts = nullptr;
};

/**
 * @brief Runtime description of one namespace-scope function
 *
 * There is no declaring class. `signature` is the registry key: the compiler's
 * display string, plus " noexcept" when the function is noexcept (the display
 * string itself omits it).
 */
struct FunctionDesc
{
    std::string_view name;
    std::string_view namespace_name; ///< empty at global scope
    std::string_view qualified_name; ///< namespace::name, or just the name
    std::string_view signature;
    const TypeDescriptor* return_type = nullptr;
    const Class* const* parameter_types = nullptr;
    const TypeDescriptor* const* parameter_decays = nullptr;
    std::size_t parameter_count = 0;
    std::uint32_t modifiers = 0; ///< NOEXCEPT / DELETED only
    bool is_operator = false;
    bool is_variadic = false;         ///< C-style `...`; only the fixed parameters are listed
    bool has_canonical_name = false;  ///< false for internal linkage: invokable, not registered

    // ── Phase 9: templates ──
    bool is_template_specialization = false; ///< `function_of<id<int>>()` is one
    const void* template_arguments = nullptr; ///< const TemplateArgument[]
    std::size_t template_argument_count = 0;

    InvokeFn invoke = nullptr;        ///< self is ignored; nullptr when the call cannot be made
    InvokeRefFn invoke_ref = nullptr; ///< reference-returning functions only

    const void* annotations = nullptr; ///< const Annotation[]
    std::size_t annotation_count = 0;
    const void* parameter_annotations = nullptr; ///< const Annotation* const[] , one span per parameter
    const std::size_t* parameter_annotation_counts = nullptr;
};

/**
 * @brief Runtime description of one constructor
 */
struct CtorDesc
{
    std::string_view signature;
    const TypeDescriptor* declaring_class = nullptr;
    const Class* const* parameter_types = nullptr;
    const TypeDescriptor* const* parameter_decays = nullptr; ///< cv/ref-normalized parameter types
    std::size_t parameter_count = 0;
    std::uint32_t modifiers = 0;
    bool is_copy = false; ///< copy constructor
    bool is_move = false; ///< move constructor

    CtorFn new_instance = nullptr;
    PlacementCtorFn construct_at = nullptr;
    DestroyFn destroy_at = nullptr;

    const void* annotations = nullptr; ///< const Annotation[] on the constructor itself
    std::size_t annotation_count = 0;
    const void* parameter_annotations = nullptr; ///< const Annotation* const[] , one span per parameter
    const std::size_t* parameter_annotation_counts = nullptr;
};

/**
 * @brief Runtime description of one enumerator
 */
struct EnumDesc
{
    std::string_view name;
    const TypeDescriptor* declaring_enum = nullptr;
    std::int64_t value = 0;              ///< signed view of the underlying value
    std::uint64_t uvalue = 0;            ///< unsigned (bit-pattern) view of the underlying value
    bool is_unsigned = false;            ///< underlying type is unsigned

    const void* annotations = nullptr; ///< const Annotation[]
    std::size_t annotation_count = 0;
};

/**
 * @brief Runtime description of one template argument (Phase 9)
 *
 * One `kind` field selects which of the payload fields is meaningful:
 *  - Type     -> `type` (the argument type itself)
 *  - Value    -> `type` (the argument's declared type, cv stripped) + `value`
 *                (nullptr when the value cannot be extracted on this compiler,
 *                e.g. a pointer or member-pointer non-type argument)
 *  - Template -> `primary_template` (the declaration-level descriptor)
 *  - Pack     -> `pack` / `pack_count` (never produced by GCC 16.2)
 */
struct TemplateArgDesc
{
    TemplateArgumentKind kind = TemplateArgumentKind::Type;
    std::string_view spelling; ///< the compiler's display string: "int" / "3" / "std::vector"

    const TypeDescriptor* type = nullptr;             ///< Kind::Type / Kind::Value
    const TypeDescriptor* primary_template = nullptr; ///< Kind::Template
    const void* value = nullptr;                      ///< Kind::Value: static copy, nullptr when unreadable
    const void* pack = nullptr;                       ///< Kind::Pack: const TemplateArgument[]
    std::size_t pack_count = 0;
};

/**
 * @brief Runtime description of a value whose type is a function pointer or a
 *        member function pointer (api_design §2.3)
 *
 * `invoke` / `invoke_ref` take the pointer *value* (a pointer to the bytes of the
 * `Object`) plus the receiver, so the same pair of thunks serves every value of
 * that type.
 */
struct CallableDesc
{
    const TypeDescriptor* type = nullptr;                 ///< the pointer type itself, e.g. `void (*)(int)`
    const TypeDescriptor* function_type = nullptr;        ///< the pointee function type, e.g. `void (int)`
    const TypeDescriptor* declaring_class = nullptr;      ///< member pointers only: `C` in `R (C::*)(...)`
    bool is_member_function_pointer = false;
    bool returns_reference = false;

    CallableInvokeFn invoke = nullptr;         ///< nullptr when the result cannot be boxed
    CallableInvokeRefFn invoke_ref = nullptr;  ///< reference results only
};

/**
 * @brief Complete runtime description of a type
 *
 * The descriptor is a plain, constexpr-constructible aggregate. One instance
 * exists per reflected type; runtime handles (`Class`, `Field`, ...) only point
 * into it. Member arrays are type-erased (`const void*`) because the handle
 * types live in higher module partitions.
 */
struct TypeDescriptor
{
    std::string_view name;           ///< simple name (Java getSimpleName)
    std::string_view qualified_name; ///< qualified name (Java getName); the compiler's spelling
    std::string_view normalized_name; ///< normalize_type_name(qualified_name); also a registry key
    bool has_canonical_name = true;

    TypeKind kind = TypeKind::Class;
    std::uint32_t modifiers = 0;    ///< class-level Modifier bits
    std::size_t size = 0;
    std::size_t alignment = 0;

    bool is_polymorphic = false;
    bool is_abstract = false;
    bool is_final = false;
    bool is_aggregate = false;
    bool is_empty = false;
    bool is_scoped_enum = false;

    const Class* self = nullptr;    ///< the Class handle wrapping this descriptor

    ReleaseFn release = nullptr;    ///< owning destruction (delete)
    DestroyFn destroy = nullptr;    ///< in-place destruction
    CloneFn   clone = nullptr;      ///< nullptr when the type is not copy-constructible

    const void* fields = nullptr;   ///< const Field[] (public members, own class)
    std::size_t field_count = 0;
    const void* methods = nullptr;  ///< const Method[] (public members, own class)
    std::size_t method_count = 0;

    const void* declared_fields = nullptr;  ///< const Field[] (all members, incl. private/protected)
    std::size_t declared_field_count = 0;
    const void* declared_methods = nullptr; ///< const Method[] (all members, incl. private/protected)
    std::size_t declared_method_count = 0;

    const void* ctors = nullptr;            ///< const Constructor[] (public, own class)
    std::size_t ctor_count = 0;
    const void* declared_ctors = nullptr;   ///< const Constructor[] (all, incl. private/protected)
    std::size_t declared_ctor_count = 0;

    const void* bases = nullptr;
    std::size_t base_count = 0;
    const void* enumerators = nullptr;
    std::size_t enumerator_count = 0;

    const void* annotations = nullptr; ///< const Annotation[] on the type itself
    std::size_t annotation_count = 0;

    // ── decomposition / qualification (Phase 5) ──
    // `class_of<T>()` reflects the exact type (cv and references preserved), so
    // the derived types below are materialized alongside every type.
    const TypeDescriptor* decayed = nullptr;        ///< remove_cvref type (value packaging / matching)
    const TypeDescriptor* unqualified = nullptr;    ///< top-level-cv-stripped type (set only when cv-qualified)
    const TypeDescriptor* pointee = nullptr;        ///< T* -> T
    const TypeDescriptor* element = nullptr;        ///< T[N] -> T
    const TypeDescriptor* pointee_class = nullptr;  ///< M C::* -> C
    const TypeDescriptor* referenced = nullptr;     ///< T& / T&& -> T
    const TypeDescriptor* underlying = nullptr;     ///< enum -> underlying integer type
    const TypeDescriptor* dynamic_array = nullptr;  ///< element E -> the dynamic E[] view

    bool is_const_qualified = false;
    bool is_volatile_qualified = false;
    RefQualifier reference_kind = RefQualifier::None;

    std::size_t array_extent = 0;       ///< T[N] -> N (0 for T[])
    std::size_t array_rank = 0;         ///< number of array dimensions
    bool is_dynamic_array = false;      ///< produced by Array::new_instance
    std::size_t array_data_offset = 0;  ///< dynamic array: bytes before element 0

    // ── enum support ──
    bool enum_is_unsigned = false;
    EnumReadFn enum_read = nullptr;
    EnumMakeFn enum_make = nullptr;
    EnumBoxFn  enum_box = nullptr;      ///< boxes an enumerator's value as the underlying type

    // ── element capabilities (Array) ──
    CopyAssignFn copy_assign = nullptr; ///< nullptr when not copy-assignable
    NewArrayFn   new_array = nullptr;   ///< nullptr when not default-constructible

    // ── structure group (Phase 9) ──
    const TypeDescriptor* primary_template = nullptr; ///< class template primary (kind == Template), when a specialization
    const void* template_arguments = nullptr;         ///< const TemplateArgument[]
    std::size_t template_argument_count = 0;

    const TypeDescriptor* function_return = nullptr;  ///< function types only
    const Class* const*   function_parameters = nullptr; ///< function types only (declared parameter types)
    const TypeDescriptor* const* function_parameter_decays = nullptr; ///< cv/ref-normalized parameter types
    std::size_t function_parameter_count = 0;
    bool function_is_variadic = false; ///< C-style `...` (fixed parameters are still listed)
    bool function_is_noexcept = false;

    const Class* const* nested_types = nullptr; ///< const Class* const[]: classes / unions / enums declared inside
    std::size_t nested_type_count = 0;
    const TypeDescriptor* enclosing_class = nullptr; ///< the class this type is declared in, if any

    const void* aggregate_initializer = nullptr; ///< const Constructor* (pseudo-constructor), aggregate types only

    // ── capability group (Phase 9) ──
    std::uint32_t capabilities = 0; ///< Capability bits; 0 for non-class types

    const CallableDesc* callable = nullptr; ///< function pointer / member function pointer types only
};

// ─────────────────────────────────────────────────────────────────────────────
// Type-name normalization
//
// `qualified_name` is the compiler's own spelling (`long long unsigned int`,
// `int [3]`, ...), which nobody types. normalize_type_name maps it, and any
// user-written spelling of the same type, to one library-defined form:
//   - `{aka ...}` annotations are dropped;
//   - whitespace is canonical: one space only where two words meet, after
//     `*` `&` `,` and after `>` `)` before a word (`const int* const`, `int[3]`);
//   - fundamental types use the short spelling: `unsigned long long`, `long`,
//     `short`, `unsigned int`, `signed char`, `long double`, ...;
//   - the two standard string spellings are `std::string` / `std::string_view`.
// It runs at compile time (descriptor) and at run time (for_name queries), so
// both sides always agree.
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] constexpr auto is_name_word_char(char c) noexcept -> bool
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

[[nodiscard]] constexpr auto is_fundamental_keyword(std::string_view word) noexcept -> bool
{
    return word == "unsigned" || word == "signed" || word == "short" || word == "long"
        || word == "int" || word == "char" || word == "double";
}

/// The canonical spelling of a run of fundamental-type keywords (any order).
[[nodiscard]] constexpr auto canonical_fundamental(std::span<const std::string_view> words) -> std::string
{
    int longs = 0;
    int shorts = 0;
    int unsigneds = 0;
    int signeds = 0;
    int ints = 0;
    int chars = 0;
    int doubles = 0;
    for (std::string_view word : words)
    {
        if (word == "long")          { ++longs; }
        else if (word == "short")    { ++shorts; }
        else if (word == "unsigned") { ++unsigneds; }
        else if (word == "signed")   { ++signeds; }
        else if (word == "int")      { ++ints; }
        else if (word == "char")     { ++chars; }
        else if (word == "double")   { ++doubles; }
    }

    const bool is_short = shorts == 1;
    const bool is_unsigned = unsigneds == 1;
    const bool is_signed = signeds == 1;
    const bool is_char = chars == 1;
    const bool is_double = doubles == 1;

    // Not a spelling of any fundamental type (`long long long`, `short long`, ...):
    // keep it as written rather than merging it into a real type's name.
    const bool valid = shorts <= 1 && unsigneds <= 1 && signeds <= 1 && ints <= 1 && chars <= 1 && doubles <= 1
                    && !(unsigneds == 1 && signeds == 1)
                    && !(shorts == 1 && longs > 0)
                    && !(chars == 1 && (longs > 0 || shorts > 0 || ints > 0 || doubles > 0))
                    && !(doubles == 1 && (longs > 1 || shorts > 0 || ints > 0 || unsigneds > 0 || signeds > 0))
                    && longs <= 2;
    if (!valid)
    {
        std::string verbatim;
        for (std::string_view word : words)
        {
            verbatim += verbatim.empty() ? "" : " ";
            verbatim += word;
        }

        return verbatim;
    }

    if (is_char)
    {
        return is_unsigned ? "unsigned char" : is_signed ? "signed char" : "char";
    }

    if (is_double)
    {
        return longs > 0 ? "long double" : "double";
    }

    std::string result = is_unsigned ? "unsigned " : "";
    result += is_short ? "short" : longs >= 2 ? "long long" : longs == 1 ? "long" : "int";
    return result;
}

constexpr void replace_all(std::string& text, std::string_view from, std::string_view to)
{
    std::size_t at = 0;
    while ((at = text.find(from, at)) != std::string::npos)
    {
        text.replace(at, from.size(), to);
        at += to.size();
    }
}

[[nodiscard]] constexpr auto normalize_type_name(std::string_view text) -> std::string
{
    // 1. tokens: words and single punctuation characters; `{aka ...}` dropped.
    std::vector<std::string_view> tokens;
    for (std::size_t i = 0; i < text.size();)
    {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            ++i;
        }
        else if (c == '{' && text.substr(i + 1).starts_with("aka "))
        {
            std::size_t depth = 0;
            for (; i < text.size(); ++i)
            {
                if (text[i] == '{')      { ++depth; }
                else if (text[i] == '}') { if (--depth == 0) { ++i; break; } }
            }
        }
        else if (is_name_word_char(c))
        {
            std::size_t end = i;
            while (end < text.size() && is_name_word_char(text[end]))
            {
                ++end;
            }
            tokens.push_back(text.substr(i, end - i));
            i = end;
        }
        else
        {
            tokens.push_back(text.substr(i, 1));
            ++i;
        }
    }

    // 2. fold each run of fundamental keywords into its canonical spelling.
    std::vector<std::string> words;
    for (std::size_t i = 0; i < tokens.size();)
    {
        if (is_fundamental_keyword(tokens[i]))
        {
            std::size_t end = i;
            while (end < tokens.size() && is_fundamental_keyword(tokens[end]))
            {
                ++end;
            }
            words.push_back(canonical_fundamental(std::span<const std::string_view>(tokens.data() + i, end - i)));
            i = end;
        }
        else
        {
            words.emplace_back(tokens[i]);
            ++i;
        }
    }

    // 3. join with canonical spacing.
    std::string result;
    for (std::size_t i = 0; i < words.size(); ++i)
    {
        if (i > 0)
        {
            const std::string& previous = words[i - 1];
            const bool next_is_word = is_name_word_char(words[i].front());
            const bool after_word = is_name_word_char(previous.back());
            const bool after_separator = previous == "*" || previous == "&" || previous == ","
                                      || previous == ">" || previous == ")";
            if (next_is_word && (after_word || after_separator))
            {
                result += ' ';
            }
        }

        result += words[i];
    }

    // 4. the standard string spellings.
    replace_all(result, "std::__cxx11::basic_string<char>", "std::string");
    replace_all(result, "std::basic_string_view<char>", "std::string_view");
    return result;
}

} // namespace speculo::detail
