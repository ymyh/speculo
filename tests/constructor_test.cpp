#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/instance_fixtures.h"

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
