// 01 — 基础：类型身份、布局信息、字段读写、方法调用
//
// 对应 api_design.md §0 的最小示例，展示 Java 风格的 Class/Field/Method
// 以及 C++ 特有的 sizeof / 对齐 / 字节偏移。

import std;
import crab_cpp;
import speculo;

struct Point
{
    int x = 0;
    int y = 0;

    double norm() const
    {
        return std::sqrt(static_cast<double>(x * x + y * y));
    }
};

int main()
{
    const speculo::Class& cls = speculo::class_of<Point>();

    std::println("== {} ==", cls.get_qualified_name());
    std::println("  kind class={} aggregate={} polymorphic={}",
                 cls.is_class(), cls.is_aggregate(), cls.is_polymorphic());
    std::println("  size={} align={}", cls.get_size(), cls.get_alignment());
    std::println("  fields={} methods={}", cls.get_fields().size(), cls.get_methods().size());

    // 字段：按名取句柄，附带 C++ 特有的布局信息。
    speculo::Field fx = cls.get_field("x").unwrap();
    speculo::Field fy = cls.get_field("y").unwrap();
    for (const speculo::Field& f : { fx, fy })
    {
        std::println("  field {:<2} type={:<7} size={} offset={} align={}",
                     f.get_name(), f.get_type().get_name(),
                     f.get_size(), f.get_offset(), f.get_alignment());
    }

    Point p{ 3, 4 };

    // 按值读 / 写。
    std::println("  get x  -> {}", fx.get(p).unwrap().as<int>().unwrap());
    int ten = 10;
    fx.set(p, ten).unwrap();
    std::println("  set x=10 -> p.x = {}", p.x);

    // 子对象引用视图（指向字段本体，不拷贝）。
    speculo::ObjectRef rx = fx.get_ref(p).unwrap();
    rx.as<int>().unwrap() = 42;
    std::println("  get_ref x=42 -> p.x = {}", p.x);

    // 方法调用：实参用 Args 打包，返回值装箱成 owning Object。
    speculo::Method norm = cls.get_method("norm").unwrap();
    speculo::Object result = std::move(norm.invoke(p, {}).unwrap());
    std::println("  norm() -> {} (return type {})",
                 result.as<double>().unwrap(), norm.get_return_type().get_name());

    return 0;
}
