add_rules("mode.debug", "mode.release")

-- NOTE: speculo requires GCC >= 16 — the only compiler that implements the
-- P2996 static reflection subset. On Windows xmake picks MSVC by default, so
-- configure the mingw toolchain once before building, e.g.:
--     xmake f -p mingw --mingw=<msys2>/ucrt64 -a x86_64
-- On Linux/macOS the GCC default toolchain is detected automatically.

-- Make dependency packages (crab_cpp, gtest) inherit the configured toolchain
-- (e.g. mingw/gcc) instead of falling back to the MSVC host toolchain.
set_policy("package.inherit_external_configs", true)

option("enable-test")
    set_default(true)
    set_showmenu(true)
    set_description("Build and run speculo tests")
option_end()

option("enable-example")
    set_default(true)
    set_showmenu(true)
    set_description("Build speculo examples")
option_end()

-- crab_cpp is a module-only C++ library and is not published in xmake-repo,
-- so we declare the package here (mirrors crab_cpp's README boilerplate). The
-- release tarball is the primary source; the git repository is kept as a
-- fallback mirror.
package("crab_cpp")

    set_kind("library", {moduleonly = true})
    set_homepage("https://github.com/ymyh/crab-cpp")
    set_description("Rust-like Option/Result and friends for modern C++")
    set_license("MIT")
    add_urls("https://github.com/ymyh/crab-cpp/archive/refs/tags/v$(version).tar.gz")
    add_urls("https://github.com/ymyh/crab-cpp.git", {alias = "git"})
    add_versions("0.4.0", "403e1b94c7a1d2133ba37272031b19facc752a60fecb833482152d3a17869885")
    add_versions("git:0.4.0", "v0.4.0")
    add_configs("enableString", {description = "Enable string module", default = false, type = "boolean"})
    add_configs("enableBacktrace", {description = "Enable backtrace", default = false, type = "boolean"})
    add_configs("enableTests", {description = "Run tests after installation", default = false, type = "boolean"})

    on_install(function (package)

        local configs = {}

        if package:config("enableString") then
            package:add("defines", "CRAB_CPP_ENABLE_STRING")
            table.insert(configs, "--enable-string=y")
        end

        if package:config("enableBacktrace") then
            package:add("defines", "CRAB_CPP_ENABLE_BACKTRACE")
            table.insert(configs, "--enable-backtrace=y")
        end

        if package:config("enableTests") then
            table.insert(configs, "--enable-test=y")
        end

        package:add("includedirs", "include")
        os.cp("include/crab_cpp/*.h", package:installdir("include", "crab_cpp"))

        import("package.tools.xmake").install(package, configs)
    end)

    on_test(function (package)

        if package:config("enableTests") then
            os.exec(path.join(package:installdir(), "bin", "crab_cpp_test"))
        end
    end)

package_end()

add_requires("crab_cpp")
add_requires("gtest", {configs = {main = true}})

-- speculo only targets GCC >= 16. `-freflection` enables P2996; `-fno-rtti`
-- and `-fno-exceptions` ban RTTI and exception throwing project-wide (errors go
-- through crab_cpp Option/Result). `force` is needed because xmake's flag probe
-- does not recognise the experimental `-freflection` option.
local function _add_speculo_flags(target)
    if target:has_tool("cc", "gcc") then
        target:add("cxflags", "-freflection", "-fno-rtti", "-fno-exceptions", {force = true})
    end
end

target("speculo")

    set_default(true)
    set_kind("moduleonly")
    set_languages("c++26")
    set_policy("build.c++.modules", true)

    add_files("src/*.cppm", {public = true})
    add_packages("crab_cpp")
    add_includedirs("include")

    on_config(_add_speculo_flags)

target("speculo_test")

    set_enabled(has_config("enable-test"))
    set_kind("binary")
    set_languages("c++26")
    set_policy("build.c++.modules", true)

    add_packages("crab_cpp", "gtest")
    add_files("src/*.cppm", "tests/*.cpp")
    add_includedirs("include")
    add_links("gtest_main")

    on_config(function (target)
        _add_speculo_flags(target)

        -- MinGW/GCC workarounds for libstdc++ `import std` (same as crab_cpp):
        --  - std::__open_terminal/std::__write_to_terminal live in libstdc++exp;
        --  - GCC PR116746 emits duplicate symbols for static locals from
        --    libstdc++ headers when using `import std` with std::print/format.
        if target:is_plat("windows", "mingw", "msys") and target:has_tool("cc", "gcc") then
            target:add("links", "stdc++exp")
            target:add("ldflags", "-Wl,--allow-multiple-definition")
        end
    end)

    add_tests("default")

-- Examples: one binary per examples/*.cpp. Like speculo_test, each target
-- compiles the module sources directly (xmake's `speculo` target is
-- `moduleonly` and is not linked/build on its own).
for _, example_src in ipairs(os.files("examples/*.cpp")) do

    target("example_" .. path.basename(example_src))

        set_enabled(has_config("enable-example"))
        set_kind("binary")
        set_languages("c++26")
        set_policy("build.c++.modules", true)

        add_packages("crab_cpp")
        add_files("src/*.cppm", example_src)
        add_includedirs("include")

        on_config(function (target)
            _add_speculo_flags(target)

            if target:is_plat("windows", "mingw", "msys") and target:has_tool("cc", "gcc") then
                target:add("links", "stdc++exp")
                target:add("ldflags", "-Wl,--allow-multiple-definition")
            end
        end)

        add_tests("default")

    target_end()
end
