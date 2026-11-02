module;

// The library version is decided by the build system, not by this file: CMake
// and xmake both define SPECULO_VERSION (as a quoted string) when compiling the
// module units, so the version lives in exactly one place per build system
// (CMakeLists.txt / xmake.lua). The fallback below only keeps a hand-rolled
// `g++ -freflection -c src/*.cppm` working; it must stay in sync with them.
#ifndef SPECULO_VERSION
#define SPECULO_VERSION "0.1.0"
#endif

export module speculo;

import std;
import crab_cpp;

export import :error;
export import :modifier;
export import :descriptor;
export import :meta;
export import :object;
export import :function;
export import :registry;
export import :compile;

export namespace speculo
{

/**
 * @brief The library version
 * @return A semantic-version string such as "0.1.0"
 * @note The string is the SPECULO_VERSION macro that the build system passes to
 *       the compiler, so it reports the version the module was built as (it is
 *       a constant of this module unit, not of the caller's translation unit).
 */
[[nodiscard]] auto version() -> std::string_view
{
    return SPECULO_VERSION;
}

} // namespace speculo
