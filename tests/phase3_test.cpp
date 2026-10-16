#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

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

TEST(ClassTest, PointIdentityAndSize)
{
    const speculo::Class& cls = speculo::class_of<Point>();

    EXPECT_EQ(cls.get_name(), "Point");
    EXPECT_EQ(cls.get_size(), sizeof(Point));
    EXPECT_EQ(cls.get_alignment(), alignof(Point));
    EXPECT_TRUE(cls.is_class());
    EXPECT_TRUE(cls.is_aggregate());
    EXPECT_FALSE(cls.is_polymorphic());
    // Phase 5: the exact type is kept -- const Point& is a distinct Class.
    EXPECT_NE(cls, speculo::class_of<const Point&>());
    EXPECT_EQ(speculo::class_of<Point&>().get_referenced_type().unwrap(), cls);
    EXPECT_EQ(speculo::class_of<const Point&>().get_referenced_type().unwrap(), speculo::class_of<const Point>());
}

TEST(ClassTest, PublicFieldsAndMethods)
{
    const speculo::Class& cls = speculo::class_of<Point>();

    EXPECT_EQ(cls.get_fields().size(), 2u);
    ASSERT_TRUE(cls.get_field("x").is_some());
    ASSERT_TRUE(cls.get_field("y").is_some());
    EXPECT_TRUE(cls.get_field("z").is_none());

    EXPECT_EQ(cls.get_methods().size(), 1u);
    ASSERT_TRUE(cls.get_method("norm").is_ok());
    EXPECT_TRUE(cls.get_method("missing").is_err());
}

TEST(FieldTest, ReadWriteAndLayout)
{
    const auto& cls = speculo::class_of<Point>();
    speculo::Field fx = cls.get_field("x").unwrap();

    EXPECT_EQ(fx.get_name(), "x");
    EXPECT_EQ(fx.get_size(), sizeof(int));
    EXPECT_EQ(fx.get_alignment(), alignof(int));
    EXPECT_EQ(fx.get_offset(), 0u);
    EXPECT_FALSE(fx.is_bitfield());
    EXPECT_FALSE(fx.is_static());
    EXPECT_EQ(fx.get_declaring_class(), cls);
    EXPECT_EQ(fx.get_type(), speculo::class_of<int>());

    Point p{3, 4};

    auto read = fx.get(p);
    ASSERT_TRUE(read.is_ok());
    EXPECT_EQ(std::move(read).unwrap().as<int>().unwrap(), 3);

    int ten = 10;
    EXPECT_TRUE(fx.set(p, ten).is_ok());
    EXPECT_EQ(p.x, 10);

    // Wrong value type -> TypeMismatch.
    double d = 1.0;
    auto bad = fx.set(p, d);
    ASSERT_TRUE(bad.is_err());
    EXPECT_EQ(bad.unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
}

TEST(FieldTest, ReferenceViewAndBitfields)
{
    const auto& cls = speculo::class_of<Geometry>();
    Geometry::sval = 100;

    int external = 7;
    Geometry g(external);

    // Plain field: reference view points at the subobject.
    speculo::Field fx = cls.get_field("x").unwrap();
    auto ref = fx.get_ref(g);
    ASSERT_TRUE(ref.is_ok());
    ref.unwrap().as<int>().unwrap() = 42;
    EXPECT_EQ(g.x, 42);

    // Bitfield: readable/writable by value, but no reference view.
    speculo::Field bf = cls.get_field("bf").unwrap();
    EXPECT_TRUE(bf.is_bitfield());
    ASSERT_TRUE(bf.get_bit_width().is_some());
    EXPECT_EQ(bf.get_bit_width().unwrap(), 3u);
    // The first bitfield of its storage unit starts at the object's byte offset.
    EXPECT_EQ(bf.get_bit_offset(), bf.get_offset() * 8u);
    EXPECT_TRUE(bf.get_ref(g).is_err());
    EXPECT_EQ(bf.get_ref(g).unwrap_err().kind, speculo::ErrorKind::NotSupported);

    unsigned five = 5;
    ASSERT_TRUE(bf.set(g, five).is_ok());
    EXPECT_EQ(g.bf, 5u);
    EXPECT_EQ(bf.get(g).unwrap().as<unsigned>().unwrap(), 5u);

    // Reference member: get reads the referent, set writes through it.
    speculo::Field rf = cls.get_field("ref").unwrap();
    EXPECT_TRUE(rf.is_reference());
    EXPECT_EQ(rf.get(g).unwrap().as<int>().unwrap(), 7);
    int nine = 9;
    ASSERT_TRUE(rf.set(g, nine).is_ok());
    EXPECT_EQ(external, 9);

    // const member: readable, not writable.
    speculo::Field cst = cls.get_field("cst").unwrap();
    EXPECT_TRUE(cst.modifiers().is_const());
    EXPECT_EQ(cst.get(g).unwrap().as<int>().unwrap(), 9);
    int eleven = 11;
    auto ro = cst.set(g, eleven);
    ASSERT_TRUE(ro.is_err());
    EXPECT_EQ(ro.unwrap_err().kind, speculo::ErrorKind::ReadOnly);
}

TEST(FieldTest, StaticMember)
{
    Geometry::sval = 100;
    const auto& cls = speculo::class_of<Geometry>();
    speculo::Field sf = cls.get_field("sval").unwrap();

    EXPECT_TRUE(sf.is_static());
    EXPECT_EQ(sf.get(speculo::ObjectRef{}).unwrap().as<int>().unwrap(), 100);

    int two_hundred = 200;
    EXPECT_TRUE(sf.set(speculo::ObjectRef{}, two_hundred).is_ok());
    EXPECT_EQ(Geometry::sval, 200);
}

TEST(FieldTest, MutableFieldIsWritableOnConstObject)
{
    const auto& cls = speculo::class_of<Cache>();
    speculo::Field plain = cls.get_field("plain").unwrap();
    speculo::Field hits = cls.get_field("hits").unwrap();
    speculo::Field fixed = cls.get_field("fixed").unwrap();
    ASSERT_TRUE(hits.is_mutable());

    const Cache cache{};
    speculo::ObjectRef view(cache);
    ASSERT_TRUE(view.is_const());

    int five = 5;
    // A mutable member is writable through a const object, like in C++.
    ASSERT_TRUE(hits.set(view, five).is_ok());
    EXPECT_EQ(cache.hits, 5);

    // Non-mutable members stay read-only.
    EXPECT_EQ(plain.set(view, five).unwrap_err().kind, speculo::ErrorKind::ReadOnly);
    EXPECT_EQ(fixed.set(view, five).unwrap_err().kind, speculo::ErrorKind::ReadOnly);

    // Reference views: mutable member -> mutable view, others -> const view.
    auto hits_ref = hits.get_ref(view);
    ASSERT_TRUE(hits_ref.is_ok());
    EXPECT_FALSE(hits_ref.unwrap().is_const());
    hits_ref.unwrap().as<int>().unwrap() = 8;
    EXPECT_EQ(cache.hits, 8);

    EXPECT_TRUE(plain.get_ref(view).unwrap().is_const());
    EXPECT_TRUE(fixed.get_ref(view).unwrap().is_const());
}

TEST(ReceiverTest, ObjectOfAnotherTypeIsRejected)
{
    Point point{ 3, 4 };
    Cache cache{};
    speculo::ObjectRef wrong(cache);

    speculo::Field x = speculo::class_of<Point>().get_field("x").unwrap();
    int one = 1;
    EXPECT_EQ(x.get(wrong).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_EQ(x.set(wrong, one).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);
    EXPECT_EQ(x.get_ref(wrong).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    speculo::Method norm = speculo::class_of<Point>().get_method("norm").unwrap();
    EXPECT_EQ(norm.invoke(wrong, {}).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    // The right receiver still works.
    EXPECT_EQ(x.get(point).unwrap().as<int>().unwrap(), 3);
    EXPECT_TRUE(norm.invoke(point, {}).is_ok());
}

TEST(FieldTest, ReferenceMemberViewIsTheReferent)
{
    const auto& cls = speculo::class_of<RefHolder>();
    int target = 1;
    const int constant = 2;
    std::unique_ptr<int> pointee = std::make_unique<int>(3);
    RefHolder holder{ target, constant, pointee, nullptr };

    // `int&`: the view is the referent, typed as the object type, mutable.
    speculo::Field r = cls.get_field("r").unwrap();
    auto r_view = r.get_ref(holder);
    ASSERT_TRUE(r_view.is_ok());
    EXPECT_EQ(r_view.unwrap().type(), speculo::class_of<int>());
    EXPECT_FALSE(r_view.unwrap().is_const());
    EXPECT_EQ(r_view.unwrap().raw_data(), static_cast<const void*>(&target));
    r_view.unwrap().as<int>().unwrap() = 10;
    EXPECT_EQ(target, 10);

    // `const int&`: const follows the referenced type.
    speculo::Field cr = cls.get_field("cr").unwrap();
    auto cr_view = cr.get_ref(holder);
    ASSERT_TRUE(cr_view.is_ok());
    EXPECT_TRUE(cr_view.unwrap().is_const());
    EXPECT_EQ(cr_view.unwrap().raw_data(), static_cast<const void*>(&constant));
    int five = 5;
    EXPECT_EQ(cr.set(holder, five).unwrap_err().kind, speculo::ErrorKind::ReadOnly);

    // A const owner does not make the referent const.
    const RefHolder const_holder{ target, constant, pointee, nullptr };
    speculo::ObjectRef const_view(const_holder);
    ASSERT_TRUE(const_view.is_const());
    EXPECT_FALSE(r.get_ref(const_view).unwrap().is_const());
    EXPECT_TRUE(cr.get_ref(const_view).unwrap().is_const());
    ASSERT_TRUE(r.set(const_view, five).is_ok());
    EXPECT_EQ(target, 5);
    EXPECT_EQ(cr.set(const_view, five).unwrap_err().kind, speculo::ErrorKind::ReadOnly);
}

TEST(FieldTest, NonCopyableMembersAreNotSupportedNotReadOnly)
{
    const auto& cls = speculo::class_of<RefHolder>();
    int target = 1;
    const int constant = 2;
    std::unique_ptr<int> pointee = std::make_unique<int>(3);
    RefHolder holder{ target, constant, pointee, nullptr };

    // Reference to a move-only object: no copy in or out, but the view works.
    speculo::Field up = cls.get_field("up").unwrap();
    auto up_view = up.get_ref(holder);
    ASSERT_TRUE(up_view.is_ok());
    EXPECT_EQ(up_view.unwrap().type(), speculo::class_of<std::unique_ptr<int>>());
    EXPECT_EQ(*up_view.unwrap().as<std::unique_ptr<int>>().unwrap(), 3);
    EXPECT_EQ(up.get(holder).unwrap_err().kind, speculo::ErrorKind::NotSupported);

    // "Cannot copy-assign" is not "read-only".
    std::unique_ptr<int> other = std::make_unique<int>(4);
    EXPECT_EQ(up.set(holder, other).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    speculo::Field owned = cls.get_field("owned").unwrap();
    EXPECT_EQ(owned.set(holder, other).unwrap_err().kind, speculo::ErrorKind::NotSupported);
    ASSERT_NE(other, nullptr);
}

TEST(FieldTest, StaticFieldViewIgnoresInstanceConstness)
{
    Cache::shared = 0;
    const auto& cls = speculo::class_of<Cache>();
    speculo::Field shared = cls.get_field("shared").unwrap();
    ASSERT_TRUE(shared.is_static());

    const Cache cache{};
    speculo::ObjectRef const_instance(cache);

    // The constness of an unrelated instance does not make a static field const.
    auto view = shared.get_ref(const_instance);
    ASSERT_TRUE(view.is_ok());
    EXPECT_FALSE(view.unwrap().is_const());

    int seven = 7;
    EXPECT_TRUE(shared.set(const_instance, seven).is_ok());
    EXPECT_EQ(Cache::shared, 7);
}

TEST(MethodTest, InstanceInvokeAndVoid)
{
    const auto& cls = speculo::class_of<Geometry>();
    speculo::Method add = cls.get_method("add").unwrap();

    EXPECT_EQ(add.get_name(), "add");
    EXPECT_EQ(add.get_declaring_class(), cls);
    EXPECT_EQ(add.get_return_type(), speculo::class_of<int>());
    EXPECT_EQ(add.get_parameter_count(), 2u);
    EXPECT_TRUE(add.is_const());
    EXPECT_FALSE(add.is_static_member());
    EXPECT_FALSE(add.get_signature().empty());

    int external = 0;
    Geometry g(external);
    g.x = 5;

    int a = 2;
    int b = 3;
    auto result = add.invoke(g, { a, b });
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(std::move(result).unwrap().as<int>().unwrap(), 10);

    // Wrong argument count / type.
    EXPECT_EQ(add.invoke(g, { a }).unwrap_err().kind, speculo::ErrorKind::InvalidArgument);
    double wrong = 1.0;
    EXPECT_EQ(add.invoke(g, { a, wrong }).unwrap_err().kind, speculo::ErrorKind::TypeMismatch);

    // void return -> empty Object.
    speculo::Method bump = cls.get_method("bump").unwrap();
    int five = 5;
    auto bumped = bump.invoke(g, { five });
    ASSERT_TRUE(bumped.is_ok());
    EXPECT_TRUE(std::move(bumped).unwrap().is_null());
    EXPECT_EQ(g.x, 10);
}

TEST(MethodTest, StaticInvoke)
{
    const auto& cls = speculo::class_of<Geometry>();
    speculo::Method smul = cls.get_method("smul").unwrap();

    EXPECT_TRUE(smul.is_static_member());

    int a = 6;
    int b = 7;
    auto result = smul.invoke(speculo::ObjectRef{}, { a, b });
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(std::move(result).unwrap().as<int>().unwrap(), 42);
}

TEST(Phase3Example, MinimalApiDesignExample)
{
    const speculo::Class& cls = speculo::class_of<Point>();

    speculo::Field fx = cls.get_field("x").unwrap();
    speculo::Method fn = cls.get_method("norm").unwrap();

    Point p{3, 4};
    int ten = 10;
    ASSERT_TRUE(fx.set(p, ten).is_ok());

    auto invoked = fn.invoke(p, {});
    ASSERT_TRUE(invoked.is_ok());
    speculo::Object r = std::move(invoked).unwrap();
    double v = r.as<double>().unwrap();
    EXPECT_DOUBLE_EQ(v, 14.0);
}

TEST(DeclaredViewTest, PrivateAndProtectedMembers)
{
    const auto& cls = speculo::class_of<Secret>();

    // Public view only exposes public members.
    EXPECT_EQ(cls.get_fields().size(), 1u);
    EXPECT_EQ(cls.get_methods().size(), 1u);
    EXPECT_TRUE(cls.get_field("hidden").is_none());
    EXPECT_TRUE(cls.get_field("guarded").is_none());
    EXPECT_TRUE(cls.get_method("secret_method").is_err());

    // Declared view exposes private/protected members too.
    ASSERT_TRUE(cls.get_declared_field("hidden").is_some());
    ASSERT_TRUE(cls.get_declared_field("guarded").is_some());
    EXPECT_EQ(cls.get_declared_fields().size(), 3u);
    ASSERT_TRUE(cls.get_declared_method("secret_method").is_ok());

    Secret s;
    speculo::Field hidden = cls.get_declared_field("hidden").unwrap();
    EXPECT_TRUE(hidden.modifiers().is_private());
    EXPECT_EQ(hidden.get(s).unwrap().as<int>().unwrap(), 3);

    int five = 5;
    ASSERT_TRUE(hidden.set(s, five).is_ok());
    EXPECT_EQ(s.read_secret(), 7); // hidden(5) + guarded(2)

    // Invoking a private member function through the declared view works.
    speculo::Method secret = cls.get_declared_method("secret_method").unwrap();
    EXPECT_EQ(secret.modifiers().is_private(), true);
    EXPECT_EQ(secret.invoke(s, {}).unwrap().as<int>().unwrap(), 5);

    // The public method sees the new value too.
    speculo::Method read = cls.get_method("read_secret").unwrap();
    EXPECT_EQ(read.invoke(s, {}).unwrap().as<int>().unwrap(), 7);
}

TEST(InheritanceTest, SingleBaseNavigation)
{
    const speculo::Class& leaf = speculo::class_of<Leaf>();

    ASSERT_EQ(leaf.get_bases().size(), 1u);
    ASSERT_TRUE(leaf.get_superclass().is_some());
    EXPECT_EQ(leaf.get_superclass().unwrap(), speculo::class_of<Root>());

    // get_fields()/get_methods() are own-class only: inherited members are
    // reached by navigating to the base first.
    EXPECT_EQ(leaf.get_fields().size(), 1u);
    EXPECT_TRUE(leaf.get_field("r").is_none());
    EXPECT_TRUE(leaf.get_method("root_method").is_err());

    const speculo::Class& root = leaf.get_superclass().unwrap();
    ASSERT_TRUE(root.get_field("r").is_some());
    ASSERT_TRUE(root.get_method("root_method").is_ok());

    Root root_obj;
    root_obj.r = 9;
    EXPECT_EQ(root.get_field("r").unwrap().get(root_obj).unwrap().as<int>().unwrap(), 9);
    EXPECT_EQ(root.get_method("root_method").unwrap().invoke(root_obj, {}).unwrap().as<int>().unwrap(), 9);
}

TEST(InheritanceTest, NoBaseAndMultipleBases)
{
    EXPECT_TRUE(speculo::class_of<Root>().get_bases().empty());
    EXPECT_TRUE(speculo::class_of<Root>().get_superclass().is_none());

    const speculo::Class& multi = speculo::class_of<Multi>();
    ASSERT_EQ(multi.get_bases().size(), 2u);
    EXPECT_TRUE(multi.get_superclass().is_none()); // ambiguous: no unique superclass
    EXPECT_EQ(multi.get_bases()[0].type, std::addressof(speculo::class_of<Root>()));
    EXPECT_EQ(multi.get_bases()[1].type, std::addressof(speculo::class_of<Other>()));
    EXPECT_TRUE(multi.get_bases()[0].modifiers.is_public());
}

TEST(InheritanceTest, VirtualBaseIsNotSuperclass)
{
    const speculo::Class& vleaf = speculo::class_of<VLeaf>();
    ASSERT_EQ(vleaf.get_bases().size(), 1u);
    EXPECT_TRUE(vleaf.get_bases()[0].is_virtual());
    EXPECT_TRUE(vleaf.get_superclass().is_none());
}

TEST(InheritanceTest, GetSpecificBase)
{
    const speculo::Class& multi = speculo::class_of<Multi>();

    ASSERT_TRUE(multi.get_base<Root>().is_some());
    EXPECT_EQ(multi.get_base<Root>().unwrap(), speculo::class_of<Root>());
    EXPECT_EQ(multi.get_base<Other>().unwrap(), speculo::class_of<Other>());

    // Leaf is unrelated to Multi -> None.
    EXPECT_TRUE(multi.get_base<Leaf>().is_none());

    // By Class handle.
    EXPECT_EQ(multi.get_base(speculo::class_of<Other>()).unwrap(), speculo::class_of<Other>());
    EXPECT_TRUE(multi.get_base(speculo::class_of<Leaf>()).is_none());
}
