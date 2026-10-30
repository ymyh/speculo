export module speculo:modifier;

import std;

export namespace speculo
{

/**
 * @brief Java-style modifier bitmask, shared by classes, fields, methods and
 *        constructors
 * @note Value semantics; combine with `operator|` and test with `is_xxx()`.
 *       The access bits (PUBLIC / PROTECTED / PRIVATE) are mutually exclusive.
 */
class Modifier
{
public:
    // ── Java counterparts ──
    static constexpr std::uint32_t PUBLIC    = 1u << 0;
    static constexpr std::uint32_t PROTECTED = 1u << 1;
    static constexpr std::uint32_t PRIVATE   = 1u << 2;
    static constexpr std::uint32_t STATIC    = 1u << 3;
    static constexpr std::uint32_t ABSTRACT  = 1u << 4; ///< class: has pure virtuals; method: pure virtual
    static constexpr std::uint32_t INTERFACE = 1u << 5; ///< no data members and all pure virtual
    static constexpr std::uint32_t FINAL     = 1u << 6;

    // ── C++ extensions ──
    static constexpr std::uint32_t CONST     = 1u << 8;
    static constexpr std::uint32_t VOLATILE  = 1u << 9;
    static constexpr std::uint32_t VIRTUAL   = 1u << 10;
    /// Reserved, never set: GCC 16.2's <meta> has no is_inline / is_constexpr /
    /// is_consteval, so the library cannot tell. There is deliberately no
    /// is_inline() / is_constexpr() / is_consteval() query, so "not set" is never
    /// mistaken for "known to be false". The bit positions stay stable.
    static constexpr std::uint32_t INLINE    = 1u << 11;
    static constexpr std::uint32_t EXPLICIT  = 1u << 12;
    static constexpr std::uint32_t NOEXCEPT  = 1u << 13;
    static constexpr std::uint32_t CONSTEXPR = 1u << 14; ///< reserved, see INLINE
    static constexpr std::uint32_t CONSTEVAL = 1u << 15; ///< reserved, see INLINE
    static constexpr std::uint32_t MUTABLE   = 1u << 16;
    static constexpr std::uint32_t BITFIELD  = 1u << 17;
    static constexpr std::uint32_t REFERENCE = 1u << 18;
    static constexpr std::uint32_t DEFAULTED = 1u << 19;
    static constexpr std::uint32_t DELETED   = 1u << 20;
    static constexpr std::uint32_t OVERRIDE  = 1u << 21;

    constexpr Modifier() noexcept = default;

    constexpr explicit Modifier(std::uint32_t bits) noexcept
        : m_bits(bits)
    {
    }

    [[nodiscard]] constexpr auto bits() const noexcept -> std::uint32_t { return m_bits; }

    [[nodiscard]] constexpr auto is_public() const noexcept -> bool { return has(PUBLIC); }
    [[nodiscard]] constexpr auto is_protected() const noexcept -> bool { return has(PROTECTED); }
    [[nodiscard]] constexpr auto is_private() const noexcept -> bool { return has(PRIVATE); }
    [[nodiscard]] constexpr auto is_static() const noexcept -> bool { return has(STATIC); }
    [[nodiscard]] constexpr auto is_abstract() const noexcept -> bool { return has(ABSTRACT); }
    [[nodiscard]] constexpr auto is_interface() const noexcept -> bool { return has(INTERFACE); }
    [[nodiscard]] constexpr auto is_final() const noexcept -> bool { return has(FINAL); }
    [[nodiscard]] constexpr auto is_const() const noexcept -> bool { return has(CONST); }
    [[nodiscard]] constexpr auto is_volatile() const noexcept -> bool { return has(VOLATILE); }
    [[nodiscard]] constexpr auto is_virtual() const noexcept -> bool { return has(VIRTUAL); }
    [[nodiscard]] constexpr auto is_explicit() const noexcept -> bool { return has(EXPLICIT); }
    [[nodiscard]] constexpr auto is_noexcept() const noexcept -> bool { return has(NOEXCEPT); }
    [[nodiscard]] constexpr auto is_mutable() const noexcept -> bool { return has(MUTABLE); }
    [[nodiscard]] constexpr auto is_bitfield() const noexcept -> bool { return has(BITFIELD); }
    [[nodiscard]] constexpr auto is_reference() const noexcept -> bool { return has(REFERENCE); }
    [[nodiscard]] constexpr auto is_defaulted() const noexcept -> bool { return has(DEFAULTED); }
    [[nodiscard]] constexpr auto is_deleted() const noexcept -> bool { return has(DELETED); }
    [[nodiscard]] constexpr auto is_override() const noexcept -> bool { return has(OVERRIDE); }

    /**
     * @brief Space-separated list of active modifier names (Java style)
     * @return e.g. "public static"; empty string when no bit is set
     */
    [[nodiscard]] constexpr auto to_string() const -> std::string
    {
        std::string result;
        const auto append = [&result](std::string_view word)
        {
            if (!result.empty())
            {
                result.push_back(' ');
            }
            result.append(word);
        };

        if (is_public())    { append("public"); }
        if (is_protected()) { append("protected"); }
        if (is_private())   { append("private"); }
        if (is_static())    { append("static"); }
        if (is_abstract())  { append("abstract"); }
        if (is_interface()) { append("interface"); }
        if (is_final())     { append("final"); }
        if (is_const())     { append("const"); }
        if (is_volatile())  { append("volatile"); }
        if (is_virtual())   { append("virtual"); }
        if (is_explicit())  { append("explicit"); }
        if (is_noexcept())  { append("noexcept"); }
        if (is_mutable())   { append("mutable"); }
        if (is_bitfield())  { append("bitfield"); }
        if (is_reference()) { append("reference"); }
        if (is_defaulted()) { append("defaulted"); }
        if (is_deleted())   { append("deleted"); }
        if (is_override())  { append("override"); }

        return result;
    }

    [[nodiscard]] friend constexpr auto operator|(Modifier lhs, Modifier rhs) noexcept -> Modifier
    {
        return Modifier(lhs.m_bits | rhs.m_bits);
    }

    [[nodiscard]] friend constexpr auto operator&(Modifier lhs, Modifier rhs) noexcept -> Modifier
    {
        return Modifier(lhs.m_bits & rhs.m_bits);
    }

    [[nodiscard]] friend constexpr auto operator==(Modifier lhs, Modifier rhs) noexcept -> bool
    {
        return lhs.m_bits == rhs.m_bits;
    }

private:
    [[nodiscard]] constexpr auto has(std::uint32_t bit) const noexcept -> bool
    {
        return (m_bits & bit) != 0;
    }

    std::uint32_t m_bits = 0;
};

/**
 * @brief ref-qualifier of a member function
 */
enum class RefQualifier
{
    None,   ///< no ref-qualifier
    Lvalue, ///< `&`
    Rvalue, ///< `&&`
};

/**
 * @brief Coarse classification of a reflected type
 * @note Since Phase 5 `class_of<T>()` reflects the *exact* type: cv-qualifiers
 *       and references are not normalized, so `class_of<T&>`, `class_of<T&&>`
 *       and `class_of<T>` are distinct handles and reference types classify as
 *       TypeKind::Reference. "a member/return type is a reference" is still also
 *       exposed via Modifier::REFERENCE (both views coexist).
 */
enum class TypeKind
{
    Void,
    Fundamental,
    Pointer,
    MemberPointer,
    Array,
    Enum,
    Union,
    Class,
    Function,
    Reference, ///< T& / T&& (distinct from the referred object type)
    /// A class/function template *primary* (declaration-level: no size, no members).
    /// Instances are reached through Class::get_primary_template().
    Template,
};

/**
 * @brief What one template argument is (Class::get_template_arguments)
 * @note Parameter packs are flattened by the reflection layer into independent
 *       arguments, so Pack is never produced by GCC 16.2; the enumerator is kept
 *       for forward compatibility (see @.agents/api_design.md §1.2).
 */
enum class TemplateArgumentKind
{
    Type,     ///< a type argument: std::vector<int> -> int
    Value,    ///< a non-type argument: std::array<int, 3> -> 3
    Template, ///< a template-template argument: Pack3<int, std::vector, 7> -> std::vector
    Pack,     ///< a parameter pack (never produced by GCC 16.2)
};

} // namespace speculo
