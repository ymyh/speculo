export module speculo:registry;

import std;
import crab_cpp;
import :error;
import :object;
import :function;

namespace speculo::detail
{

/**
 * @brief The global name table: qualified name -> canonical Class handle
 *
 * A function-local magic static, so it is constructed on first use (thread-safe
 * since C++11) and destroyed at program exit. The class table and the function
 * table are the only mutable global state in speculo (see @.agents/design.md §8);
 * all descriptor tables remain read-only. A shared mutex allows concurrent
 * readers while a writer registers a class.
 */
struct ClassRegistry
{
    std::shared_mutex mutex;
    std::unordered_map<std::string_view, const Class*> classes;

    /// Every registered class, once each, in registration order. The relation
    /// queries (get_overrider_methods) have to look at all of them, so the table
    /// has to remember the canonical handles itself (the name map cannot: it
    /// holds two keys per type).
    std::vector<const Class*> all;

    /**
     * @brief Reverse edge index (api_design §15.2): base -> derived classes
     *
     * Keyed by the base's normalized name rather than by its descriptor pointer,
     * so the index follows the same identity rule as `Class::operator==`: two
     * copies of the same class (two DLLs, two static copies of the library) map
     * to one key. The values are canonical handles; the order is registration
     * order, and a class appears once per *direct* base edge.
     *
     * The index is maintained while the class is registered, which is why
     * get_derived_classes() only sees registered types (api_design §15.2).
     */
    std::unordered_map<std::string_view, std::vector<const Class*>> derived;
};

/**
 * @brief Signature -> Function. Same lifetime rules as ClassRegistry.
 * The key is Function::get_signature(), which lives in the read-only descriptor.
 */
struct FunctionRegistry
{
    std::shared_mutex mutex;
    std::unordered_map<std::string_view, const Function*> functions;
};

/// The one registry instance (magic static: built on first access).
[[nodiscard]] inline auto class_registry() -> ClassRegistry&
{
    static ClassRegistry registry;
    return registry;
}

/**
 * @brief Process-wide type ids (api_design §13)
 *
 * `ids` maps the canonical (normalized) name to its integer id; `occupants` is
 * the reverse map used by the collision chain. Both are keyed by views into the
 * read-only descriptors, so they never own storage.
 */
struct TypeIdRegistry
{
    std::shared_mutex mutex;
    std::unordered_map<std::string_view, std::uint64_t> ids;
    std::unordered_map<std::uint64_t, std::string_view> occupants;
};

[[nodiscard]] inline auto type_id_registry() -> TypeIdRegistry&
{
    static TypeIdRegistry registry;
    return registry;
}

/// FNV-1a (64 bit): a fixed, allocation-free hash that every copy of the
/// library computes identically, unlike std::hash<std::string_view>.
[[nodiscard]] constexpr auto fnv1a_64(std::string_view text) noexcept -> std::uint64_t
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const char c : text)
    {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ull;
    }

    return hash;
}

[[nodiscard]] inline auto function_registry() -> FunctionRegistry&
{
    static FunctionRegistry registry;
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
    const bool first_registration = registry.classes.try_emplace(cls.get_qualified_name(), canonical).second;
    registry.classes.try_emplace(cls.get_normalized_name(), canonical);

    // Edge maintenance happens once per type: the qualified-name key is only new
    // on the first registration, so a repeated registration (every class_of<T>()
    // call, SPECULO_REGISTER in another TU) cannot duplicate the edges.
    if (!first_registration)
    {
        return;
    }

    registry.all.push_back(canonical);

    // Reverse edges. A base with no canonical name (anonymous namespace, function
    // local) has no name to key on, and its derived classes could never be found
    // by get_derived_classes() anyway.
    for (const BaseClass& base : cls.get_bases())
    {
        if (base.type == nullptr || !base.type->has_canonical_name())
        {
            continue;
        }

        std::vector<const Class*>& derived = registry.derived[base.type->get_normalized_name()];
        if (std::ranges::find(derived, canonical) == derived.end())
        {
            derived.push_back(canonical);
        }
    }
}

/**
 * @brief Registers a free function under its signature (definition of the
 *        declaration in speculo:function)
 * @note Idempotent. Internal-linkage functions are skipped: they can be invoked
 *       through function_of, but the signature is not a stable cross-TU key.
 */
void register_function(const Function& fn)
{
    if (!fn.has_canonical_name())
    {
        return;
    }

    detail::FunctionRegistry& registry = detail::function_registry();
    const std::unique_lock<std::shared_mutex> lock(registry.mutex);
    registry.functions.try_emplace(fn.get_signature(), std::addressof(fn));
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

/**
 * @brief Whether two methods have the same overriding key
 *
 * The C++ rule for an override is identity of the parameter-type-list plus the
 * same name and the same cv / ref qualification (the return type is not part of
 * the check; covariant returns are out of scope, api_design §17). Parameter
 * types are compared with `Class::operator==` rather than by descriptor pointer
 * so that a class materialized in two copies of the library still matches; they
 * are compared **exactly**, not after decay: `void f(int)` does not override
 * `void f(const int&)`.
 */
[[nodiscard]] auto same_override_key(const Method& a, const Method& b) -> bool
{
    if (a.get_name() != b.get_name() || a.is_const() != b.is_const() || a.is_volatile() != b.is_volatile()
        || a.get_ref_qualifier() != b.get_ref_qualifier())
    {
        return false;
    }

    const std::span<const Class* const> left = a.get_parameter_types();
    const std::span<const Class* const> right = b.get_parameter_types();
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t i = 0; i < left.size(); ++i)
    {
        if (*left[i] != *right[i])
        {
            return false;
        }
    }

    return true;
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

/**
 * @brief Looks up a free function by the exact get_signature() string
 */
auto Function::for_name(std::string_view signature) -> Option<const Function&>
{
    detail::FunctionRegistry& registry = detail::function_registry();
    const std::shared_lock<std::shared_mutex> lock(registry.mutex);

    const auto found = registry.functions.find(signature);
    if (found == registry.functions.end())
    {
        return None{};
    }

    return *found->second;
}

auto Function::registered_signatures() -> std::vector<std::string_view>
{
    detail::FunctionRegistry& registry = detail::function_registry();
    const std::shared_lock<std::shared_mutex> lock(registry.mutex);

    std::vector<std::string_view> signatures;
    signatures.reserve(registry.functions.size());
    for (const auto& entry : registry.functions)
    {
        signatures.push_back(entry.first);
    }

    std::ranges::sort(signatures);
    return signatures;
}

/**
 * @brief Process-wide integer identity of a type (definition of Class::get_type_id)
 *
 * Canonical names are assigned an id on first sight: FNV-1a of the normalized
 * name, linear probing forward while the slot belongs to a *different* name
 * (the collision chain). A 64-bit collision is astronomically unlikely; when one
 * happens the ids still differ, but which name gets the base id depends on the
 * registration order (documented limitation, api_design §13).
 *
 * Types without a canonical name never enter the table: their id is the plain
 * hash of the display name, stable within the process but not usable to unify
 * two anonymous copies of a type.
 */
auto Class::get_type_id() const -> std::uint64_t
{
    const detail::TypeDescriptor* const desc = m_desc.get();
    if (desc == nullptr)
    {
        return 0; // an empty handle; Class::operator== treats two of them as equal
    }

    if (!desc->has_canonical_name)
    {
        return detail::fnv1a_64(desc->qualified_name);
    }

    detail::TypeIdRegistry& registry = detail::type_id_registry();

    {
        const std::shared_lock<std::shared_mutex> lock(registry.mutex);
        const auto found = registry.ids.find(desc->normalized_name);
        if (found != registry.ids.end())
        {
            return found->second;
        }
    }

    const std::unique_lock<std::shared_mutex> lock(registry.mutex);
    const auto found = registry.ids.find(desc->normalized_name);
    if (found != registry.ids.end())
    {
        return found->second;
    }

    std::uint64_t id = detail::fnv1a_64(desc->normalized_name);
    while (true)
    {
        const auto occupant = registry.occupants.find(id);
        if (occupant == registry.occupants.end())
        {
            break;
        }

        if (occupant->second == desc->normalized_name)
        {
            registry.ids.try_emplace(desc->normalized_name, id);
            return id;
        }

        ++id;
    }

    registry.occupants.emplace(id, desc->normalized_name);
    registry.ids.emplace(desc->normalized_name, id);
    return id;
}

// ── Relation graph (api_design §15) ──

/**
 * @brief The direct derived classes that are registered (definition of
 *        Class::get_derived_classes)
 */
auto Class::get_derived_classes() const -> std::vector<const Class*>
{
    detail::ClassRegistry& registry = detail::class_registry();
    const std::shared_lock<std::shared_mutex> lock(registry.mutex);

    const auto found = registry.derived.find(get_normalized_name());
    if (found == registry.derived.end())
    {
        return {};
    }

    return found->second;
}

/**
 * @brief Every registered descendant (definition of Class::get_all_derived_classes)
 *
 * Breadth first, so the result is level order (nearest derivation first) and
 * each class appears once even with a diamond in the middle.
 */
auto Class::get_all_derived_classes() const -> std::vector<const Class*>
{
    detail::ClassRegistry& registry = detail::class_registry();
    const std::shared_lock<std::shared_mutex> lock(registry.mutex);

    std::vector<const Class*> result;
    std::vector<std::string_view> seen{get_normalized_name()};
    std::vector<const Class*> level{detail::Access::descriptor(*this)->self};
    while (!level.empty())
    {
        std::vector<const Class*> next;
        for (const Class* current : level)
        {
            if (current == nullptr)
            {
                continue;
            }

            const auto found = registry.derived.find(current->get_normalized_name());
            if (found == registry.derived.end())
            {
                continue;
            }

            for (const Class* child : found->second)
            {
                if (child == nullptr || std::ranges::find(seen, child->get_normalized_name()) != seen.end())
                {
                    continue;
                }

                seen.push_back(child->get_normalized_name());
                result.push_back(child);
                next.push_back(child);
            }
        }

        level = std::move(next);
    }

    return result;
}

/**
 * @brief The upward inheritance path (definition of Class::get_conversion_path)
 *
 * A depth-first walk over the public base edges (`BaseClass::cast != nullptr`,
 * the same gate cast_to() uses), collecting every distinct path. Two paths that
 * enter the same virtual base subobject are one path -- a virtual base is shared
 * by the whole object, which is exactly why cast_to() compares addresses; here
 * the same statement is made statically by looking at whether the last hop is a
 * virtual base edge.
 */
auto Class::get_conversion_path(const Class& base) const -> Outcome<std::vector<const Class*>>
{
    // Empty handles are a programmer error, not a graph answer (see
    // Class::is_derived_from): get_qualified_name() panics through CheckedDesc.
    (void)get_qualified_name();
    (void)base.get_qualified_name();

    struct Path
    {
        std::vector<const Class*> types; ///< this ... base, inclusive
        bool enters_virtually = false;   ///< the last hop enters a virtual base
    };

    std::vector<Path> found;
    std::vector<const Class*> chain;

    auto walk = [&](auto&& self, const Class& from, bool enters_virtually) -> void
    {
        chain.push_back(std::addressof(from));
        if (from == base)
        {
            found.push_back(Path{chain, enters_virtually});
            chain.pop_back();
            return;
        }

        for (const BaseClass& edge : from.get_bases())
        {
            if (edge.cast != nullptr && edge.type != nullptr)
            {
                self(self, *edge.type, edge.is_virtual());
            }
        }

        chain.pop_back();
    };

    walk(walk, *this, false);

    if (found.empty())
    {
        if (reaches(*this, base, false))
        {
            return Error{ErrorKind::NotSupported,
                         std::format("Class::get_conversion_path({} -> {}): the target is a base only through "
                                     "protected or private inheritance",
                                     get_qualified_name(), base.get_qualified_name())};
        }

        return Error{ErrorKind::TypeMismatch,
                     std::format("Class::get_conversion_path({} -> {}): '{}' is not a base of '{}'",
                                 get_qualified_name(), base.get_qualified_name(), base.get_qualified_name(),
                                 get_qualified_name())};
    }

    const bool shared_virtual_base =
        std::ranges::all_of(found, [](const Path& path) { return path.enters_virtually; });
    if (found.size() > 1 && !shared_virtual_base)
    {
        return Error{ErrorKind::Ambiguous,
                     std::format("Class::get_conversion_path({} -> {}): several inheritance paths reach the target",
                                 get_qualified_name(), base.get_qualified_name())};
    }

    // All virtual paths land on the same subobject; report the shortest one.
    const Path* shortest = std::addressof(*std::ranges::min_element(
        found, {}, [](const Path& path) { return path.types.size(); }));

    std::vector<const Class*> path;
    path.reserve(shortest->types.size());
    for (const Class* node : shortest->types)
    {
        // Report the canonical handle that lives in the descriptor, never a
        // pointer to a caller's temporary (`class_of<D>().get_conversion_path()`
        // would dangle otherwise).
        const detail::TypeDescriptor* desc = detail::Access::descriptor(*node);
        path.push_back(desc != nullptr && desc->self != nullptr ? desc->self : node);
    }

    return path;
}

/**
 * @brief The overridden base method (definition of Method::get_overridden_method)
 */
auto Method::get_overridden_method() const -> Option<Method>
{
    if (!is_virtual())
    {
        return None{};
    }

    // Breadth first over the declared bases: the nearest declaration wins, which
    // is the one C++ would bind a virtual call to.
    std::vector<const Class*> pending;
    const auto enqueue_bases = [&pending](const Class& cls)
    {
        for (const BaseClass& edge : cls.get_bases())
        {
            if (edge.type != nullptr)
            {
                pending.push_back(edge.type);
            }
        }
    };

    enqueue_bases(get_declaring_class());
    for (std::size_t i = 0; i < pending.size(); ++i)
    {
        const Class& candidate = *pending[i];
        for (const Method& method : candidate.get_declared_methods())
        {
            if (method.is_virtual() && detail::same_override_key(*this, method))
            {
                return method;
            }
        }

        enqueue_bases(candidate);
    }

    return None{};
}

/**
 * @brief Every registered override (definition of Method::get_overrider_methods)
 */
auto Method::get_overrider_methods() const -> std::vector<Method>
{
    std::vector<Method> result;
    if (!is_virtual())
    {
        return result;
    }

    const Class& declaring = get_declaring_class();

    std::vector<const Class*> registered;
    {
        detail::ClassRegistry& registry = detail::class_registry();
        const std::shared_lock<std::shared_mutex> lock(registry.mutex);
        registered = registry.all;
    }

    for (const Class* candidate : registered)
    {
        if (candidate == nullptr || *candidate == declaring || !declaring.is_base_of_ignoring_access(*candidate))
        {
            continue;
        }

        for (const Method& method : candidate->get_declared_methods())
        {
            if (method.is_virtual() && detail::same_override_key(method, *this))
            {
                result.push_back(method);
            }
        }
    }

    return result;
}

} // namespace speculo
