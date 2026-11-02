// 06 — proxy 集成：静态派发 + 运行期元数据，两者都无 RTTI
//
// 场景：一个容器里放着一堆「形状」槽位，类型在运行期才确定。
//   - 调用 area() 走 proxy 的类型擦除（编译期生成的派发表，无虚函数）；
//   - 需要元数据时，用 SPECULO_IMPL_VIRTUAL 注入的 get_class() 取到运行期
//     Class，再按名字列字段、读字段、调方法——全部来自编译期物化的描述表。
// 两条路都不需要 RTTI，也不需要异常。
//
// proxy 依赖：https://github.com/ngcpp/proxy （v4，社区接手的后继仓库）。
// 本文件由 CMake / xmake 的 proxy 依赖目标提供 include 路径。

#include <proxy/proxy.h>

#include <speculo/macros.h>

import std;
import crab_cpp;
import speculo;

// proxy 的派发能力：成员函数名 -> 调用约定。
PRO_DEF_MEM_DISPATCH(MemArea, area);
PRO_DEF_MEM_DISPATCH(MemGetClass, get_class);
PRO_DEF_MEM_DISPATCH(MemRaw, raw);

// 门面（facade）：槽位支持什么，由这张表决定。
struct ShapeFacade
    : pro::facade_builder
      ::add_convention<MemArea, double()>
      ::add_convention<MemGetClass, const speculo::Class&() const>
      ::add_convention<MemRaw, void*()>
      ::build
{
};

namespace demo
{

struct Shape
{
    SPECULO_IMPL_VIRTUAL(Shape)
    virtual ~Shape() = default;

    // 把「我是谁」的对像地址交出去：proxy 那一侧只有类型擦除后的槽位，
    // 有了 (地址, Class) 就能构造 speculo 的 ObjectRef，进而做反射读写。
    void* raw() noexcept { return this; }
};

struct Circle : Shape
{
    SPECULO_IMPL_VIRTUAL(Circle)
    double radius = 1.5;
    double area() const { return 3.0 * radius * radius; }
};

struct Square : Shape
{
    SPECULO_IMPL_VIRTUAL(Square)
    double side = 2.0;
    double area() const { return side * side; }
};

} // namespace demo

int main()
{
    std::vector<pro::proxy<ShapeFacade>> shapes;
    shapes.push_back(pro::make_proxy<ShapeFacade, demo::Circle>());
    shapes.push_back(pro::make_proxy<ShapeFacade, demo::Square>());

    std::println("== proxy dispatch + speculo metadata ==");
    for (pro::proxy<ShapeFacade>& shape : shapes)
    {
        // 静态派发：一次函数指针调用，没有虚表。
        const double area = shape->area();

        // 动态类型：经 SPECULO_IMPL_VIRTUAL 的虚函数拿到运行期 Class。
        const speculo::Class& cls = shape->get_class();

        // 未类型化的槽位 + Class -> 反射视图。
        const speculo::ObjectRef view(shape->raw(), cls);

        // 类型身份：不依赖 typeid，跨槽位可比较（api_design §13）。
        std::println("{:<7} area={:.2f} id={:#x}", cls.get_name(), area, cls.get_type_id());

        // 元数据：字段名、类型、字节偏移全部来自编译期物化的描述表。
        for (const speculo::Field& field : cls.get_fields())
        {
            std::println("  field {:<8} {:<7} @ +{}", field.get_name(), field.get_type().get_normalized_name(),
                         field.get_offset());
        }

        // 字段值：诊断格式化（每个字段走它自己的 formatter thunk）。
        std::println("  values: {}", speculo::to_string(view));

        // 同名方法的反射调用走的是同一份实现，结果必须一致。
        const speculo::Method method = cls.get_method("area").unwrap();
        const speculo::Object result = method.invoke(view).unwrap();
        std::println("  invoke(\"area\") = {:.2f} (matches={})", result.as<double>().unwrap(),
                     result.as<double>().unwrap() == area);
    }

    return 0;
}

