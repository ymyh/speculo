export module speculo:error;

import std;
import crab_cpp;

export namespace speculo
{

// crab_cpp is the error-handling vocabulary used across the whole speculo API.
// Re-export it so that consumers of `import speculo;` can write
// `speculo::Option<T>` / `speculo::Result<T, E>` without a second import.
using crab_cpp::None;
using crab_cpp::Option;
using crab_cpp::panic;
using crab_cpp::Result;

/**
 * @brief Category of a speculo error
 */
enum class ErrorKind
{
    NotFound,        ///< Name / signature lookup failed
    TypeMismatch,    ///< Argument or boxed value has the wrong type
    NullInstance,    ///< Operation on an empty ObjectRef / Object
    NotSupported,    ///< Capability missing (bitfield ref, non-copyable clone, ...)
    ReadOnly,        ///< Write to a const field / const object / reference member
    Ambiguous,       ///< Non-unique overload or inheritance path
    InvalidArgument, ///< Wrong argument count, index out of range, ...
};

/**
 * @brief Error value: an ErrorKind plus a human-readable context message
 */
struct Error
{
    ErrorKind   kind;
    std::string message;
};

/**
 * @brief Result carrying no value; `None` (std::monostate) means success
 */
using Status = Result<std::monostate, Error>;

/**
 * @brief Result carrying a value of type T
 */
template<typename T>
using Outcome = Result<T, Error>;

/**
 * @brief Diagnostic name of an ErrorKind
 * @param kind The error category
 * @return A stable, non-localized name (e.g. "TypeMismatch")
 */
[[nodiscard]] constexpr auto to_string(ErrorKind kind) -> std::string_view
{
    switch (kind)
    {
        case ErrorKind::NotFound:        return "NotFound";
        case ErrorKind::TypeMismatch:    return "TypeMismatch";
        case ErrorKind::NullInstance:    return "NullInstance";
        case ErrorKind::NotSupported:    return "NotSupported";
        case ErrorKind::ReadOnly:        return "ReadOnly";
        case ErrorKind::Ambiguous:       return "Ambiguous";
        case ErrorKind::InvalidArgument: return "InvalidArgument";
    }

    return "Unknown";
}

} // namespace speculo

/// `std::format("{}", kind)` -> "TypeMismatch"
template<>
struct std::formatter<speculo::ErrorKind> : std::formatter<std::string_view>
{
    auto format(speculo::ErrorKind kind, std::format_context& ctx) const
    {
        return std::formatter<std::string_view>::format(speculo::to_string(kind), ctx);
    }
};

/// `std::format("{}", error)` -> "TypeMismatch: Field::set(Point::x): the value has type ..."
template<>
struct std::formatter<speculo::Error> : std::formatter<std::string_view>
{
    auto format(const speculo::Error& error, std::format_context& ctx) const
    {
        const std::string text = std::format("{}: {}", error.kind, error.message);
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
