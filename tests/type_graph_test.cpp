#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/type_fixtures.h"
#include "support/type_graph_fixtures.h"

// Relation graph (api_design §15): registered derived classes, the upward
// conversion path, and virtual-method overriding.
//
// Value classes per .agents/testing.md §2, combination per §3. Every table below
// is a full cartesian product (all cells ≤ 64), so **every** cell is asserted by
// the loop next to it; there are no N/A cells — a receiver/target pair that is not
// related is an assertable `TypeMismatch`, not "undefined".
//
// ── Class::get_conversion_path(base): 6 receivers × 8 targets = 48 cells ──
// cell = path length (Ok) or the promised ErrorKind
// receiver \ target | Circle | Shape | LeftRoot | VTop      | NTop      | HiddenRoot   | ProtectedRoot | Unrelated
// tg::Ball          | 2      | 3     | Mismatch | Mismatch  | Mismatch  | Mismatch     | Mismatch      | Mismatch
// tg::Circle        | 1      | 2     | Mismatch | Mismatch  | Mismatch  | Mismatch     | Mismatch      | Mismatch
// tg::VBottom       | Mismatch | Mismatch | Mismatch | 3  | Mismatch  | Mismatch     | Mismatch      | Mismatch
// tg::NBottom       | Mismatch | Mismatch | Mismatch | Mismatch | Ambiguous | Mismatch   | Mismatch      | Mismatch
// tg::Both          | Mismatch | Mismatch | 2     | Mismatch  | Mismatch  | Mismatch     | Mismatch      | Mismatch
// tg::Closed        | Mismatch | Mismatch | Mismatch | Mismatch | Mismatch | NotSupported | NotSupported | Mismatch
//
// Plus the identity column (receiver -> itself, 1 hop) for all 6 receivers, and the
// empty-handle row (either side empty -> panic, not an Err).
//
// Edge case outside the `tg` matrix, from the shared fixtures: MixedBottom reaches
// DiamondBase once through a virtual base and once through a plain one -> the two
// routes end at different subobjects, so the answer stays Ambiguous (merging only
// happens when *every* route enters the same virtual base).
//
// Value-class notes (testing.md §2), so the N/A cells are stated rather than skipped:
//   V4 视图修饰 —— N/A for every API in this file. `Class` / `Method` are 8/16-byte
//   read-only handles with no const/volatile view; only `ObjectRef` has views, and
//   none of these APIs takes one.
//   V3 空 —— a default-constructed handle panics (precondition), asserted with
//   EXPECT_DEATH in the four `*OnAnEmptyHandlePanics` / `*Panics` cases.
//
// ── Class::get_derived_classes / get_all_derived_classes: 4 type forms × 2 queries ──
// type form   | get_derived_classes() (direct)            | get_all_derived_classes() (level order)
// tg::Shape   | {Circle, ConstMismatch, Sizer}            | + {Ball} (grandchild, after its parent)
// tg::Ball    | {} (leaf)                                 | {}
// tg::Both    | {} (no registered child)                  | {}
// int         | {} (not a class)                          | {}
// A registered descendant that was never materialized (tg::Unregistered) must be
// absent from both queries: the reverse index only knows what the registry knows.
//
// ── Class::is_derived_from(base): 10 relations ──
// from            | base             | expected | why
// tg::Ball        | tg::Circle       | true     | direct public base
// tg::Ball        | tg::Shape        | true     | transitive
// tg::Circle      | tg::Shape        | true     | direct public base
// tg::NBottom     | tg::NTop         | true     | public base, even though the path is ambiguous
// tg::Shape       | tg::Ball         | false    | reversed (no down-cast)
// tg::Ball        | tg::Ball         | false    | identity is `==`, not derivation
// tg::Closed      | tg::HiddenRoot   | false    | private base
// tg::Closed      | tg::ProtectedRoot| false    | protected base
// tg::Both        | tg::Unrelated    | false    | unrelated
// tg::Shape       | tg::Unrelated    | false    | unrelated (root side)
// Empty handle on either side -> panic (V3), asserted with EXPECT_DEATH.
//
// ── Method::get_overridden_method / get_overrider_methods: 10 method forms × 2 ──
// method                | get_overridden_method() | get_overrider_methods() (sorted)
// tg::Shape::area       | None (chain root)        | {Ball::area, Circle::area}
// tg::Shape::resize     | None                    | {Circle::resize}
// tg::Shape::hidden     | None                    | {}  (non-virtual)
// tg::Circle::area      | Shape                   | {Ball::area}
// tg::Circle::hidden    | None (own is not virtual)| {}
// tg::Ball::area        | Circle (nearest)        | {}
// tg::Plain::compute    | None                    | {}  (PlainChild::compute is not virtual)
// tg::PlainChild::compute | None                  | {}
// tg::Sizer::area       | None (different params) | {}
// tg::ConstMismatch::area | None (different cv)   | {}

namespace
{

using speculo::Class;
using speculo::ErrorKind;
using speculo::Method;
using speculo::None;
using speculo::Option;
using speculo::Outcome;

/// One cell of the conversion-path matrix.
struct PathCell
{
    bool          ok = false;
    ErrorKind     error = ErrorKind::TypeMismatch;
    std::size_t   length = 0;
};

[[nodiscard]] constexpr auto ok_path(std::size_t length) -> PathCell
{
    return PathCell{true, ErrorKind::TypeMismatch, length};
}

[[nodiscard]] constexpr auto failed_path(ErrorKind kind) -> PathCell
{
    return PathCell{false, kind, 0};
}

[[nodiscard]] auto declared_method(const Class& cls, std::string_view name) -> Option<Method>
{
    for (const Method& method : cls.get_declared_methods())
    {
        if (method.get_name() == name)
        {
            return method;
        }
    }

    return None{};
}

/// A declared method that must exist; the empty handle panics otherwise.
[[nodiscard]] auto method_of(const Class& cls, std::string_view name) -> Method
{
    const Option<Method> found = declared_method(cls, name);
    EXPECT_TRUE(found.is_some());
    return found.is_some() ? found.unwrap() : Method{};
}

[[nodiscard]] auto simple_names(const std::vector<const Class*>& classes) -> std::vector<std::string>
{
    std::vector<std::string> names;
    names.reserve(classes.size());
    for (const Class* cls : classes)
    {
        names.emplace_back(cls == nullptr ? "<null>" : cls->get_simple_name());
    }

    std::ranges::sort(names);
    return names;
}

[[nodiscard]] auto method_names(const std::vector<Method>& methods) -> std::vector<std::string>
{
    std::vector<std::string> names;
    names.reserve(methods.size());
    for (const Method& method : methods)
    {
        names.push_back(std::format("{}::{}", method.get_declaring_class().get_simple_name(), method.get_name()));
    }

    std::ranges::sort(names);
    return names;
}

void expect_path(const std::vector<const Class*>& path, const std::vector<const Class*>& expected)
{
    ASSERT_EQ(path.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        SCOPED_TRACE(std::format("hop {}", i));
        EXPECT_EQ(path[i], expected[i]);
    }
}

} // namespace

/**
 * @brief Shared setup for the relation matrices
 *
 * Registration is what fills the reverse edge index, so every type a table
 * mentions is materialized here: the tests then hold no matter in which order
 * gtest runs them (testing.md §8). `tg::Unregistered` is deliberately **not**
 * materialized — it is the negative case.
 */
class GraphMatrix : public ::testing::Test
{
protected:
    GraphMatrix()
    {
        (void)speculo::class_of<tg::Shape>();
        (void)speculo::class_of<tg::Circle>();
        (void)speculo::class_of<tg::Ball>();
        (void)speculo::class_of<tg::Sizer>();
        (void)speculo::class_of<tg::ConstMismatch>();
        (void)speculo::class_of<tg::Plain>();
        (void)speculo::class_of<tg::PlainChild>();
        (void)speculo::class_of<tg::Both>();
        (void)speculo::class_of<tg::VBottom>();
        (void)speculo::class_of<tg::NBottom>();
        (void)speculo::class_of<tg::Closed>();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Class::get_conversion_path
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(GraphMatrix, ConversionPathMatrixCoversEveryReceiverTargetPair)
{
    const std::array<const char*, 6> receiver_names{ "tg::Ball", "tg::Circle", "tg::VBottom",
                                                     "tg::NBottom", "tg::Both", "tg::Closed" };
    const std::array<const Class*, 6> receivers{ &speculo::class_of<tg::Ball>(), &speculo::class_of<tg::Circle>(),
                                                 &speculo::class_of<tg::VBottom>(),
                                                 &speculo::class_of<tg::NBottom>(), &speculo::class_of<tg::Both>(),
                                                 &speculo::class_of<tg::Closed>() };
    const std::array<const Class*, 8> targets{
        &speculo::class_of<tg::Circle>(),        &speculo::class_of<tg::Shape>(),
        &speculo::class_of<tg::LeftRoot>(),      &speculo::class_of<tg::VTop>(),
        &speculo::class_of<tg::NTop>(),          &speculo::class_of<tg::HiddenRoot>(),
        &speculo::class_of<tg::ProtectedRoot>(), &speculo::class_of<tg::Unrelated>(),
    };

    const PathCell ok1 = ok_path(1);
    const PathCell ok2 = ok_path(2);
    const PathCell ok3 = ok_path(3);
    const PathCell mismatch = failed_path(ErrorKind::TypeMismatch);
    const PathCell ambiguous = failed_path(ErrorKind::Ambiguous);
    const PathCell inaccessible = failed_path(ErrorKind::NotSupported);

    // Rows in the same order as receiver_names, columns as in the header table.
    const std::array<std::array<PathCell, 8>, 6> expected{ {
        { ok2, ok3, mismatch, mismatch, mismatch, mismatch, mismatch, mismatch },            // tg::Ball
        { ok1, ok2, mismatch, mismatch, mismatch, mismatch, mismatch, mismatch },            // tg::Circle
        { mismatch, mismatch, mismatch, ok3, mismatch, mismatch, mismatch, mismatch },       // tg::VBottom
        { mismatch, mismatch, mismatch, mismatch, ambiguous, mismatch, mismatch, mismatch }, // tg::NBottom
        { mismatch, mismatch, ok2, mismatch, mismatch, mismatch, mismatch, mismatch },       // tg::Both
        { mismatch, mismatch, mismatch, mismatch, mismatch, inaccessible, inaccessible, mismatch }, // tg::Closed
    } };

    for (std::size_t row = 0; row < receivers.size(); ++row)
    {
        for (std::size_t column = 0; column < targets.size(); ++column)
        {
            SCOPED_TRACE(std::format("{} -> {}", receiver_names[row], targets[column]->get_simple_name()));
            const PathCell& cell = expected[row][column];
            const Outcome<std::vector<const Class*>> path = receivers[row]->get_conversion_path(*targets[column]);

            if (cell.ok)
            {
                ASSERT_TRUE(path.is_ok());
                const std::vector<const Class*>& hops = path.unwrap();
                EXPECT_EQ(hops.size(), cell.length);
                EXPECT_EQ(hops.front(), receivers[row]);
                EXPECT_EQ(hops.back(), targets[column]);
                continue;
            }

            ASSERT_TRUE(path.is_err());
            EXPECT_EQ(path.unwrap_err().kind, cell.error);
        }
    }
}

TEST_F(GraphMatrix, ConversionPathIdentityIsOneHop)
{
    const std::array<const char*, 6> names{ "tg::Ball", "tg::Circle", "tg::VBottom",
                                            "tg::NBottom", "tg::Both", "tg::Closed" };
    const std::array<const Class*, 6> types{ &speculo::class_of<tg::Ball>(), &speculo::class_of<tg::Circle>(),
                                             &speculo::class_of<tg::VBottom>(), &speculo::class_of<tg::NBottom>(),
                                             &speculo::class_of<tg::Both>(), &speculo::class_of<tg::Closed>() };

    for (std::size_t i = 0; i < types.size(); ++i)
    {
        SCOPED_TRACE(names[i]);
        const auto path = types[i]->get_conversion_path(*types[i]);
        ASSERT_TRUE(path.is_ok());
        expect_path(path.unwrap(), { types[i] });
    }
}

TEST_F(GraphMatrix, ConversionPathReportsTheWholeChainInOrder)
{
    // The matrix asserts length and endpoints; this pins the elements in between,
    // including the tie-break rule for equal-length virtual routes (declaration
    // order of get_bases(), api_design §15.2).
    const Class& ball = speculo::class_of<tg::Ball>();
    const Class& circle = speculo::class_of<tg::Circle>();
    const Class& shape = speculo::class_of<tg::Shape>();
    expect_path(ball.get_conversion_path(shape).unwrap(), { &ball, &circle, &shape });

    const Class& bottom = speculo::class_of<tg::VBottom>();
    const Class& left = speculo::class_of<tg::VLeft>();
    const Class& top = speculo::class_of<tg::VTop>();
    expect_path(bottom.get_conversion_path(top).unwrap(), { &bottom, &left, &top });

    const Class& both = speculo::class_of<tg::Both>();
    const Class& left_root = speculo::class_of<tg::LeftRoot>();
    expect_path(both.get_conversion_path(left_root).unwrap(), { &both, &left_root });
}

TEST_F(GraphMatrix, ConversionPathElementsAreCanonicalHandles)
{
    // The returned pointers must stay valid after the receiver handle is gone:
    // they are the handles owned by the descriptor tables, not pointers into the
    // caller's temporary (api_design §15.2).
    const std::vector<const Class*> path =
        speculo::class_of<tg::Ball>().get_conversion_path(speculo::class_of<tg::Shape>()).unwrap();
    ASSERT_EQ(path.size(), 3);
    EXPECT_EQ(path[0], std::addressof(speculo::class_of<tg::Ball>()));
    EXPECT_EQ(path[1], std::addressof(speculo::class_of<tg::Circle>()));
    EXPECT_EQ(path[2], std::addressof(speculo::class_of<tg::Shape>()));
}

TEST_F(GraphMatrix, ConversionPathOnAnEmptyHandlePanics)
{
    EXPECT_DEATH((void)speculo::Class{}.get_conversion_path(speculo::class_of<tg::Shape>()), "");
    EXPECT_DEATH((void)speculo::class_of<tg::Shape>().get_conversion_path(speculo::Class{}), "");
}

TEST_F(GraphMatrix, MixedVirtualAndPlainRoutesStayAmbiguous)
{
    // A third shape of diamond, from the shared fixtures: one route enters
    // DiamondBase virtually, the other does not, so the two routes end at
    // *different* DiamondBase subobjects. Merging only applies when every route
    // enters the same virtual base (api_design §15.2), so this stays Ambiguous --
    // the same answer cast_to() gives by comparing addresses.
    const auto path = speculo::class_of<MixedBottom>().get_conversion_path(speculo::class_of<DiamondBase>());
    ASSERT_TRUE(path.is_err());
    EXPECT_EQ(path.unwrap_err().kind, ErrorKind::Ambiguous);
}

// ─────────────────────────────────────────────────────────────────────────────
// Class::get_derived_classes / get_all_derived_classes
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(GraphMatrix, DerivedClassMatrixCoversEveryTypeForm)
{
    struct Row
    {
        const char*                    type;
        const Class*                   cls;
        std::vector<std::string>       direct;
        std::vector<std::string>       all;
    };

    const std::array<Row, 4> rows{
        Row{ "tg::Shape", &speculo::class_of<tg::Shape>(), { "Circle", "ConstMismatch", "Sizer" },
             { "Ball", "Circle", "ConstMismatch", "Sizer" } },
        Row{ "tg::Ball", &speculo::class_of<tg::Ball>(), {}, {} },
        Row{ "tg::Both", &speculo::class_of<tg::Both>(), {}, {} },
        Row{ "int", &speculo::class_of<int>(), {}, {} },
    };

    for (const Row& row : rows)
    {
        SCOPED_TRACE(row.type);
        EXPECT_EQ(simple_names(row.cls->get_derived_classes()), row.direct);
        EXPECT_EQ(simple_names(row.cls->get_all_derived_classes()), row.all);
    }
}

TEST_F(GraphMatrix, AllDerivedClassesAreInLevelOrder)
{
    const std::vector<const Class*> all = speculo::class_of<tg::Shape>().get_all_derived_classes();
    const auto grandchild = std::ranges::find_if(all, [](const Class* cls) { return cls->get_simple_name() == "Ball"; });
    const auto child = std::ranges::find_if(all, [](const Class* cls) { return cls->get_simple_name() == "Circle"; });

    ASSERT_NE(grandchild, all.end());
    ASSERT_NE(child, all.end());
    EXPECT_LT(std::distance(all.begin(), child), std::distance(all.begin(), grandchild));
}

TEST_F(GraphMatrix, UnregisteredDerivedClassIsInvisible)
{
    // tg::Unregistered derives from Shape in C++ terms, but nothing ever asked for
    // its Class, so the reverse index cannot know about it (api_design §15.2).
    const std::vector<std::string> direct = simple_names(speculo::class_of<tg::Shape>().get_derived_classes());
    const std::vector<std::string> all = simple_names(speculo::class_of<tg::Shape>().get_all_derived_classes());

    EXPECT_EQ(std::find(direct.begin(), direct.end(), "Unregistered"), direct.end());
    EXPECT_EQ(std::find(all.begin(), all.end(), "Unregistered"), all.end());
}

TEST_F(GraphMatrix, DerivedClassQueriesOnAnEmptyHandlePanic)
{
    EXPECT_DEATH((void)speculo::Class{}.get_derived_classes(), "");
    EXPECT_DEATH((void)speculo::Class{}.get_all_derived_classes(), "");
}

// ─────────────────────────────────────────────────────────────────────────────
// Class::is_derived_from
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(GraphMatrix, IsDerivedFromMatrixCoversEveryRelation)
{
    struct Row
    {
        const char* from;
        const char* to;
        const Class* from_cls;
        const Class* to_cls;
        bool         expected;
    };

    const std::array<Row, 10> rows{
        Row{ "tg::Ball", "tg::Circle", &speculo::class_of<tg::Ball>(), &speculo::class_of<tg::Circle>(), true },
        Row{ "tg::Ball", "tg::Shape", &speculo::class_of<tg::Ball>(), &speculo::class_of<tg::Shape>(), true },
        Row{ "tg::Circle", "tg::Shape", &speculo::class_of<tg::Circle>(), &speculo::class_of<tg::Shape>(), true },
        Row{ "tg::NBottom", "tg::NTop", &speculo::class_of<tg::NBottom>(), &speculo::class_of<tg::NTop>(), true },
        Row{ "tg::Shape", "tg::Ball", &speculo::class_of<tg::Shape>(), &speculo::class_of<tg::Ball>(), false },
        Row{ "tg::Ball", "tg::Ball", &speculo::class_of<tg::Ball>(), &speculo::class_of<tg::Ball>(), false },
        Row{ "tg::Closed", "tg::HiddenRoot", &speculo::class_of<tg::Closed>(),
             &speculo::class_of<tg::HiddenRoot>(), false },
        Row{ "tg::Closed", "tg::ProtectedRoot", &speculo::class_of<tg::Closed>(),
             &speculo::class_of<tg::ProtectedRoot>(), false },
        Row{ "tg::Both", "tg::Unrelated", &speculo::class_of<tg::Both>(), &speculo::class_of<tg::Unrelated>(), false },
        Row{ "tg::Shape", "tg::Unrelated", &speculo::class_of<tg::Shape>(), &speculo::class_of<tg::Unrelated>(), false },
    };

    for (const Row& row : rows)
    {
        SCOPED_TRACE(std::format("{} -> {}", row.from, row.to));
        EXPECT_EQ(row.from_cls->is_derived_from(*row.to_cls), row.expected);
    }
}

TEST_F(GraphMatrix, IsDerivedFromOnAnEmptyHandlePanics)
{
    EXPECT_DEATH((void)speculo::Class{}.is_derived_from(speculo::class_of<tg::Shape>()), "");
    EXPECT_DEATH((void)speculo::class_of<tg::Ball>().is_derived_from(speculo::Class{}), "");
}

// ─────────────────────────────────────────────────────────────────────────────
// Method::get_overridden_method / get_overrider_methods
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(GraphMatrix, OverrideMatrixCoversEveryMethodForm)
{
    struct Row
    {
        const char*              name;       ///< row label, also the SCOPED_TRACE text
        Method                   method;
        const char*              overridden; ///< empty: None; else the declaring class of the answer
        std::vector<std::string> overriders;
    };

    const std::array<Row, 10> rows{
        Row{ "Shape::area", method_of(speculo::class_of<tg::Shape>(), "area"), "",
             { "Ball::area", "Circle::area" } },
        Row{ "Shape::resize", method_of(speculo::class_of<tg::Shape>(), "resize"), "", { "Circle::resize" } },
        Row{ "Shape::hidden", method_of(speculo::class_of<tg::Shape>(), "hidden"), "", {} },
        Row{ "Circle::area", method_of(speculo::class_of<tg::Circle>(), "area"), "Shape", { "Ball::area" } },
        Row{ "Circle::hidden", method_of(speculo::class_of<tg::Circle>(), "hidden"), "", {} },
        Row{ "Ball::area", method_of(speculo::class_of<tg::Ball>(), "area"), "Circle", {} },
        Row{ "Plain::compute", method_of(speculo::class_of<tg::Plain>(), "compute"), "", {} },
        Row{ "PlainChild::compute", method_of(speculo::class_of<tg::PlainChild>(), "compute"), "", {} },
        Row{ "Sizer::area", method_of(speculo::class_of<tg::Sizer>(), "area"), "", {} },
        Row{ "ConstMismatch::area", method_of(speculo::class_of<tg::ConstMismatch>(), "area"), "", {} },
    };

    for (const Row& row : rows)
    {
        SCOPED_TRACE(row.name);

        const Option<Method> overridden = row.method.get_overridden_method();
        if (row.overridden[0] == '\0')
        {
            EXPECT_TRUE(overridden.is_none());
        }
        else
        {
            ASSERT_TRUE(overridden.is_some());
            EXPECT_EQ(overridden.unwrap().get_declaring_class().get_simple_name(), row.overridden);
            EXPECT_EQ(overridden.unwrap().get_name(), row.method.get_name());
        }

        EXPECT_EQ(method_names(row.method.get_overrider_methods()), row.overriders);
    }
}

TEST_F(GraphMatrix, OverriddenMethodWalksTheChainOneHopAtATime)
{
    // Walking up from the most derived override reaches the root declaration and
    // then stops (None), one hop per call.
    const Method ball_area = method_of(speculo::class_of<tg::Ball>(), "area");
    const Option<Method> middle = ball_area.get_overridden_method();
    ASSERT_TRUE(middle.is_some());
    EXPECT_EQ(middle.unwrap(), method_of(speculo::class_of<tg::Circle>(), "area"));

    const Option<Method> root = middle.unwrap().get_overridden_method();
    ASSERT_TRUE(root.is_some());
    EXPECT_EQ(root.unwrap(), method_of(speculo::class_of<tg::Shape>(), "area"));
    EXPECT_TRUE(root.unwrap().get_overridden_method().is_none());
}

TEST_F(GraphMatrix, OverriderHandlesAreUsableMethods)
{
    // The returned handles are real Method handles: declaring class, signature and
    // a working invoker.
    const std::vector<Method> overriders =
        method_of(speculo::class_of<tg::Shape>(), "resize").get_overrider_methods();
    ASSERT_EQ(overriders.size(), 1);

    tg::Circle circle;
    speculo::ObjectRef ref(circle);
    ASSERT_TRUE(overriders.front().invoke(ref, { 4 }).is_ok());
    EXPECT_EQ(circle.size, 8); // Circle::resize doubles the value
}

TEST_F(GraphMatrix, OverrideQueriesOnAnEmptyHandlePanic)
{
    EXPECT_DEATH((void)speculo::Method{}.get_overridden_method(), "");
    EXPECT_DEATH((void)speculo::Method{}.get_overrider_methods(), "");
}
