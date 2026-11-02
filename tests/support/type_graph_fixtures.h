#ifndef SPECULO_TEST_TYPE_GRAPH_FIXTURES_H
#define SPECULO_TEST_TYPE_GRAPH_FIXTURES_H

// Fixtures for the relation graph (api_design §15): virtual inheritance chains,
// multiple inheritance, both kinds of diamond, and override / shadow pairs.
//
// Unlike the other fixture headers these types live in a **named** namespace:
// the graph tests query the registry by name and need canonical names, and one
// of them (`Unregistered`) exists precisely to prove that a type which is never
// passed to class_of<T>() stays invisible to the reverse edge index.

namespace tg
{

// ── A three-level virtual chain: Shape <- Circle <- Ball ──
struct Shape
{
    virtual ~Shape() = default;
    virtual int area() const { return 0; }
    virtual void resize(int factor) { size = factor; }
    void hidden() const {}

    int size = 1;
};

struct Circle : Shape
{
    int area() const override { return 3; }
    void resize(int factor) override { size = factor * 2; }
    void hidden() const {}

    int radius = 2;
};

struct Ball : Circle
{
    int area() const override { return 4; }
};

// Same name, different parameters: hides `area()`, overrides nothing.
struct Sizer : Shape
{
    int area(int scale) const { return scale * 5; }
};

// Same name, dropped `const`: a different signature, so it is not an override
// even though it is virtual.
struct ConstMismatch : Shape
{
    int area() { return 6; }
};

// Never materialized: the negative case for get_derived_classes /
// get_overrider_methods. Do not call class_of<Unregistered>() anywhere.
struct Unregistered : Shape
{
    int area() const override { return 7; }
};

// Non-virtual base method: a same-signature method in the child shadows it.
struct Plain
{
    int compute() const { return 8; }
};

struct PlainChild : Plain
{
    int compute() const { return 9; }
};

// ── Multiple inheritance, no shared base ──
struct LeftRoot
{
    int left = 10;
};

struct RightRoot
{
    int right = 11;
};

struct Both : LeftRoot, RightRoot
{
};

// ── Virtual diamond: one shared VTop subobject ──
struct VTop
{
    int top = 12;
};

struct VLeft : virtual VTop
{
};

struct VRight : virtual VTop
{
};

struct VBottom : VLeft, VRight
{
};

// ── Non-virtual diamond: two distinct NTop subobjects (an ambiguous path) ──
struct NTop
{
    int top = 13;
};

struct NLeft : NTop
{
};

struct NRight : NTop
{
};

struct NBottom : NLeft, NRight
{
};

// ── Inaccessible bases ──
struct HiddenRoot
{
    int hidden = 14;
};

struct ProtectedRoot
{
    int guarded = 15;
};

class Closed : private HiddenRoot, protected ProtectedRoot
{
};

struct Unrelated
{
    int value = 16;
};

} // namespace tg

#endif // SPECULO_TEST_TYPE_GRAPH_FIXTURES_H
