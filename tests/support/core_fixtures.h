// Fixtures for Error / Modifier / Args / Class / Object tests.
// Include after `import std; import crab_cpp; import speculo;`.

#pragma once

namespace
{

// Counts destructions to exercise Object / ObjectRef ownership.
struct Counter
{
    static int destroyed;

    int value = 0;

    Counter() = default;
    explicit Counter(int v) : value(v) {}
    Counter(const Counter&) = default;
    auto operator=(const Counter&) -> Counter& = default;
    ~Counter() { ++destroyed; }
};

int Counter::destroyed = 0;

struct Widget
{
    int x = 0;
    virtual ~Widget() = default;
};

} // namespace
