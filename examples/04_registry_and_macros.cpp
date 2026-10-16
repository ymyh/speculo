// 04 — 注册表与宏：for_name、SPECULO_REGISTER、SPECULO_IMPL(_VIRTUAL)
//
// 展示「按类型名查找」的第三级路径，以及三个便利宏。

#include <speculo/macros.h>

import std;
import crab_cpp;
import speculo;

namespace demo
{

struct Engine
{
    int power = 100;
    int boost(int by) const { return power + by; }
};

struct Wheel
{
    int diameter = 17;
};

} // namespace demo

// 只按名字使用、从不 class_of<Wheel>()：用宏在静态初始化期注册。
SPECULO_REGISTER(demo::Wheel);

// SPECULO_IMPL：附加 get_class()，不引入虚表、不破坏聚合体。
struct Point
{
    SPECULO_IMPL(Point)
    int x = 0;
    int y = 0;
};

// SPECULO_IMPL_VIRTUAL：多态链统一使用，经基类引用得到动态类型。
struct Shape
{
    SPECULO_IMPL_VIRTUAL(Shape)
    virtual ~Shape() = default;
};

struct Circle : Shape
{
    SPECULO_IMPL_VIRTUAL(Circle)
    double radius = 1.0;
};

int main()
{
    // ── 自动注册：class_of<T>() 首次调用即注册 ──
    std::println("== registry ==");
    const speculo::Class& engine = speculo::class_of<demo::Engine>();
    auto by_name = speculo::Class::for_name("demo::Engine");
    std::println("  for_name(demo::Engine) ok={} same_handle={}",
                 by_name.is_some(), by_name.unwrap() == engine);

    // ── 宏注册：未调用 class_of<demo::Wheel>() ──
    auto wheel = speculo::Class::for_name("demo::Wheel");
    std::println("  for_name(demo::Wheel) ok={} (SPECULO_REGISTER)", wheel.is_some());

    // ── 类型名版方法查找 ──
    (void)speculo::class_of<int>();
    speculo::Method boost = engine.get_method("boost", { "int" }).unwrap();
    demo::Engine e;
    int five = 5;
    std::println("  get_method(\"boost\", {{\"int\"}}).invoke(5) = {}",
                 boost.invoke(e, { five }).unwrap().as<int>().unwrap());

    // ── SPECULO_IMPL ──
    std::println("== macros ==");
    Point pt{ 3, 4 };
    std::println("  Point{{3,4}} still aggregate: size={} get_class()={}",
                 sizeof(Point), pt.get_class().get_name());

    // ── SPECULO_IMPL_VIRTUAL ──
    Circle c;
    Shape& s = c;
    std::println("  via Shape& -> get_class() = {} (dynamic type)", s.get_class().get_name());

    return 0;
}
