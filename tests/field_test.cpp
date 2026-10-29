#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/class_fixtures.h"
#include "support/instance_fixtures.h"

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

TEST(FieldLookupTest, TypeChecked)
{
    const speculo::Class& cls = speculo::class_of<Vec>();

    EXPECT_TRUE(cls.get_field<int>("x").is_some());
    EXPECT_TRUE(cls.get_field<long>("x").is_none());
    EXPECT_TRUE(cls.get_field<int>("missing").is_none());
    EXPECT_TRUE(cls.get_declared_field<int>("y").is_some());
}
