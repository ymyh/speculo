cmake_minimum_required(VERSION 3.31.6)

# Enable experimental `import std;` support. This must be set before project()
# so that CMake's compiler probe can see it. GCC/libstdc++ does not ship a
# prebuilt `std` module, so CMake builds it from bits/std.cc (only supported by
# the Ninja generators). The gate value is tied to the CMake version; update it
# when upgrading CMake (a wrong value produces a warning that names the gate).
if (CMAKE_VERSION VERSION_GREATER_EQUAL "4.4")
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
else()
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
endif()

project(speculo LANGUAGES CXX)

# speculo targets C++26 only; `-freflection` enables the P2996 static
# reflection subset implemented by GCC 16+.
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
    message(FATAL_ERROR "speculo requires GCC >= 16 (no `-freflection` / P2996 support)")
endif()

# Let CMake add the standard library `std` module dependency for GCC/libstdc++.
set(CMAKE_CXX_MODULE_STD ON)

# Dependency: crab_cpp (also a C++ module library), pulled in via FetchContent.
# Set -DFETCHCONTENT_SOURCE_DIR_CRAB_CPP=<path> to build against a local checkout.
include(FetchContent)
FetchContent_Declare(
    crab_cpp
    GIT_REPOSITORY https://github.com/ymyh/crab-cpp.git
    GIT_TAG v0.4.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(crab_cpp)

# crab_cpp's own CMakeLists.txt pins `C++23` and leaves RTTI/exceptions on, but
# speculo is `C++26` + `-fno-rtti -fno-exceptions`. Both import `std`, and every
# `import std;` consumer must agree on the std module's dialect. When they do
# not, CMake builds two `std` modules and aborts with "Disagreement of the
# location of the 'std' module" / "language dialect differs" (seen with CLion's
# bundled CMake; the plain `cmake -G Ninja` command line happens to tolerate
# it). Normalize the dependency to speculo's standard and flags so a single
# `std` module serves every consumer. (crab_cpp is exception-free by design.)
if (TARGET crab_cpp)
    set_target_properties(crab_cpp PROPERTIES
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_options(crab_cpp PRIVATE -fno-rtti -fno-exceptions)
endif()

# MinGW/GCC: libstdc++'s terminal helpers (std::__open_terminal /
# std::__write_to_terminal, used by std::print and friends) live in
# libstdc++exp. Attach it to crab_cpp so it lands *after* the static archive on
# the link line; a direct dependency of the executable would be ordered before
# crab_cpp and fail to resolve its symbols.
if (MINGW)
    target_link_libraries(crab_cpp INTERFACE stdc++exp)
endif()

option(SPECULO_BUILD_TEST "Build speculo tests" ON)

# Main library target. Module sources must be declared as a CXX_MODULES file
# set, otherwise the Ninja generator cannot build them.
add_library(speculo)
target_sources(speculo
    PUBLIC
        FILE_SET CXX_MODULES FILES
            src/error.cppm
            src/modifier.cppm
            src/descriptor.cppm
            src/object.cppm
            src/function.cppm
            src/registry.cppm
            src/mod.cppm
)

target_include_directories(speculo PUBLIC include)
target_link_libraries(speculo PUBLIC crab_cpp)
# -freflection enables P2996; -fno-rtti / -fno-exceptions ban RTTI and exception
# throwing project-wide (errors are reported via crab_cpp Option/Result only).
target_compile_options(speculo PRIVATE -freflection -fno-rtti -fno-exceptions)

# Tests
if (SPECULO_BUILD_TEST)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/releases/download/v1.18.0/googletest-1.18.0.tar.gz
        URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    )
    FetchContent_MakeAvailable(googletest)

    file(GLOB_RECURSE SPECULO_TEST_SOURCES tests/*.cpp)

    add_executable(speculo_test ${SPECULO_TEST_SOURCES})
    target_link_libraries(speculo_test PRIVATE speculo gtest gtest_main)
    target_include_directories(speculo_test PRIVATE include)
    target_compile_options(speculo_test PRIVATE -freflection -fno-rtti -fno-exceptions)

    # MinGW/GCC workaround for libstdc++ `import std`: GCC PR116746 emits static
    # locals from libstdc++ headers with external linkage, producing duplicate
    # symbol errors for any program that uses both `import std` and
    # std::print/std::format.
    if (MINGW)
        target_link_options(speculo_test PRIVATE "-Wl,--allow-multiple-definition")
    endif()

    add_test(NAME speculo_test COMMAND speculo_test)
endif()

# Examples: standalone programs demonstrating the public API. Each *.cpp in
# examples/ becomes its own executable (file(GLOB) -> re-run configure after
# adding a file). They link the speculo module library directly.
option(SPECULO_BUILD_EXAMPLES "Build speculo examples" ON)

if (SPECULO_BUILD_EXAMPLES)
    file(GLOB SPECULO_EXAMPLE_SOURCES examples/*.cpp)

    foreach (example_src IN LISTS SPECULO_EXAMPLE_SOURCES)
        get_filename_component(example_name "${example_src}" NAME_WE)
        set(example_target "speculo_example_${example_name}")

        add_executable("${example_target}" "${example_src}")
        target_link_libraries("${example_target}" PRIVATE speculo)
        target_include_directories("${example_target}" PRIVATE include)
        target_compile_options("${example_target}" PRIVATE -freflection -fno-rtti -fno-exceptions)

        if (MINGW)
            target_link_options("${example_target}" PRIVATE "-Wl,--allow-multiple-definition")
        endif()

        add_test(NAME "${example_target}" COMMAND "${example_target}")
    endforeach()
endif()
