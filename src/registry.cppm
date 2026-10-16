export module speculo:registry;

import std;
import crab_cpp;
import :error;
import :object;

namespace speculo::detail
{

/**
 * @brief The global name table: qualified name -> canonical Class handle
 *
 * A function-local magic static, so it is constructed on first use (thread-safe
 * since C++11) and destroyed at program exit. It is the only mutable global
 * state in speculo (see @.agents/design.md §8 / §10); all descriptor tables
 * remain read-only. A shared mutex allows concurrent readers while a writer
 * registers a class.
 */
struct ClassRegistry
{
    std::shared_mutex mutex;
    std::unordered_map<std::string_view, const Class*> classes;
};

/// The one registry instance (magic static: built on first access).
[[nodiscard]] inline auto class_registry() -> ClassRegistry&
{
    static ClassRegistry registry;
    return registry;
}

} // namespace speculo::detail

export namespace speculo
{

/**
 * @brief Registers a class in the global name table under its qualified name
 * @param cls The class to register
 * @note Idempotent: the first registration of a name wins, later duplicates are
 *       ignored (matching @.agents/api_design.md §10). Classes without a
 *       canonical name (anonymous-namespace / function-local types) are skipped.
 * @note Thread-safe; class_of<T>() calls this automatically on first use.
 */
void register_class(const Class& cls)
{
    if (!cls.has_canonical_name())
    {
        return;
    }

    const Class* canonical = detail::Access::descriptor(cls)->self;
    if (canonical == nullptr)
    {
        return;
    }

    detail::ClassRegistry& registry = detail::class_registry();
    const std::unique_lock<std::shared_mutex> lock(registry.mutex);
    // Both spellings are keys: the compiler's (what get_qualified_name() reports)
    // and the library's (what users can write). First registration of a key wins.
    registry.classes.try_emplace(cls.get_qualified_name(), canonical);
    registry.classes.try_emplace(cls.get_normalized_name(), canonical);
}

} // namespace speculo

namespace speculo::detail
{

// Auto-registration hook declared in `speculo:object`; called once per type
// from class_of<T>(). It forwards to the public API so the registry stays the
// sole owner of the table.
void register_type(const Class& cls)
{
    register_class(cls);
}

} // namespace speculo::detail

export namespace speculo
{

/**
 * @brief Looks up a type by its qualified name (definition of Class::for_name)
 * @param qualified_name Either spelling of the name, or any spelling that
 *        normalizes to a registered one (see detail::normalize_type_name)
 * @return Some(class) when the name is registered, otherwise None
 */
auto Class::for_name(std::string_view qualified_name) -> Option<const Class&>
{
    detail::ClassRegistry& registry = detail::class_registry();
    const std::shared_lock<std::shared_mutex> lock(registry.mutex);

    // Exact spelling first: no allocation for the common case.
    auto found = registry.classes.find(qualified_name);
    if (found == registry.classes.end())
    {
        const std::string normalized = detail::normalize_type_name(qualified_name);
        found = registry.classes.find(std::string_view(normalized));
    }

    if (found == registry.classes.end())
    {
        return None{};
    }

    return *found->second;
}

/**
 * @brief Every registered name (both spellings of each type), sorted
 * @note Definition of Class::registered_names; for diagnostics.
 */
auto Class::registered_names() -> std::vector<std::string_view>
{
    detail::ClassRegistry& registry = detail::class_registry();
    const std::shared_lock<std::shared_mutex> lock(registry.mutex);

    std::vector<std::string_view> names;
    names.reserve(registry.classes.size());
    for (const auto& entry : registry.classes)
    {
        names.push_back(entry.first);
    }

    std::ranges::sort(names);
    return names;
}

} // namespace speculo
