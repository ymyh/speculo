#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/class_fixtures.h"
#include "support/instance_fixtures.h"

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

namespace
{

struct Quiet
{
    auto hum() const noexcept -> int { return 1; }
    auto talk() const -> int { return 2; }
};

} // namespace

TEST(MethodTest, QualifierPredicates)
{
    // get_ref_qualifier() reports the declaration; MemberQualifiers is only the
    // lookup key.
    const speculo::Class& qualified = speculo::class_of<Qualified>();
    const speculo::Class* int_cls = std::addressof(speculo::class_of<int>());

    speculo::Method lvalue =
        qualified.get_method("g", {}, speculo::MemberQualifiers{ .ref = speculo::RefQualifier::Lvalue }).unwrap();
    speculo::Method rvalue =
        qualified.get_method("g", {}, speculo::MemberQualifiers{ .ref = speculo::RefQualifier::Rvalue }).unwrap();
    EXPECT_EQ(lvalue.get_ref_qualifier(), speculo::RefQualifier::Lvalue);
    EXPECT_EQ(rvalue.get_ref_qualifier(), speculo::RefQualifier::Rvalue);

    speculo::Method plain = qualified.get_method("f", { int_cls }, speculo::MemberQualifiers{}).unwrap();
    EXPECT_EQ(plain.get_ref_qualifier(), speculo::RefQualifier::None);
    EXPECT_FALSE(plain.is_volatile());
    EXPECT_FALSE(plain.is_noexcept());

    // volatile / cv-qualified members.
    const speculo::Class& reg = speculo::class_of<Register>();
    speculo::Method read = reg.get_method("read").unwrap();
    EXPECT_TRUE(read.is_volatile());
    EXPECT_FALSE(read.is_const());

    speculo::Method peek = reg.get_method("peek").unwrap();
    EXPECT_TRUE(peek.is_volatile());
    EXPECT_TRUE(peek.is_const());

    speculo::Method write = reg.get_method("write").unwrap();
    EXPECT_FALSE(write.is_volatile());
    EXPECT_FALSE(write.is_noexcept());

    // noexcept members.
    const speculo::Class& quiet = speculo::class_of<Quiet>();
    EXPECT_TRUE(quiet.get_method("hum").unwrap().is_noexcept());
    EXPECT_FALSE(quiet.get_method("talk").unwrap().is_noexcept());
}

// ─────────────────────────────────────────────────────────────────────────────
// invoke matrix: receiver form × argument form
//
// Every cell of the 5 × 5 product is asserted (tables generated by
// tools/gen_matrix.py), so the error precedence of api_design.md §0.1 is pinned
// rather than sampled:
//   count -> receiver null -> receiver type -> const -> volatile -> arguments
// ─────────────────────────────────────────────────────────────────────────────

namespace
{

/// Non-const receiver with one `int&` parameter: binds mutable lvalues only.
struct MatrixTarget
{
    int value = 0;

    auto bump(int& delta) -> int
    {
        value += delta;
        return value;
    }
};

struct MatrixOther
{
    int value = 0;
};

enum class ReceiverForm { Normal, Const, Volatile, WrongType, Null };
enum class ArgumentForm { Match, Empty, Extra, WrongType, ConstForMutableRef };
enum class Expected { Ok, InvalidArgument, NullInstance, TypeMismatch, ReadOnly, NotSupported };

constexpr std::array<ReceiverForm, 5> kReceiverForms{
    ReceiverForm::Normal,
    ReceiverForm::Const,
    ReceiverForm::Volatile,
    ReceiverForm::WrongType,
    ReceiverForm::Null,
};

constexpr std::array<ArgumentForm, 5> kArgumentForms{
    ArgumentForm::Match,
    ArgumentForm::Empty,
    ArgumentForm::Extra,
    ArgumentForm::WrongType,
    ArgumentForm::ConstForMutableRef,
};

// 5 x 5 = 25 cells.
//                    Match                Empty                 Extra                 WrongType             ConstForMutableRef
constexpr std::array<std::array<Expected, 5>, 5> kInvokeMatrix{{
    /* Normal    */ { Expected::Ok,             Expected::InvalidArgument, Expected::InvalidArgument, Expected::TypeMismatch, Expected::TypeMismatch },
    /* Const     */ { Expected::ReadOnly,       Expected::InvalidArgument, Expected::InvalidArgument, Expected::ReadOnly,     Expected::ReadOnly },
    /* Volatile  */ { Expected::NotSupported,   Expected::InvalidArgument, Expected::InvalidArgument, Expected::NotSupported, Expected::NotSupported },
    /* WrongType */ { Expected::TypeMismatch,   Expected::InvalidArgument, Expected::InvalidArgument, Expected::TypeMismatch, Expected::TypeMismatch },
    /* Null      */ { Expected::NullInstance,   Expected::InvalidArgument, Expected::InvalidArgument, Expected::NullInstance, Expected::NullInstance },
}};

[[nodiscard]] constexpr auto form_name(ReceiverForm form) -> std::string_view
{
    switch (form)
    {
        case ReceiverForm::Normal:    return "normal";
        case ReceiverForm::Const:     return "const";
        case ReceiverForm::Volatile:  return "volatile";
        case ReceiverForm::WrongType: return "wrong-type";
        case ReceiverForm::Null:      return "null";
    }

    return "?";
}

[[nodiscard]] constexpr auto form_name(ArgumentForm form) -> std::string_view
{
    switch (form)
    {
        case ArgumentForm::Match:              return "match";
        case ArgumentForm::Empty:              return "empty";
        case ArgumentForm::Extra:              return "extra";
        case ArgumentForm::WrongType:          return "wrong-type";
        case ArgumentForm::ConstForMutableRef: return "const-for-mutable-ref";
    }

    return "?";
}

[[nodiscard]] constexpr auto expected_kind(Expected expected) -> speculo::ErrorKind
{
    switch (expected)
    {
        case Expected::Ok:              return speculo::ErrorKind::NotFound;
        case Expected::InvalidArgument: return speculo::ErrorKind::InvalidArgument;
        case Expected::NullInstance:    return speculo::ErrorKind::NullInstance;
        case Expected::TypeMismatch:    return speculo::ErrorKind::TypeMismatch;
        case Expected::ReadOnly:        return speculo::ErrorKind::ReadOnly;
        case Expected::NotSupported:    return speculo::ErrorKind::NotSupported;
    }

    return speculo::ErrorKind::NotFound;
}

[[nodiscard]] auto make_receiver(ReceiverForm form, MatrixTarget& target, const MatrixTarget& const_target,
                                 volatile MatrixTarget& volatile_target, MatrixOther& other) -> speculo::ObjectRef
{
    switch (form)
    {
        case ReceiverForm::Normal:    return speculo::ObjectRef(target);
        case ReceiverForm::Const:     return speculo::ObjectRef(const_target);
        case ReceiverForm::Volatile:  return speculo::ObjectRef(volatile_target);
        case ReceiverForm::WrongType: return speculo::ObjectRef(other);
        case ReceiverForm::Null:      return speculo::ObjectRef{};
    }

    std::abort();
}

[[nodiscard]] auto make_arguments(ArgumentForm form, int& delta, double& wrong, const int& frozen) -> speculo::Args
{
    switch (form)
    {
        case ArgumentForm::Match:              return speculo::Args{ delta };
        case ArgumentForm::Empty:              return speculo::Args{};
        case ArgumentForm::Extra:              return speculo::Args{ delta, delta };
        case ArgumentForm::WrongType:          return speculo::Args{ wrong };
        case ArgumentForm::ConstForMutableRef: return speculo::Args{ frozen };
    }

    std::abort();
}

} // namespace

TEST(MethodInvokeMatrix, ReceiverByArgumentForm)
{
    const speculo::Class& cls = speculo::class_of<MatrixTarget>();
    speculo::Method bump = cls.get_method<int&>("bump").unwrap();

    MatrixTarget target;
    const MatrixTarget const_target{};
    volatile MatrixTarget volatile_target{};
    MatrixOther other;

    int delta = 1;
    double wrong = 1.0;
    const int frozen = 1;

    for (std::size_t row = 0; row < kReceiverForms.size(); ++row)
    {
        for (std::size_t column = 0; column < kArgumentForms.size(); ++column)
        {
            const ReceiverForm receiver_form = kReceiverForms[row];
            const ArgumentForm argument_form = kArgumentForms[column];
            SCOPED_TRACE(std::format("{}/{}", form_name(receiver_form), form_name(argument_form)));

            target.value = 0;
            speculo::ObjectRef receiver =
                make_receiver(receiver_form, target, const_target, volatile_target, other);
            auto result = bump.invoke(receiver, make_arguments(argument_form, delta, wrong, frozen));

            const Expected expected = kInvokeMatrix[row][column];
            if (expected == Expected::Ok)
            {
                ASSERT_TRUE(result.is_ok());
                EXPECT_EQ(std::move(result).unwrap().as<int>().unwrap(), 1);
                EXPECT_EQ(target.value, 1);
            }
            else
            {
                ASSERT_TRUE(result.is_err());
                EXPECT_EQ(result.unwrap_err().kind, expected_kind(expected));
            }
        }
    }
}

TEST(MethodTest, EmptyHandlePanicsOnUse)
{
    const speculo::Method empty{};

    EXPECT_TRUE(empty == speculo::Method{});
    EXPECT_DEATH((void)empty.get_name(), "default-constructed");
    EXPECT_DEATH((void)empty.get_signature(), "default-constructed");
}
