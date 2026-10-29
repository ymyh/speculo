// Fixtures for the type identity, inheritance, enumerator and array tests.
// Include after `import std; import crab_cpp; import speculo;`.

#pragma once

namespace
{

struct Root
{
    int r = 0;
    virtual ~Root() = default;
};

struct Other
{
    int o = 0;
};

struct Multi : Root, Other
{
    int m = 0;
};

struct VRoot
{
    int vr = 0;
};

struct VLeaf : virtual VRoot
{
    int vl = 0;
};

// Non-virtual diamond: casting D -> A is ambiguous.
struct A
{
    int a = 0;
};
struct B1 : A
{
};
struct B2 : A
{
};
struct D : B1, B2
{
};

// Virtual diamond: both paths reach the one shared virtual base subobject.
struct DiamondBase
{
    int db = 0;
};
struct DiamondLeft : virtual DiamondBase
{
};
struct DiamondRight : virtual DiamondBase
{
};
struct DiamondBottom : DiamondLeft, DiamondRight
{
};

// One virtual and one non-virtual path: two distinct DiamondBase subobjects.
struct MixedVirtual : virtual DiamondBase
{
};
struct MixedPlain : DiamondBase
{
};
struct MixedBottom : MixedVirtual, MixedPlain
{
};

// Inaccessible bases: the relation exists in the type graph but cannot be used.
struct HiddenBase
{
    int hb = 0;
};
struct ProtectedBase
{
    int pb = 0;
};
struct Unrelated
{
    int u = 0;
};
class Closed : private HiddenBase, protected ProtectedBase, public Root
{
};

// Recursive member lookup: hiding, overloads, siblings, static members.
struct HideBase
{
    int x = 1;
    int y = 5;
    int f() const { return 1; }
};
struct HideDerived : HideBase
{
    int x = 2;
};

struct Over
{
    int g() const { return 1; }
    int g(int v) const { return v; }
};
struct OverDerived : Over
{
};
struct OverHiding : Over
{
    int g(double) const { return 3; }
};

struct P1
{
    int same = 1;
};
struct P2
{
    int same = 2;
};
struct PP : P1, P2
{
};

struct SBase
{
    int nv = 0;
    static int sv() { return 5; }
};
struct SLeft : SBase
{
};
struct SRight : SBase
{
};
struct SBottom : SLeft, SRight
{
};

enum class Color
{
    Red,
    Green = 5,
    Blue,
};

enum Legacy
{
    Alpha = 1,
    Beta = 2,
};

enum class Narrow : short
{
    Neg = -3,
};

enum class Small : unsigned char
{
    Low = 1,
    High = 255,
};

enum class Big : unsigned long long
{
    Huge = 0xFFFFFFFFFFFFFFFFULL,
};

struct Prop
{
    int value = 0;
};

struct Arrays
{
    int v[3] = { 1, 2, 3 };
    int m[2][2] = { { 1, 2 }, { 3, 4 } };
};

struct NoDefault
{
    int v = 0;
    NoDefault() = delete;
    explicit NoDefault(int x) : v(x) {}
};

struct Tracked
{
    static int alive;

    int v = 0;

    Tracked() { ++alive; }
    explicit Tracked(int value) : v(value) { ++alive; }
    Tracked(const Tracked& other) : v(other.v) { ++alive; }
    auto operator=(const Tracked& other) -> Tracked& = default;
    ~Tracked() { --alive; }
};

int Tracked::alive = 0;

struct alignas(64) OverAligned
{
    int v = 3;
};

struct SmallObject
{
    int v = 1;
};

struct PinnedElement
{
    int v = 0;
    PinnedElement() = default;
    PinnedElement(const PinnedElement&) = delete;
};

} // namespace
