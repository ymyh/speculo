// Fixtures for the registry, diagnostics, std-library and annotation tests.
// Include after `import std; import crab_cpp; import speculo;`.

#pragma once

namespace p6
{

struct Point
{
    int x = 0;
    int y = 0;

    Point() = default;
    Point(int a, int b) : x(a), y(b) {}

    int scale(int factor) const { return x * factor; }
};

struct Base
{
    int b = 0;
};

struct Derived : Base
{
    int d = 0;
};

/// Per-thread registration target for the concurrency test (must be in a named
/// namespace so it has a canonical name and actually gets registered).
template<int I>
struct Tag
{
    int payload = I;
};

/// A parameter whose compiler spelling (`long long unsigned int`) nobody writes.
struct Wide
{
    int take(unsigned long long value) const { return static_cast<int>(value); }
};

} // namespace p6

namespace
{

struct Anonymous
{
    int a = 0;
};

} // namespace

namespace p6b
{

struct Wide
{
    int sum(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) const
    {
        return a + b + c + d + e + f + g + h + i + j;
    }

    int sum8(int a, int b, int c, int d, int e, int f, int g, int h) const
    {
        return a + b + c + d + e + f + g + h;
    }

    Wide() = default;
    Wide(int, double) {}
};

} // namespace p6b

namespace p6c
{

struct Person
{
    int age = 0;
    std::string name;
    std::vector<std::string> tags;

    std::string greet(const std::string& prefix) const { return prefix + name; }
};

struct Anon
{
    int tag = 0;
    union
    {
        int   as_int;
        float as_float;
    };
};

} // namespace p6c

namespace p6ann
{

struct Tag
{
};

struct Label
{
    int rank = 0;
};

struct Rename
{
    const char* name = nullptr;
};

struct [[= Tag{}]]
Base
{
};

struct [[= Label{ 9 }]]
Derived : [[= Tag{}]] Base
{
    [[= Label{ 3 }]]
    [[= 1, = 2]]
    int x = 0;

    [[= "literal"]]
    int text = 0;

    [[= std::array<char, 8>{ "literal" }]]
    int chars = 0;

    [[= Rename{ "hidden" }]]
    int renamed = 0;

    [[= Tag{}]]
    Derived() = default;

    [[nodiscard]]
    int plain() const { return x; }

    void scale([[= Tag{}]] int factor) const { (void)factor; }
};

enum class Color
{
    Red [[= 7]],
    Green,
};

} // namespace p6ann
