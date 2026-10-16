#ifndef SPECULO_MACROS_H
#define SPECULO_MACROS_H

// speculo 的宏集合。
//
// 与 crab_cpp 的 macros.h 一样，本头文件只做代码注入、不含任何声明；
// 使用前提是当前翻译单元已经 `import speculo;`（宏展开后直接引用
// ::speculo::Class 与 ::speculo::class_of）。
//
// ────────────────────────────────────────────────────────────────
// SPECULO_IMPL(Type)          —— 非虚版本（默认，零开销）
// SPECULO_IMPL_VIRTUAL(Type)  —— 虚函数版本（多态/proxy 场景）
//
// 两者都在类定义内部（public 区）使用，注入成员函数 get_class()：
//
//     struct Point { SPECULO_IMPL(Point) int x, y; };      // 仍是聚合体
//     struct Shape { SPECULO_IMPL_VIRTUAL(Shape)
//                    virtual ~Shape() = default; };
//     struct Circle : Shape { SPECULO_IMPL_VIRTUAL(Circle) double r; };
//
// 选择哪个：
//
//   - 默认用 SPECULO_IMPL：只是给类附加一个"我是谁"的访问器，不引入
//     虚表、不破坏聚合体，开销为零。get_class() 按静态类型解析。
//   - 需要经基类指针/引用拿到动态类型时（比如配合 Microsoft proxy
//     库做多态派发），整条继承链统一用 SPECULO_IMPL_VIRTUAL，派生类
//     再次使用即自动 override，与 Java 的 Object.getClass() 行为一致：
//
//         PRO_DEF_MEM_DISPATCH(MemGetClass, get_class);
//         struct Reflectable
//             : pro::facade_builder
//               ::add_convention<MemGetClass, const ::speculo::Class&() const>
//               ::build {};
//
//   注意：两个宏不要混用在同一继承链上（基类非虚、派生类 virtual
//   只是名字隐藏而非 override，经基类访问会拿到基类类型）。
//
// 实现细节：注入的 get_class() 带一个 [[= detail::reflect_impl_tag{}]] 注解，
// 反射物化时会被排除（is_reflectable_method）。否则物化任何用本宏的类型都会
// 因 get_class() 的返回类型 const Class& 而把整个 Class 的签名闭包（含
// crab_cpp 的 move-only 变体类型）拖进来，触发大量无法实例化的库类型。
// 也就是说 get_class() 是"反射桥接"，不出现在 get_methods() 中。

#define SPECULO_IMPL(Type)                                        \
    [[= ::speculo::detail::reflect_impl_tag{}]]                   \
    const ::speculo::Class& get_class() const noexcept            \
    {                                                             \
        return ::speculo::class_of<Type>();                       \
    }

#define SPECULO_IMPL_VIRTUAL(Type)                                \
    [[= ::speculo::detail::reflect_impl_tag{}]]                   \
    virtual const ::speculo::Class& get_class() const noexcept    \
    {                                                             \
        return ::speculo::class_of<Type>();                       \
    }

// ────────────────────────────────────────────────────────────────
// SPECULO_REGISTER(Type)      —— 强制按名字注册
//
// 在命名空间作用域（通常紧跟在类型定义之后）使用，使 Type 无论是否调用过
// class_of<Type>() 都能被 Class::for_name("...") 按限定名查到：
//
//     namespace my_ns { struct Foo { int x; }; }
//     SPECULO_REGISTER(my_ns::Foo);
//
//     auto cls = ::speculo::Class::for_name("my_ns::Foo");  // Some
//
// 展开为一个匿名/内部链接的静态初始化器，程序启动时调用一次
// register_class()（幂等）。适用于"只按名字使用、从不 class_of<T>()"的类型，
// 以及内建类型的预注册。匿名命名空间 / 函数局部类型没有规范名，注册会被
// 忽略（与 class_of<T>() 的自动注册口径一致）。
//
// 使用前提（编译器不会诊断，请自行保证）：
//
//   - 注册发生在静态初始化期，各翻译单元之间的顺序没有保证。因此不要在静态
//     初始化期（全局对象的构造函数、另一个宏展开的初始化器等）里调用
//     Class::for_name()，它可能先于对应的 SPECULO_REGISTER 运行而查不到；
//     在 main() 或之后调用则没有这个问题。
//   - 注册表本身是函数局部静态（首次使用时构造），所以注册动作本身在任何顺序下
//     都是安全的；先于注册器运行的只有「按名字查」这一种用法。
//   - 不能改成 class_of<T>() 那样的「首次使用时注册」：这个宏的用途恰恰是
//     「类型从未被 class_of 过、只有名字」，除了静态初始化没有别的触发点。
//     凡是代码里引用得到的类型，直接 class_of<T>() 即可，不需要这个宏。
//   - 宏放在头文件里时，每个包含它的翻译单元都会各自物化一次该类型（链接期
//     inline 实体会合并，编译时间不会合并）。建议放在某个 .cpp 里，或只在
//     确有需要的头文件里使用。

#define SPECULO_DETAIL_CAT_IMPL(a, b) a##b
#define SPECULO_DETAIL_CAT(a, b) SPECULO_DETAIL_CAT_IMPL(a, b)

#define SPECULO_REGISTER(Type)                                          \
    [[maybe_unused]] static const bool                                  \
        SPECULO_DETAIL_CAT(speculo_registered_, __COUNTER__) =          \
            (::speculo::register_class(::speculo::class_of<Type>()), true)

#endif // SPECULO_MACROS_H
