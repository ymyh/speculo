// Fixtures for the class, field, method and base-navigation tests.
// Include after `import std; import crab_cpp; import speculo;`.

#pragma once

namespace
{

struct Point
{
    int x = 0;
    int y = 0;

    double norm() const { return static_cast<double>(x) + static_cast<double>(y); }
};

struct Geometry
{
    int x = 0;
    int& ref;
    const int cst = 9;
    unsigned bf : 3;
    unsigned bf2 : 5;
    static int sval;

    explicit Geometry(int& r) : ref(r) {}

    int add(int a, int b) const { return x + a + b; }
    void bump(int by) { x += by; }
    static int smul(int a, int b) { return a * b; }
};

int Geometry::sval = 100;

struct Cache
{
    int plain = 1;
    mutable int hits = 0;
    const int fixed = 3;
    static int shared;
};

int Cache::shared = 0;

struct RefHolder
{
    int& r;
    const int& cr;
    std::unique_ptr<int>& up;
    std::unique_ptr<int> owned;
};

class Secret
{
public:
    int visible = 1;

    int read_secret() const { return hidden + guarded; }

protected:
    int guarded = 2;

private:
    int hidden = 3;

    int secret_method() const { return hidden; }
};

struct Root
{
    int r = 0;
    int root_method() const { return r; }
};

struct Leaf : Root
{
    int l = 1;
};

struct Other
{
    int o = 2;
};

struct Multi : Root, Other
{
    int m = 3;
};

struct VRoot
{
    int vr = 4;
};

struct VLeaf : virtual VRoot
{
    int vl = 5;
};

} // namespace
