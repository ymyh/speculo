// S7 —— 类型谓词：哪些 std::meta 提供，哪些需要 std::type_traits 补。
#include <meta>

#include <cstddef>
#include <print>

#include "spike_util.hpp"

namespace m = std::meta;

struct Empty
{
};
struct Final final
{
};
struct Poly
{
    virtual ~Poly() = default;
};
struct Abstract
{
    virtual void f() = 0;
};
struct Agg
{
    int a;
    int b;
};
struct Plain
{
    int a = 1;
    void method() {}
};
struct NonAgg
{
    int a;
    explicit NonAgg(int x) : a(x) {}
};

enum E
{
    E0
};
enum class SE : short
{
    S0
};
union U
{
    int i;
    double d;
};

int main()
{
    // 一级分类
    static_assert(m::is_void_type(^^void));
    static_assert(m::is_fundamental_type(^^int));
    static_assert(m::is_fundamental_type(^^std::nullptr_t));
    static_assert(m::is_pointer_type(^^int*));
    static_assert(m::is_lvalue_reference_type(^^int&));
    static_assert(m::is_rvalue_reference_type(^^int&&));
    static_assert(m::is_array_type(^^int[3]));
    static_assert(m::is_bounded_array_type(^^int[3]));
    static_assert(m::is_unbounded_array_type(^^int[]));
    static_assert(m::is_enum_type(^^E));
    static_assert(m::is_scoped_enum_type(^^SE));
    static_assert(m::is_union_type(^^U));
    static_assert(m::is_class_type(^^Plain));
    static_assert(m::is_function_type(^^void(int)));
    static_assert(m::is_member_object_pointer_type(^^decltype(&Plain::a)));
    static_assert(m::is_member_function_pointer_type(^^decltype(&Plain::method)));

    // class 属性
    static_assert(m::is_polymorphic_type(^^Poly));
    static_assert(!m::is_polymorphic_type(^^Plain));
    static_assert(m::is_abstract_type(^^Abstract));
    static_assert(!m::is_abstract_type(^^Poly));
    static_assert(m::is_final_type(^^Final));
    static_assert(!m::is_final_type(^^Poly));
    static_assert(m::is_aggregate_type(^^Agg));
    static_assert(!m::is_aggregate_type(^^NonAgg));
    static_assert(m::is_empty_type(^^Empty));
    static_assert(!m::is_empty_type(^^Plain));
    static_assert(m::has_virtual_destructor(^^Poly));

    // cv
    static_assert(m::is_const_type(m::add_const(^^int)));
    static_assert(m::is_volatile_type(m::add_volatile(^^int)));
    static_assert(m::is_same_type(m::remove_cv(m::add_const(m::add_volatile(^^int))), ^^int));

    // 分解
    static_assert(m::is_same_type(m::remove_pointer(^^const int*), ^^const int));
    static_assert(m::is_same_type(m::remove_extent(^^int[3]), ^^int));
    static_assert(m::rank(^^int[2][3]) == 2);
    static_assert(m::extent(^^int[2][3]) == 2);
    static_assert(m::extent(^^int[2][3], 1) == 3);
    static_assert(m::is_same_type(m::remove_all_extents(^^int[2][3]), ^^int));

    // 关系
    static_assert(m::is_same_type(^^int, ^^int));
    static_assert(m::is_base_of_type(^^Poly, ^^Poly));
    static_assert(m::is_convertible_type(^^int, ^^double));

    std::println("S7 ok: all type predicates available");
}
