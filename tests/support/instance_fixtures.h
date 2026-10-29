// Fixtures for the constructor, method, const/volatile and argument tests.
// Include after `import std; import crab_cpp; import speculo;`.

#pragma once

namespace
{

struct Vec
{
    int x = 0;
    int y = 0;

    Vec() = default;
    explicit Vec(int v) : x(v), y(v) {}
    Vec(int a, int b) : x(a), y(b) {}

    int dot(const Vec& other) const { return x * other.x + y * other.y; }
    void scale(int factor) { x *= factor; y *= factor; }
    int scale(double factor) const { return static_cast<int>(static_cast<double>(x) * factor); }
    static Vec dupe(const Vec& v) { return Vec(v.x + 1, v.y + 1); }
};

struct Widget
{
    int v = 1;

    int get() const { return v; }
    void set(int value) { v = value; }
    ~Widget() = default; // user-declared destructor -> surfaced as a Method
};

struct Greeter
{
    int tag = 0;

    int length(const char* text) const { return static_cast<int>(std::string_view(text).size()); }
    int weigh(const Vec& vec) const { return vec.x + vec.y; }
    int scaled(Vec vec, int times) const { return (vec.x + vec.y) * times; }
    bool flip(bool value) const { return !value; }
};

struct IShape
{
    virtual ~IShape() = default;
    virtual int area() const = 0;
    virtual void draw() = 0;
    static int count() { return 0; }
};

struct IRound : IShape
{
    virtual int radius() const = 0;
};

struct AbstractWithState
{
    int id = 0;
    virtual void run() = 0;
};

struct AbstractWithBody
{
    virtual void run() = 0;
    virtual void stop() {}
};

struct DerivedFromState : AbstractWithState
{
    void run() override {}
};

struct Overrides
{
    virtual void plain() {}
    virtual void locked() {}
    virtual void sealed() {}
};

struct OverridesDerived : Overrides
{
    void plain() override {}
    void locked() final {}
    void sealed() override final {}
};

class Outer
{
public:
    struct Open {};

    // Friends, not members: keeps `speculo::Class` out of Outer's own reflection.
    friend auto outer_private_class() -> const speculo::Class&;
    friend auto outer_protected_class() -> const speculo::Class&;

private:
    struct Hidden {};

protected:
    struct Guarded {};
};

auto outer_private_class() -> const speculo::Class& { return speculo::class_of<Outer::Hidden>(); }
auto outer_protected_class() -> const speculo::Class& { return speculo::class_of<Outer::Guarded>(); }

struct Assignable
{
    int v = 0;

    Assignable() = default;
    Assignable(const Assignable&) = default;
    auto operator=(const Assignable& other) -> Assignable&
    {
        v = other.v + 100;
        return *this;
    }
    auto operator=(Assignable&&) -> Assignable& = default;
};

struct Locked
{
    int v = 0;

    auto operator=(const Locked&) -> Locked& = delete;
    int dead(int) = delete;
    int dead(double) const { return 1; }
    int alive(int x) const { return x; }
    int variadic(int first, ...) const { return first; }
    int explicit_object(this Locked&, int) { return 2; }
    template<typename T> int generic(T) { return 3; }
    template<typename Self> int deduced(this Self&&, int) { return 4; }
};

struct Mixed
{
    int base = 10;

    int n() const { return base; }
    int n(int add) const { return base + add; }
    int only(int v) const { return v; }
};

struct Register
{
    int value = 5;
    volatile int status = 7;

    int read() volatile { return value; }
    int peek() const volatile { return value; }
    void write(int v) { value = v; }
};

struct Op
{
    int v = 2;
    int operator+(int rhs) const { return v + rhs; }
};

struct Shape
{
    virtual int area() const { return 0; }
    virtual ~Shape() = default;
};

struct Square : Shape
{
    int s = 3;
    int area() const override { return s * s; }
};

struct AbstractShape
{
    virtual void draw() const = 0;
    virtual ~AbstractShape() = default;
};

struct NoDefault
{
    int v = 0;
    NoDefault() = delete;
    explicit NoDefault(int x) : v(x) {}
};

class Sealed
{
public:
    int v = 0;

    Sealed(const Sealed&) = default;
    Sealed(Sealed&&) = default;

private:
    explicit Sealed(int x) : v(x) {}
};

struct Tracked
{
    static int alive;

    int v = 0;

    Tracked() { ++alive; }
    explicit Tracked(int value) : v(value) { ++alive; }
    Tracked(const Tracked& other) : v(other.v) { ++alive; }
    ~Tracked() { --alive; }
};

int Tracked::alive = 0;

// Move constructor declared before the copy constructor, so declaration order
// would pick it first for a lvalue argument.
struct MoveFirst
{
    int v = 0;
    int moved = 0;
    int copied = 0;

    explicit MoveFirst(int x) : v(x) {}
    MoveFirst(MoveFirst&& other) noexcept : v(other.v), moved(other.moved + 1) { other.v = -1; }
    MoveFirst(const MoveFirst& other) : v(other.v), copied(other.copied + 1) {}
};

// Deleted move constructor first, usable copy constructor after it.
struct DeletedMoveFirst
{
    int v = 0;

    explicit DeletedMoveFirst(int x) : v(x) {}
    DeletedMoveFirst(DeletedMoveFirst&&) = delete;
    DeletedMoveFirst(const DeletedMoveFirst& other) : v(other.v) {}
};

struct MoveOnlyCtor
{
    int v = 0;

    explicit MoveOnlyCtor(int x) : v(x) {}
    MoveOnlyCtor(const MoveOnlyCtor&) = delete;
    MoveOnlyCtor(MoveOnlyCtor&& other) noexcept : v(other.v) { other.v = -1; }
};

// Mutable-reference constructor: must not bind to a const object.
struct FromMutableRef
{
    int v = 0;

    explicit FromMutableRef(int x) : v(x) {}
    FromMutableRef(FromMutableRef& other) : v(other.v + 100) {}
};

// Overloads that differ only in cv / ref qualification.
struct Qualified
{
    int f(int) { return 1; }
    int f(int) const { return 2; }

    int g() && { return 10; } // not invokable (no value category), declared first
    int g() & { return 11; }
};

// Members that cannot be called through a copying thunk: a by-value move-only
// parameter and a reference return of a move-only type.
struct Owner
{
    std::unique_ptr<int> p;
    int tag = 0;

    Owner() = default;
    explicit Owner(std::unique_ptr<int> value) : p(std::move(value)) {}
    explicit Owner(int t) : tag(t) {}

    void reset(std::unique_ptr<int> value) { p = std::move(value); }
    const std::unique_ptr<int>& get() const { return p; }
    static const std::unique_ptr<int>& null_ref() { static const std::unique_ptr<int> none; return none; }
    std::unique_ptr<int> release() { return std::move(p); }
    int value() const { return p ? *p : -1; }
};

// Reference returns: invoke() boxes a copy, invoke_ref() views the referent.
struct Slot
{
    int value = 3;

    int& ref() { return value; }
    const int& cref() const { return value; }
    int&& moved() { return std::move(value); }
    int& at(int add) { value += add; return value; }
    int plain() const { return value; }
    static int& shared() { static int storage = 40; return storage; }
};

} // namespace
