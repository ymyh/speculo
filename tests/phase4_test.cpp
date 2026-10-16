#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

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

// ─────────────────────────────────────────────────────────────────────────────
// Class construction views (Java double view)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ConstructorTest, ClassConstructionViews)
{
    const speculo::Class& cls = speculo::class_of<Vec>();
    const speculo::Class& int_cls = speculo::class_of<int>();

    // default + explicit(int) + (int,int) + implicit copy + implicit move
    EXPECT_EQ(cls.get_constructors().size(), 5u);
    EXPECT_EQ(cls.get_declared_constructors().size(), cls.get_constructors().size());

    ASSERT_TRUE(cls.get_constructor().is_some());
    EXPECT_TRUE(cls.get_constructor().unwrap().is_default_constructor());
    EXPECT_TRUE(cls.get_constructor().unwrap().is_defaulted());

    ASSERT_TRUE(cls.get_constructor({ &int_cls }).is_some());
    EXPECT_EQ(cls.get_constructor({ &int_cls }).unwrap().get_parameter_count(), 1u);
    EXPECT_TRUE(cls.get_constructor({ &int_cls }).unwrap().is_explicit());

    // Template overloads resolve to the same constructors.
    EXPECT_TRUE(cls.get_constructor<int>().is_some());
    EXPECT_TRUE((cls.get_constructor<int, int>().is_some()));
    EXPECT_EQ((cls.get_constructor<int, int>().unwrap().get_parameter_count()), 2u);

    // A non-existent signature -> None.
    EXPECT_TRUE(cls.get_constructor({ &speculo::class_of<char>() }).is_none());

    // Copy / move constructors are identifiable by predicate (their exact
    // parameter types const T& / T&& stay distinct; only the runtime decay key
    // is shared).
    bool has_copy = false;
    bool has_move = false;
    for (const speculo::Constructor& ctor : cls.get_constructors())
    {
        if (ctor.is_copy_constructor()) { has_copy = true; }
        if (ctor.is_move_constructor()) { has_move = true; }
    }
    EXPECT_TRUE(has_copy);
    EXPECT_TRUE(has_move);
}

TEST(ConstructorTest, Metadata)
{
    const speculo::Class& cls = speculo::class_of<Vec>();
    speculo::Constructor two = cls.get_constructor<int, int>().unwrap();

    EXPECT_EQ(two.get_declaring_class(), cls);
    EXPECT_EQ(two.get_parameter_count(), 2u);
    ASSERT_EQ(two.get_parameter_types().size(), 2u);
    EXPECT_EQ(*two.get_parameter_types()[0], speculo::class_of<int>());
    EXPECT_EQ(*two.get_parameter_types()[1], speculo::class_of<int>());
    EXPECT_FALSE(two.get_signature().empty());
    EXPECT_TRUE(two.modifiers().is_public());
    EXPECT_FALSE(two.is_explicit());
    EXPECT_FALSE(two.is_deleted());
}

TEST(ConstructorTest, NewInstance)
{
    const speculo::Class& cls = speculo::class_of<Vec>();

    int three = 3;
    int four = 4;
    speculo::Object made = std::move(cls.new_instance({ three, four }).unwrap());
    ASSERT_FALSE(made.is_null());
    const Vec& value = made.as<Vec>().unwrap();
    EXPECT_EQ(value.x, 3);
    EXPECT_EQ(value.y, 4);
    EXPECT_EQ(made.type(), cls);

    // No public constructor matches -> NotFound.
    char one = 1;
    EXPECT_EQ(cls.new_instance({ one }).unwrap_err().kind, speculo::ErrorKind::NotFound);

    // Direct Constructor::new_instance.
    speculo::Constructor def = cls.get_constructor().unwrap();
    speculo::Object zero = std::move(def.new_instance().unwrap());
    EXPECT_EQ(zero.as<Vec>().unwrap().x, 0);

    speculo::Constructor two = cls.get_constructor<int, int>().unwrap();
    EXPECT_EQ(two.new_instance({ one }).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
    double wrong = 1.0;
    EXPECT_EQ(two.new_instance({ one, wrong }).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
}

TEST(ConstructorTest, ConstructAtAndDestroyAt)
{
    const speculo::Class& cls = speculo::class_of<Vec>();
    speculo::Constructor two = cls.get_constructor<int, int>().unwrap();

    alignas(Vec) unsigned char storage[sizeof(Vec)];
    int six = 6;
    int seven = 7;
    ASSERT_TRUE(two.construct_at(storage, { six, seven }).is_ok());

    Vec* placed = reinterpret_cast<Vec*>(storage);
    EXPECT_EQ(placed->x, 6);
    EXPECT_EQ(placed->y, 7);

    EXPECT_TRUE(two.destroy_at(storage).is_ok());

    int one = 1;
    EXPECT_EQ(two.construct_at(storage, { one }).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
    EXPECT_EQ(two.destroy_at(nullptr).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
}

TEST(ConstructorTest, DeletedAndAbstract)
{
    const speculo::Class& no_default = speculo::class_of<NoDefault>();
    speculo::Constructor deleted = no_default.get_constructor().unwrap();
    EXPECT_TRUE(deleted.is_deleted());
    EXPECT_EQ(deleted.new_instance().unwrap_err().kind, speculo::ErrorKind::NotSupported);

    // The non-deleted overload still works.
    int nine = 9;
    EXPECT_EQ(no_default.new_instance({ nine }).unwrap().as<NoDefault>().unwrap().v, 9);

    // Abstract classes cannot be instantiated.
    const speculo::Class& abstract = speculo::class_of<AbstractShape>();
    EXPECT_TRUE(abstract.is_abstract());
    EXPECT_EQ(abstract.new_instance().unwrap_err().kind, speculo::ErrorKind::NotSupported);
}

TEST(ConstructorTest, PrivateConstructorDeclaredView)
{
    const speculo::Class& cls = speculo::class_of<Sealed>();
    const speculo::Class& int_cls = speculo::class_of<int>();

    // The private constructor is hidden from the public view...
    EXPECT_TRUE(cls.get_constructor({ &int_cls }).is_none());
    // ...but visible in the declared view.
    ASSERT_TRUE(cls.get_declared_constructor({ &int_cls }).is_some());
    EXPECT_TRUE(cls.get_declared_constructor({ &int_cls }).unwrap().modifiers().is_private());

    // new_instance only sees public constructors.
    int five = 5;
    EXPECT_EQ(cls.new_instance({ five }).unwrap_err().kind, speculo::ErrorKind::NotFound);
}

TEST(ConstructorTest, LvalueArgumentCopiesInsteadOfMoving)
{
    const speculo::Class& cls = speculo::class_of<MoveFirst>();
    int seven = 7;
    speculo::Object source = std::move(cls.new_instance({ seven }).unwrap());

    // The caller wrote no std::move: the copy constructor must be chosen even
    // though the move constructor is declared first.
    auto copied = cls.new_instance({ source.ref() });
    ASSERT_TRUE(copied.is_ok());
    speculo::Object copy_box = std::move(copied).unwrap();
    const MoveFirst& copy = copy_box.as<MoveFirst>().unwrap();
    EXPECT_EQ(copy.copied, 1);
    EXPECT_EQ(copy.moved, 0);
    EXPECT_EQ(source.as<MoveFirst>().unwrap().v, 7);
}

TEST(ConstructorTest, DeletedCandidateDoesNotShadowUsableOne)
{
    const speculo::Class& cls = speculo::class_of<DeletedMoveFirst>();
    int four = 4;
    speculo::Object source = std::move(cls.new_instance({ four }).unwrap());

    auto copied = cls.new_instance({ source.ref() });
    ASSERT_TRUE(copied.is_ok());
    EXPECT_EQ(std::move(copied).unwrap().as<DeletedMoveFirst>().unwrap().v, 4);
}

TEST(ConstructorTest, MoveOnlyNeedsExplicitMoveConstructor)
{
    const speculo::Class& cls = speculo::class_of<MoveOnlyCtor>();
    int five = 5;
    speculo::Object source = std::move(cls.new_instance({ five }).unwrap());

    // A lvalue cannot select the move constructor automatically; the copy
    // constructor is deleted -> NotSupported, and the source is untouched.
    EXPECT_EQ(cls.new_instance({ source.ref() }).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(source.as<MoveOnlyCtor>().unwrap().v, 5);

    // Obtaining the move constructor explicitly is the opt-in to a move.
    speculo::Constructor move_ctor = cls.get_constructor<MoveOnlyCtor&&>().unwrap();
    auto moved = move_ctor.new_instance({ source.ref() });
    ASSERT_TRUE(moved.is_ok());
    EXPECT_EQ(std::move(moved).unwrap().as<MoveOnlyCtor>().unwrap().v, 5);
    EXPECT_EQ(source.as<MoveOnlyCtor>().unwrap().v, -1);
}

TEST(ConstructorTest, ConstArgumentCannotBindToMutableReference)
{
    const speculo::Class& cls = speculo::class_of<FromMutableRef>();
    int one = 1;
    speculo::Object source = std::move(cls.new_instance({ one }).unwrap());

    EXPECT_EQ(cls.new_instance({ source.ref() }).unwrap().as<FromMutableRef>().unwrap().v, 101);

    const speculo::ObjectRef const_view = source.cref();
    ASSERT_TRUE(const_view.is_const());
    EXPECT_EQ(cls.new_instance({ const_view }).unwrap_err().kind, speculo::ErrorKind::NotFound);
}

TEST(ConstructorTest, OwnershipIsTransferred)
{
    Tracked::alive = 0;
    {
        const speculo::Class& cls = speculo::class_of<Tracked>();
        int eleven = 11;
        speculo::Object obj = std::move(cls.new_instance({ eleven }).unwrap());
        EXPECT_EQ(Tracked::alive, 1);
        EXPECT_EQ(obj.as<Tracked>().unwrap().v, 11);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Method overload resolution (three lookup levels)
// ─────────────────────────────────────────────────────────────────────────────

TEST(MethodTest, OverloadResolutionByPointer)
{
    const speculo::Class& cls = speculo::class_of<Vec>();

    speculo::Method scale_int = cls.get_method("scale", { &speculo::class_of<int>() }).unwrap();
    EXPECT_FALSE(scale_int.is_const());
    EXPECT_EQ(scale_int.get_return_type(), speculo::class_of<void>());
    EXPECT_EQ(scale_int.get_parameter_count(), 1u);

    speculo::Method scale_double = cls.get_method("scale", { &speculo::class_of<double>() }).unwrap();
    EXPECT_TRUE(scale_double.is_const());
    EXPECT_EQ(scale_double.get_return_type(), speculo::class_of<int>());

    EXPECT_TRUE(cls.get_method("scale", { &speculo::class_of<char>() }).is_err());
}

TEST(MethodTest, OverloadResolutionByTemplate)
{
    const speculo::Class& cls = speculo::class_of<Vec>();

    speculo::Method by_int = cls.get_method<int>("scale").unwrap();
    EXPECT_FALSE(by_int.is_const());

    speculo::Method by_double = cls.get_method<double>("scale").unwrap();
    EXPECT_TRUE(by_double.is_const());

    // Name as a compile-time constant.
    speculo::Method by_name = cls.get_method<"scale", int>().unwrap();
    EXPECT_FALSE(by_name.is_const());

    // Reference parameters normalize to the referred type.
    EXPECT_TRUE(cls.get_method<const Vec&>("dot").is_ok());
}

TEST(MethodTest, InvokeSelectedOverloads)
{
    const speculo::Class& cls = speculo::class_of<Vec>();
    speculo::Method scale_int = cls.get_method<int>("scale").unwrap();
    speculo::Method scale_double = cls.get_method<double>("scale").unwrap();

    Vec v{3, 4};

    int factor = 2;
    ASSERT_TRUE(scale_int.invoke(v, { factor }).is_ok());
    EXPECT_EQ(v.x, 6);
    EXPECT_EQ(v.y, 8);

    double half = 0.5;
    auto scaled = scale_double.invoke(v, { half });
    EXPECT_EQ(std::move(scaled).unwrap().as<int>().unwrap(), 3);
}

TEST(MethodTest, StaticMethod)
{
    const speculo::Class& cls = speculo::class_of<Vec>();
    speculo::Method dupe = cls.get_method("dupe").unwrap();

    EXPECT_TRUE(dupe.is_static_member());
    Vec arg{1, 2};
    auto made = dupe.invoke(speculo::ObjectRef{}, { arg });
    ASSERT_TRUE(made.is_ok());
    EXPECT_EQ(std::move(made).unwrap().as<Vec>().unwrap().x, 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Virtual dispatch and method classification
// ─────────────────────────────────────────────────────────────────────────────

TEST(MethodTest, VirtualDispatch)
{
    const speculo::Class& shape = speculo::class_of<Shape>();
    speculo::Method area = shape.get_method("area").unwrap();
    EXPECT_TRUE(area.is_virtual());
    EXPECT_FALSE(area.is_pure_virtual());

    Square square;
    square.s = 5;
    Shape& as_shape = square;

    auto result = area.invoke(as_shape, {});
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(std::move(result).unwrap().as<int>().unwrap(), 25);

    speculo::Method override = speculo::class_of<Square>().get_method("area").unwrap();
    EXPECT_TRUE(override.is_override());
}

TEST(MethodTest, PureVirtualAndOperatorAndDestructor)
{
    speculo::Method draw = speculo::class_of<AbstractShape>().get_method("draw").unwrap();
    EXPECT_TRUE(draw.is_pure_virtual());
    EXPECT_TRUE(draw.modifiers().is_abstract());

    // Operator functions carry no identifier; classification uses is_operator().
    const speculo::Class& op_cls = speculo::class_of<Op>();
    bool found_operator = false;
    for (const speculo::Method& method : op_cls.get_methods())
    {
        if (method.is_operator())
        {
            found_operator = true;
            Op object;
            int rhs = 3;
            EXPECT_EQ(method.invoke(object, { rhs }).unwrap().as<int>().unwrap(), 5);
        }
    }
    EXPECT_TRUE(found_operator);

    // User-declared destructors are surfaced but not invokable via invoke().
    const speculo::Class& widget = speculo::class_of<Widget>();
    bool found_destructor = false;
    for (const speculo::Method& method : widget.get_declared_methods())
    {
        if (method.is_destructor())
        {
            found_destructor = true;
            EXPECT_EQ(method.get_return_type(), speculo::class_of<void>());
            Widget object;
            EXPECT_EQ(method.invoke(object, {}).unwrap_err().kind, speculo::ErrorKind::NotSupported);
        }
    }
    EXPECT_TRUE(found_destructor);
}

TEST(MethodTest, QualifierOnlyOverloadsAreAmbiguous)
{
    const speculo::Class& cls = speculo::class_of<Qualified>();
    const speculo::Class* int_cls = &speculo::class_of<int>();

    // The parameter-typed lookups ignore cv / ref, so they cannot choose.
    EXPECT_EQ(cls.get_method("f", { int_cls }).unwrap_err().kind, speculo::ErrorKind::Ambiguous);
    EXPECT_EQ(cls.get_method<int>("f").unwrap_err().kind, speculo::ErrorKind::Ambiguous);
    EXPECT_EQ(cls.get_method("f", { "int" }).unwrap_err().kind, speculo::ErrorKind::Ambiguous);
    EXPECT_EQ(cls.get_method("g").unwrap_err().kind, speculo::ErrorKind::Ambiguous);
    EXPECT_EQ(cls.get_method("nope").unwrap_err().kind, speculo::ErrorKind::NotFound);

    // Single candidates are unaffected.
    const speculo::Class& vec = speculo::class_of<Vec>();
    EXPECT_TRUE(vec.get_method("dot", { &speculo::class_of<const Vec&>() }).is_ok());
    EXPECT_TRUE(vec.get_method("dupe").is_ok());
    EXPECT_EQ(vec.get_method("dot", { &speculo::class_of<Vec>() }).unwrap_err().kind, speculo::ErrorKind::NotFound);
    // Overloaded name without a parameter list is ambiguous as well.
    EXPECT_EQ(vec.get_method("scale").unwrap_err().kind, speculo::ErrorKind::Ambiguous);
}

TEST(ModifierTest, InterfaceIsAbstractStatelessAndAllPureVirtual)
{
    EXPECT_TRUE(speculo::class_of<IShape>().modifiers().is_interface());
    EXPECT_TRUE(speculo::class_of<IShape>().modifiers().is_abstract());
    EXPECT_TRUE(speculo::class_of<IShape>().modifiers().to_string().contains("interface"));

    // An interface that extends an interface is still one.
    EXPECT_TRUE(speculo::class_of<IRound>().modifiers().is_interface());

    // Data members, a concrete virtual function or a stateful base all disqualify.
    EXPECT_TRUE(speculo::class_of<AbstractWithState>().modifiers().is_abstract());
    EXPECT_FALSE(speculo::class_of<AbstractWithState>().modifiers().is_interface());
    EXPECT_TRUE(speculo::class_of<AbstractWithBody>().modifiers().is_abstract());
    EXPECT_FALSE(speculo::class_of<AbstractWithBody>().modifiers().is_interface());
    EXPECT_FALSE(speculo::class_of<DerivedFromState>().modifiers().is_interface());

    // Concrete and non-class types are not interfaces.
    EXPECT_FALSE(speculo::class_of<Vec>().modifiers().is_interface());
    EXPECT_FALSE(speculo::class_of<int>().modifiers().is_interface());
}

TEST(ModifierTest, FinalOnVirtualFunctions)
{
    const speculo::Class& base = speculo::class_of<Overrides>();
    const speculo::Class& derived = speculo::class_of<OverridesDerived>();

    for (std::string_view name : { "plain", "locked", "sealed" })
    {
        EXPECT_FALSE(base.get_declared_method(name).unwrap().modifiers().is_final()) << name;
    }

    EXPECT_FALSE(derived.get_declared_method("plain").unwrap().modifiers().is_final());
    EXPECT_TRUE(derived.get_declared_method("locked").unwrap().modifiers().is_final());
    EXPECT_TRUE(derived.get_declared_method("sealed").unwrap().modifiers().is_final());
    EXPECT_TRUE(derived.get_declared_method("sealed").unwrap().is_override());

    // Non-virtual functions never carry it.
    EXPECT_FALSE(speculo::class_of<Mixed>().get_method("only").unwrap().modifiers().is_final());
}

TEST(ModifierTest, ClassAccessFollowsNesting)
{
    EXPECT_TRUE(speculo::class_of<Vec>().modifiers().is_public());
    EXPECT_TRUE(speculo::class_of<Outer>().modifiers().is_public());
    EXPECT_TRUE(speculo::class_of<Outer::Open>().modifiers().is_public());

    const speculo::Modifier hidden = outer_private_class().modifiers();
    EXPECT_TRUE(hidden.is_private());
    EXPECT_FALSE(hidden.is_public());

    const speculo::Modifier guarded = outer_protected_class().modifiers();
    EXPECT_TRUE(guarded.is_protected());
    EXPECT_FALSE(guarded.is_public());
}

TEST(MethodTest, UserDeclaredAssignmentOperatorsAreListed)
{
    const speculo::Class& cls = speculo::class_of<Assignable>();
    const speculo::Class* copy_param = &speculo::class_of<const Assignable&>();
    const speculo::Class* move_param = &speculo::class_of<Assignable&&>();

    // Operators have no identifier, so they are found by their parameter list.
    const auto find_assignment = [](const speculo::Class& owner, const speculo::Class* param)
    {
        for (const speculo::Method& method : owner.get_methods())
        {
            if (method.is_operator() && method.get_parameter_types().size() == 1
                && method.get_parameter_types()[0] == param && method.get_signature().contains("operator="))
            {
                return method;
            }
        }
        return speculo::Method{};
    };

    speculo::Method copy = find_assignment(cls, copy_param);
    speculo::Method move = find_assignment(cls, move_param);
    ASSERT_EQ(copy.get_parameter_count(), 1u);
    ASSERT_EQ(move.get_parameter_count(), 1u);
    EXPECT_TRUE(copy.is_operator());
    EXPECT_FALSE(copy.is_defaulted());
    EXPECT_TRUE(move.is_defaulted());
    EXPECT_TRUE(copy.is_invokable());
    EXPECT_TRUE(move.is_invokable());

    Assignable target;
    Assignable source;
    source.v = 5;
    auto assigned = copy.invoke(target, { source });
    ASSERT_TRUE(assigned.is_ok());
    EXPECT_EQ(target.v, 105);
    EXPECT_EQ(std::move(assigned).unwrap().type(), cls);

    // Only the user-declared ones are listed: an implicit assignment is not.
    for (const speculo::Method& method : speculo::class_of<Vec>().get_methods())
    {
        EXPECT_FALSE(method.get_signature().contains("operator=")) << method.get_signature();
    }
}

TEST(MethodTest, DeletedAndVariadicFunctionsAreListedButNotInvokable)
{
    const speculo::Class& cls = speculo::class_of<Locked>();
    Locked object;
    int three = 3;
    double half = 0.5;

    // A deleted copy assignment stays in the table, flagged.
    speculo::Method deleted_assign;
    for (const speculo::Method& method : cls.get_methods())
    {
        if (method.is_operator() && method.get_signature().contains("operator="))
        {
            deleted_assign = method;
        }
    }
    ASSERT_EQ(deleted_assign.get_parameter_count(), 1u);
    EXPECT_TRUE(deleted_assign.is_deleted());
    EXPECT_FALSE(deleted_assign.is_invokable());
    EXPECT_TRUE(deleted_assign.modifiers().to_string().contains("deleted"));
    auto refused = deleted_assign.invoke(object, { object });
    ASSERT_TRUE(refused.is_err());
    EXPECT_EQ(refused.unwrap_err().kind, speculo::ErrorKind::NotSupported);

    // A deleted overload next to a live one: the name is overloaded, each is reachable.
    EXPECT_EQ(cls.get_method("dead").unwrap_err().kind, speculo::ErrorKind::Ambiguous);
    speculo::Method dead_int = cls.get_method<int>("dead").unwrap();
    speculo::Method dead_double = cls.get_method<double>("dead").unwrap();
    EXPECT_TRUE(dead_int.is_deleted());
    EXPECT_EQ(dead_int.invoke(object, { three }).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_FALSE(dead_double.is_deleted());
    EXPECT_EQ(std::move(dead_double.invoke(object, { half })).unwrap().as<int>().unwrap(), 1);

    // A C-variadic function lists its fixed parameters and is not invokable.
    speculo::Method variadic = cls.get_method("variadic").unwrap();
    EXPECT_EQ(variadic.get_parameter_count(), 1u);
    EXPECT_FALSE(variadic.is_invokable());
    EXPECT_FALSE(variadic.is_deleted());
    EXPECT_EQ(variadic.invoke(object, { three }).unwrap_err().kind, speculo::ErrorKind::NotSupported);

    // Ordinary members are unaffected.
    EXPECT_EQ(std::move(cls.get_method("alive").unwrap().invoke(object, { three })).unwrap().as<int>().unwrap(), 3);

    // Templates (deducing-this ones included) and non-template explicit-object
    // functions are not enumerated at all, and do not break materialization.
    EXPECT_EQ(cls.get_method("generic").unwrap_err().kind, speculo::ErrorKind::NotFound);
    EXPECT_EQ(cls.get_method("deduced").unwrap_err().kind, speculo::ErrorKind::NotFound);
    EXPECT_EQ(cls.get_method("explicit_object").unwrap_err().kind, speculo::ErrorKind::NotFound);
}

TEST(MethodTest, NameOnlyLookupAndTheNullaryOverload)
{
    const speculo::Class& cls = speculo::class_of<Mixed>();
    const Mixed object;

    // A bare name is "the method called that", never a silently chosen overload.
    EXPECT_EQ(cls.get_method("n").unwrap_err().kind, speculo::ErrorKind::Ambiguous);
    // A unique name resolves regardless of its parameters.
    EXPECT_TRUE(cls.get_method("only").is_ok());

    // The nullary overload has three spellings; the empty braces are not ambiguous.
    speculo::Method by_braces = cls.get_method("n", {}).unwrap();
    speculo::Method by_template = cls.get_method<>("n").unwrap();
    speculo::Method by_list = cls.get_method("n", std::initializer_list<const speculo::Class*>{}).unwrap();
    EXPECT_EQ(by_braces, by_template);
    EXPECT_EQ(by_braces, by_list);
    EXPECT_EQ(by_braces.get_parameter_types().size(), 0u);
    EXPECT_EQ(std::move(by_braces.invoke(object, {})).unwrap().as<int>().unwrap(), 10);

    // The other overload is still reachable with its parameter list, by type or by name.
    speculo::Method with_int = cls.get_method("n", { &speculo::class_of<int>() }).unwrap();
    EXPECT_NE(with_int, by_braces);
    EXPECT_EQ(cls.get_method("n", { "int" }).unwrap(), with_int);

    // `{}` on a method that has parameters finds nothing; on declared methods too.
    EXPECT_EQ(cls.get_method("only", {}).unwrap_err().kind, speculo::ErrorKind::NotFound);
    speculo::Method declared = cls.get_declared_method("n", {}).unwrap();
    EXPECT_EQ(declared.get_name(), "n");
    EXPECT_EQ(declared.get_parameter_types().size(), 0u);
}

TEST(MethodTest, QualifiersSelectBetweenOtherwiseIdenticalOverloads)
{
    const speculo::Class& cls = speculo::class_of<Qualified>();
    const speculo::Class* int_cls = &speculo::class_of<int>();

    Qualified object;
    const Qualified const_object;
    int zero = 0;

    speculo::Method plain = cls.get_method("f", { int_cls }, speculo::MemberQualifiers{}).unwrap();
    EXPECT_FALSE(plain.is_const());
    EXPECT_EQ(plain.invoke(object, { zero }).unwrap().as<int>().unwrap(), 1);

    speculo::Method constant = cls.get_method("f", { int_cls }, speculo::MemberQualifiers{ .is_const = true }).unwrap();
    EXPECT_TRUE(constant.is_const());
    EXPECT_EQ(constant.invoke(const_object, { zero }).unwrap().as<int>().unwrap(), 2);

    EXPECT_NE(plain, constant);
    EXPECT_EQ(cls.get_method("f", { int_cls }, speculo::MemberQualifiers{ .is_volatile = true }).unwrap_err().kind,
              speculo::ErrorKind::NotFound);

    // Ref-qualifiers are part of the key too; an `&&` overload declared first
    // does not hide the invokable `&` one.
    speculo::Method lvalue = cls.get_method("g", {}, speculo::MemberQualifiers{ .ref = speculo::RefQualifier::Lvalue }).unwrap();
    speculo::Method rvalue = cls.get_method("g", {}, speculo::MemberQualifiers{ .ref = speculo::RefQualifier::Rvalue }).unwrap();
    EXPECT_TRUE(lvalue.is_invokable());
    EXPECT_FALSE(rvalue.is_invokable());
    EXPECT_EQ(std::move(lvalue.invoke(object, {})).unwrap().as<int>().unwrap(), 11);
}
TEST(MethodTest, MoveOnlySignaturesDoNotBreakMaterialization)
{
    // class_of<Owner>() must still compile although two members cannot be
    // invoked by value; they are listed but report NotSupported at runtime.
    const speculo::Class& cls = speculo::class_of<Owner>();

    Owner owner;
    owner.p = std::make_unique<int>(9);

    speculo::Method get = cls.get_method("get").unwrap();
    EXPECT_EQ(get.invoke(owner, {}).unwrap_err().kind, speculo::ErrorKind::NotSupported);

    speculo::Method null_ref = cls.get_method("null_ref").unwrap();
    EXPECT_EQ(null_ref.invoke(speculo::ObjectRef{}, {}).unwrap_err().kind, speculo::ErrorKind::NotSupported);

    std::unique_ptr<int> replacement = std::make_unique<int>(1);
    speculo::Method reset = cls.get_method("reset").unwrap();
    EXPECT_EQ(reset.invoke(owner, { replacement }).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(*owner.p, 9);
    ASSERT_NE(replacement, nullptr);

    // Members that are fine keep working, including a by-value move-only return.
    EXPECT_EQ(cls.get_method("value").unwrap().invoke(owner, {}).unwrap().as<int>().unwrap(), 9);
    auto released = cls.get_method("release").unwrap().invoke(owner, {});
    ASSERT_TRUE(released.is_ok());
    EXPECT_EQ(*std::move(released).unwrap().as<std::unique_ptr<int>>().unwrap(), 9);
    EXPECT_EQ(owner.p, nullptr);
}

TEST(MethodTest, ReferenceReturnsBoxACopyUnlessInvokedByReference)
{
    const speculo::Class& cls = speculo::class_of<Slot>();
    speculo::Method ref = cls.get_method("ref").unwrap();
    speculo::Method plain = cls.get_method("plain").unwrap();
    Slot slot;

    // The declared return type keeps the reference; invoke() yields its decayed copy.
    EXPECT_TRUE(ref.returns_reference());
    EXPECT_FALSE(plain.returns_reference());
    EXPECT_EQ(ref.get_return_type(), speculo::class_of<int&>());
    auto boxed = ref.invoke(slot, {});
    ASSERT_TRUE(boxed.is_ok());
    speculo::Object copy = std::move(boxed).unwrap();
    EXPECT_EQ(copy.type(), speculo::class_of<int>());
    EXPECT_NE(copy.type(), ref.get_return_type());
    copy.as<int>().unwrap() = 99; // writes go to the copy
    EXPECT_EQ(slot.value, 3);

    // invoke_ref() reaches the object itself, with the referent's object type.
    auto viewed = ref.invoke_ref(slot, {});
    ASSERT_TRUE(viewed.is_ok());
    speculo::ObjectRef view = viewed.unwrap();
    EXPECT_EQ(view.type(), speculo::class_of<int>());
    EXPECT_FALSE(view.is_const());
    EXPECT_EQ(view.raw_data(), &slot.value);
    view.as<int>().unwrap() = 7;
    EXPECT_EQ(slot.value, 7);

    // A method that does not return a reference has nothing to view.
    EXPECT_EQ(plain.invoke_ref(slot, {}).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_FALSE(plain.is_ref_invokable());
    EXPECT_TRUE(ref.is_ref_invokable());
}

TEST(MethodTest, InvokeRefKeepsConstnessChecksAndArguments)
{
    const speculo::Class& cls = speculo::class_of<Slot>();
    speculo::Method cref = cls.get_method("cref").unwrap();
    speculo::Method ref = cls.get_method("ref").unwrap();
    speculo::Method at = cls.get_method("at").unwrap();
    speculo::Method moved = cls.get_method("moved").unwrap();
    speculo::Method shared = cls.get_method("shared").unwrap();

    Slot slot;
    const Slot const_slot;

    // `const int&` is viewed as a const referent; the receiver may be const.
    auto read_only = cref.invoke_ref(const_slot, {});
    ASSERT_TRUE(read_only.is_ok());
    speculo::ObjectRef read_view = read_only.unwrap();
    EXPECT_TRUE(read_view.is_const());
    EXPECT_TRUE(read_view.as<int>().is_none());
    EXPECT_EQ(read_view.as<const int>().unwrap(), 3);
    EXPECT_EQ(read_view.raw_data(), &const_slot.value);

    // The receiver checks match invoke().
    EXPECT_EQ(ref.invoke_ref(const_slot, {}).unwrap_err().kind, speculo::ErrorKind::ReadOnly);
    EXPECT_EQ(ref.invoke_ref(speculo::ObjectRef{}, {}).unwrap_err().kind, speculo::ErrorKind::NullInstance);

    // Arguments are forwarded, and the result aliases the receiver.
    int add = 4;
    auto bumped = at.invoke_ref(slot, { add });
    ASSERT_TRUE(bumped.is_ok());
    EXPECT_EQ(slot.value, 7);
    EXPECT_EQ(bumped.unwrap().raw_data(), &slot.value);
    EXPECT_EQ(at.invoke_ref(slot, {}).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);

    // `T&&` returns are viewed too (the referent is the receiver's member).
    auto rvalue = moved.invoke_ref(slot, {});
    ASSERT_TRUE(rvalue.is_ok());
    EXPECT_EQ(rvalue.unwrap().raw_data(), &slot.value);

    // Static members take no receiver.
    auto global = shared.invoke_ref(speculo::ObjectRef{}, {});
    ASSERT_TRUE(global.is_ok());
    speculo::ObjectRef global_view = global.unwrap();
    EXPECT_EQ(global_view.as<int>().unwrap(), 40);
    EXPECT_EQ(global_view.raw_data(), &Slot::shared());
}

TEST(MethodTest, InvokeRefReachesNonCopyableReferents)
{
    const speculo::Class& cls = speculo::class_of<Owner>();
    Owner owner;
    owner.p = std::make_unique<int>(9);

    // invoke() cannot copy a unique_ptr, but invoke_ref() needs no copy.
    speculo::Method get = cls.get_method("get").unwrap();
    EXPECT_FALSE(get.is_invokable());
    EXPECT_TRUE(get.is_ref_invokable());
    EXPECT_EQ(get.invoke(owner, {}).unwrap_err().kind, speculo::ErrorKind::NotSupported);

    auto viewed = get.invoke_ref(owner, {});
    ASSERT_TRUE(viewed.is_ok());
    speculo::ObjectRef view = viewed.unwrap();
    EXPECT_TRUE(view.is_const());
    EXPECT_EQ(view.raw_data(), &owner.p);
    EXPECT_EQ(*view.as<const std::unique_ptr<int>>().unwrap(), 9);

    speculo::Method null_ref = cls.get_method("null_ref").unwrap();
    auto none = null_ref.invoke_ref(speculo::ObjectRef{}, {});
    ASSERT_TRUE(none.is_ok());
    EXPECT_EQ(none.unwrap().as<const std::unique_ptr<int>>().unwrap().get(), nullptr);

    // By-value parameters that cannot be copied still disable both entry points.
    speculo::Method reset = cls.get_method("reset").unwrap();
    EXPECT_FALSE(reset.is_ref_invokable());
}

TEST(ConstructorTest, MoveOnlyByValueParameterIsNotInvokable)
{
    const speculo::Class& cls = speculo::class_of<Owner>();
    speculo::Constructor ctor = cls.get_constructor<std::unique_ptr<int>>().unwrap();

    std::unique_ptr<int> value = std::make_unique<int>(3);
    EXPECT_EQ(ctor.new_instance({ value }).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    ASSERT_NE(value, nullptr);

    int tag = 4;
    EXPECT_EQ(std::move(cls.new_instance({ tag }).unwrap()).as<Owner>().unwrap().tag, 4);
}

// ─────────────────────────────────────────────────────────────────────────────
// Const correctness (runtime-checked)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ConstCorrectnessTest, MethodInvocation)
{
    const speculo::Class& cls = speculo::class_of<Widget>();
    speculo::Method get = cls.get_method("get").unwrap();
    speculo::Method set = cls.get_method("set").unwrap();

    Widget mutable_object;
    const Widget const_object{};

    EXPECT_TRUE(get.is_const());
    EXPECT_FALSE(set.is_const());

    // const method on a mutable or const object -> OK.
    EXPECT_EQ(get.invoke(mutable_object, {}).unwrap().as<int>().unwrap(), 1);
    EXPECT_EQ(get.invoke(const_object, {}).unwrap().as<int>().unwrap(), 1);

    // non-const method on a const object -> ReadOnly.
    int five = 5;
    EXPECT_EQ(set.invoke(const_object, { five }).unwrap_err().kind, speculo::ErrorKind::ReadOnly);
    EXPECT_TRUE(set.invoke(mutable_object, { five }).is_ok());
    EXPECT_EQ(mutable_object.v, 5);
}

TEST(ConstCorrectnessTest, ObjectRefConstness)
{
    Widget mutable_object;
    const Widget const_object{};

    speculo::ObjectRef mutable_ref(mutable_object);
    EXPECT_FALSE(mutable_ref.is_const());
    EXPECT_TRUE(mutable_ref.as<Widget>().is_some());

    // A view of a const object: non-const `as` is refused...
    speculo::ObjectRef const_ref(const_object);
    EXPECT_TRUE(const_ref.is_const());
    EXPECT_TRUE(const_ref.as<Widget>().is_none());

    // ...while a const-qualified handle yields a const view.
    const speculo::ObjectRef readonly_ref = const_object;
    EXPECT_TRUE(readonly_ref.as<Widget>().is_some());
}

TEST(ConstCorrectnessTest, ConstQualifiedAsReadsConstObjectThroughNonConstHandle)
{
    Widget mutable_object;
    const Widget const_object{};

    speculo::ObjectRef const_ref(const_object);
    auto read = const_ref.as<const Widget>();
    ASSERT_TRUE(read.is_some());
    EXPECT_EQ(&read.unwrap(), &const_object);
    static_assert(std::is_same_v<decltype(read.unwrap()), const Widget&>);

    // Asking for a writable T from a const view still fails.
    EXPECT_TRUE(const_ref.as<Widget>().is_none());

    // A mutable view can still be read as const, and as writable.
    speculo::ObjectRef mutable_ref(mutable_object);
    EXPECT_TRUE(mutable_ref.as<const Widget>().is_some());
    EXPECT_TRUE(mutable_ref.as<Widget>().is_some());

    // A type mismatch is still None.
    EXPECT_TRUE(const_ref.as<const int>().is_none());
}

TEST(ArgumentLiteralTest, ConstructWithLiterals)
{
    const speculo::Class& cls = speculo::class_of<Vec>();

    auto made = cls.new_instance({ 3, 4 });
    ASSERT_TRUE(made.is_ok());
    speculo::Object vec = std::move(made).unwrap();
    EXPECT_EQ(vec.as<Vec>().unwrap().x, 3);
    EXPECT_EQ(vec.as<Vec>().unwrap().y, 4);

    // Value expressions work too, and still need an exact type match.
    int base = 10;
    auto computed = cls.new_instance({ base + 1, base * 2 });
    ASSERT_TRUE(computed.is_ok());
    EXPECT_EQ(std::move(computed).unwrap().as<Vec>().unwrap().y, 20);

    EXPECT_EQ(cls.new_instance({ 3, 4.0 }).unwrap_err().kind, speculo::ErrorKind::NotFound);
    EXPECT_EQ(cls.new_instance({ true }).unwrap_err().kind, speculo::ErrorKind::NotFound);

    speculo::Constructor two = cls.get_constructor<int, int>().unwrap();
    EXPECT_TRUE(two.new_instance({ 1, 2 }).is_ok());
    EXPECT_EQ(two.new_instance({ 3, 4.0 }).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
}

TEST(ArgumentLiteralTest, InvokeWithTemporariesAndStringLiterals)
{
    const speculo::Class& cls = speculo::class_of<Greeter>();
    Greeter greeter;

    // A string literal is passed as const char*.
    speculo::Method length = cls.get_method("length").unwrap();
    EXPECT_EQ(std::move(length.invoke(greeter, { "hello" })).unwrap().as<int>().unwrap(), 5);

    // A class temporary binds to a const& parameter...
    speculo::Method weigh = cls.get_method("weigh").unwrap();
    EXPECT_EQ(std::move(weigh.invoke(greeter, { Vec(2, 5) })).unwrap().as<int>().unwrap(), 7);

    // ...and to a by-value parameter, alongside other literals.
    speculo::Method scaled = cls.get_method("scaled").unwrap();
    EXPECT_EQ(std::move(scaled.invoke(greeter, { Vec(1, 2), 3 })).unwrap().as<int>().unwrap(), 9);

    speculo::Method flip = cls.get_method("flip").unwrap();
    EXPECT_FALSE(std::move(flip.invoke(greeter, { true })).unwrap().as<bool>().unwrap());

    // No numeric promotion: a short is not an int, and a char* literal is not a Vec.
    EXPECT_EQ(scaled.invoke(greeter, { Vec(1, 2), short{ 3 } }).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_EQ(weigh.invoke(greeter, { "abc" }).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    // Lvalues, ObjectRefs and Objects are still accepted alongside temporaries.
    int times = 2;
    EXPECT_EQ(std::move(scaled.invoke(greeter, { Vec(1, 1), times })).unwrap().as<int>().unwrap(), 4);
    speculo::Object owned = std::move(speculo::Object::from_value(4)).unwrap();
    EXPECT_EQ(std::move(scaled.invoke(greeter, { Vec(1, 1), owned })).unwrap().as<int>().unwrap(), 8);
    EXPECT_EQ(std::move(scaled.invoke(greeter, { Vec(1, 1), speculo::ObjectRef(times) })).unwrap().as<int>().unwrap(), 4);

    // More than the inline capacity still works (heap path); the call itself fails on count.
    EXPECT_EQ(scaled.invoke(greeter, { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }).unwrap_err().kind,
              speculo::ErrorKind::InvalidArgument);
}

TEST(VolatileCorrectnessTest, HandleRecordsVolatility)
{
    Register plain;
    volatile Register device;

    speculo::ObjectRef plain_ref(plain);
    speculo::ObjectRef device_ref(device);
    EXPECT_FALSE(plain_ref.is_volatile());
    EXPECT_TRUE(device_ref.is_volatile());
    EXPECT_FALSE(device_ref.is_const());

    // Same unqualified type key either way.
    EXPECT_EQ(device_ref.type(), speculo::class_of<Register>());

    // as<T>() needs the volatile qualifier to be spelled out.
    EXPECT_TRUE(device_ref.as<Register>().is_none());
    EXPECT_TRUE(device_ref.as<const Register>().is_none());
    ASSERT_TRUE(device_ref.as<volatile Register>().is_some());
    EXPECT_EQ(&device_ref.as<volatile Register>().unwrap(), &device);
    EXPECT_TRUE(plain_ref.as<Register>().is_some());

    // Qualifiers combine.
    const volatile Register cv_device{};
    speculo::ObjectRef cv_ref(cv_device);
    EXPECT_TRUE(cv_ref.is_const());
    EXPECT_TRUE(cv_ref.is_volatile());
    EXPECT_TRUE(cv_ref.as<const Register>().is_none());
    EXPECT_TRUE(cv_ref.as<const volatile Register>().is_some());
}

TEST(VolatileCorrectnessTest, MethodsRequireVolatileQualifiedOnVolatileObject)
{
    const speculo::Class& cls = speculo::class_of<Register>();
    volatile Register device;
    speculo::ObjectRef device_ref(device);

    speculo::Method read = cls.get_method("read").unwrap();
    speculo::Method peek = cls.get_method("peek").unwrap();
    speculo::Method write = cls.get_method("write").unwrap();

    EXPECT_EQ(std::move(read.invoke(device_ref, {})).unwrap().as<int>().unwrap(), 5);
    EXPECT_EQ(std::move(peek.invoke(device_ref, {})).unwrap().as<int>().unwrap(), 5);

    int nine = 9;
    auto rejected = write.invoke(device_ref, { nine });
    ASSERT_TRUE(rejected.is_err());
    EXPECT_EQ(rejected.unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(device.value, 5);

    // A volatile method is callable on a non-volatile object.
    Register plain;
    EXPECT_EQ(std::move(read.invoke(plain, {})).unwrap().as<int>().unwrap(), 5);
}

TEST(VolatileCorrectnessTest, FieldAccessOnVolatileObject)
{
    const speculo::Class& cls = speculo::class_of<Register>();
    speculo::Field value = cls.get_field("value").unwrap();
    speculo::Field status = cls.get_field("status").unwrap();

    volatile Register device;
    speculo::ObjectRef device_ref(device);
    int one = 1;

    // By-value reads and writes would drop volatile: refused.
    EXPECT_EQ(value.get(device_ref).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(value.set(device_ref, one).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    EXPECT_EQ(device.value, 5);

    // A view keeps the qualifier, and as<volatile T>() is the explicit access.
    auto view = value.get_ref(device_ref);
    ASSERT_TRUE(view.is_ok());
    EXPECT_TRUE(view.unwrap().is_volatile());
    EXPECT_TRUE(view.unwrap().as<int>().is_none());
    ASSERT_TRUE(view.unwrap().as<volatile int>().is_some());
    view.unwrap().as<volatile int>().unwrap() = 11;
    EXPECT_EQ(device.value, 11);

    // A member declared volatile gives a volatile view even on a plain object,
    // while its by-value get / set stay available (the thunk accesses it as volatile).
    Register plain;
    auto status_view = status.get_ref(plain);
    ASSERT_TRUE(status_view.is_ok());
    EXPECT_TRUE(status_view.unwrap().is_volatile());
    EXPECT_TRUE(status_view.unwrap().as<int>().is_none());
    EXPECT_EQ(status_view.unwrap().as<volatile int>().unwrap(), 7);
    EXPECT_TRUE(status.set(plain, one).is_ok());
    EXPECT_EQ(plain.status, 1);

    auto plain_view = value.get_ref(plain);
    ASSERT_TRUE(plain_view.is_ok());
    EXPECT_FALSE(plain_view.unwrap().is_volatile());
}

TEST(VolatileCorrectnessTest, VolatileArgumentsAndCopiesAreRefused)
{
    volatile int sample = 3;
    speculo::ObjectRef sample_ref(sample);
    EXPECT_TRUE(sample_ref.is_volatile());
    EXPECT_EQ(sample_ref.clone().unwrap_err().kind, speculo::ErrorKind::NotSupported);

    // Passing it by value (copy) to a method is refused as a type mismatch.
    const speculo::Class& cls = speculo::class_of<Register>();
    Register plain;
    EXPECT_EQ(cls.get_method("write").unwrap().invoke(plain, { sample_ref }).unwrap_err().kind,
              speculo::ErrorKind::TypeMismatch);

    // Upcasts keep the qualifier.
    volatile Register device;
    auto same = cls.cast_to(speculo::ObjectRef(device), cls);
    ASSERT_TRUE(same.is_ok());
    EXPECT_TRUE(same.unwrap().is_volatile());
}

TEST(ConstCorrectnessTest, RawDataOfNonConstHandleIsReadOnlyPointer)
{
    const Widget const_object{};
    speculo::ObjectRef const_ref(const_object);

    static_assert(std::is_same_v<decltype(const_ref.raw_data()), const void*>);
    EXPECT_EQ(const_ref.raw_data(), static_cast<const void*>(&const_object));
}

TEST(ConstCorrectnessTest, FieldAccess)
{
    const speculo::Class& cls = speculo::class_of<Widget>();
    speculo::Field field = cls.get_field("v").unwrap();

    Widget mutable_object;
    const Widget const_object{};
    int nine = 9;

    EXPECT_EQ(field.set(const_object, nine).unwrap_err().kind, speculo::ErrorKind::ReadOnly);
    EXPECT_TRUE(field.set(mutable_object, nine).is_ok());
    EXPECT_EQ(mutable_object.v, 9);

    // get_ref() honours the object's constness.
    auto mutable_view = field.get_ref(mutable_object);
    ASSERT_TRUE(mutable_view.is_ok());
    EXPECT_FALSE(mutable_view.unwrap().is_const());
    EXPECT_TRUE(mutable_view.unwrap().as<int>().is_some());

    auto const_view = field.get_ref(const_object);
    ASSERT_TRUE(const_view.is_ok());
    EXPECT_TRUE(const_view.unwrap().is_const());
    EXPECT_TRUE(const_view.unwrap().as<int>().is_none());
    const speculo::ObjectRef readonly_view = const_view.unwrap();
    EXPECT_TRUE(readonly_view.as<int>().is_some());
}

// ─────────────────────────────────────────────────────────────────────────────
// Type-checked field lookup (template overload)
// ─────────────────────────────────────────────────────────────────────────────

TEST(FieldLookupTest, TypeChecked)
{
    const speculo::Class& cls = speculo::class_of<Vec>();

    EXPECT_TRUE(cls.get_field<int>("x").is_some());
    EXPECT_TRUE(cls.get_field<long>("x").is_none());
    EXPECT_TRUE(cls.get_field<int>("missing").is_none());
    EXPECT_TRUE(cls.get_declared_field<int>("y").is_some());
}
