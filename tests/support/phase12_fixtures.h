// Fixtures shared by the Phase 12 tests (api_design §11–§14, §16).
//
// Included after the module imports, like every other support header. The types
// live in an unnamed namespace so each test TU gets its own copy.

#ifndef SPECULO_TESTS_PHASE12_FIXTURES_H
#define SPECULO_TESTS_PHASE12_FIXTURES_H

namespace
{

/// Counts live instances, so allocator paths can prove destruction ran.
struct P12Counter
{
    static int live;

    int value = 0;

    P12Counter() { ++live; }
    explicit P12Counter(int v) : value(v) { ++live; }
    P12Counter(const P12Counter& other) : value(other.value) { ++live; }
    P12Counter(P12Counter&& other) noexcept : value(other.value)
    {
        other.value = -1;
        ++live;
    }

    auto operator=(const P12Counter&) -> P12Counter& = default;
    auto operator=(P12Counter&&) noexcept -> P12Counter& = default;

    ~P12Counter() { --live; }

    [[nodiscard]] auto get() const -> int { return value; }
    void set(int v) { value = v; }
    void bump(int& v) { ++v; }
    [[nodiscard]] auto label() const -> std::string { return "P12Counter"; }
    [[nodiscard]] auto slot() -> int& { return value; }
    [[nodiscard]] auto cslot() const -> const int& { return value; }
    [[nodiscard]] auto consume() && -> int { return value; }
    [[nodiscard]] static auto twice(int x) -> int { return 2 * x; }
};

inline int P12Counter::live = 0;

/// Plain aggregate for new_aggregate_with.
struct P12Aggregate
{
    int a;
    int b;
};

/// A field type with no std::formatter (to_string marks it "<unprintable>").
struct P12Opaque
{
    int v;
};

/// Every field is formattable: int / double / std::string / unprintable.
struct P12Printable
{
    int            number = 0;
    double         ratio = 0.0;
    std::string    text;
    P12Opaque      opaque{ 7 };
};

/// Over-aligned to prove the allocator receives the alignment it asked for.
struct alignas(64) P12OverAligned
{
    int value = 0;
};

// ── Constructor-selection fixtures (new_instance_with matrix) ──

/// Two constructors that both accept one non-const `int` lvalue -> Ambiguous.
/// The parameter types differ at the C++ level (`int&` vs `const int&`), so each
/// per-constructor thunk still compiles; only the runtime selector sees a tie.
struct P12Ambig
{
    P12Ambig(int&) {}
    P12Ambig(const int&) {}
};

/// A deleted matching constructor -> NotSupported.
struct P12Deleted
{
    P12Deleted() = default;
    P12Deleted(int) = delete;
};

/// Abstract -> NotSupported before any selection.
struct P12Abstract
{
    virtual void f() = 0;
    virtual ~P12Abstract() = default;
};

/// A public constructor that passes selection but cannot run: a by-value
/// move-only parameter has no loadable thunk. Drives the "allocated, then
/// construction failed -> block returned" path.
struct P12MoveOnlyCtor
{
    explicit P12MoveOnlyCtor(std::unique_ptr<int>) {}
};

// ── Diagnostic field-shape fixtures ──

/// Every interesting field shape: scalar, reference, bit-field, array,
/// volatile, and a static member.
struct P12Fields
{
    P12Fields() : ref(value) {}

    int          value = 5;
    int&         ref;
    int          bits : 3 = 2;
    int          arr[2] = { 1, 2 };
    volatile int vol = 0;
    static int   counter;
};

inline int P12Fields::counter = 0;

/// describe() must only list public members.
class P12Private
{
public:
    int exposed = 1;

private:
    int hidden = 2;

protected:
    int guarded = 3;
};

union P12Union
{
    int   as_int;
    float as_float;
};

enum class P12Enum : unsigned char
{
    First,
    Second,
};

/// All argument-parameter classes: too many for Args' inline buffer.
struct P12ManyArgs
{
    [[nodiscard]] auto sum10(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10) const
        -> int
    {
        return a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10;
    }
};

/// Methods that exist but cannot be invoked.
struct P12Uninvokable
{
    void removed(int) = delete;
    [[nodiscard]] auto varargs(int first, ...) const -> int { return first; }
    [[nodiscard]] operator int() const { return 7; } // conversion operator
};


[[nodiscard]] inline auto p12_add(int a, int b) -> int { return a + b; }
inline void p12_noop(int) {}
[[nodiscard]] inline auto p12_slot_ref(P12Counter& counter) -> int& { return counter.value; }
inline auto p12_varargs(int first, ...) -> int { return first; }

} // namespace

#endif // SPECULO_TESTS_PHASE12_FIXTURES_H
