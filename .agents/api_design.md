# speculo API 详细设计

> 命名约定：**类名大驼峰**（Java 风格，如 `Class` / `Field` / `ObjectRef`），**方法与自由函数蛇形命名**（snake_case，如 `get_name` / `new_instance`）。即：API 的词汇直接取自 Java，但书写遵循 C++ 下划线惯例。
> 所有 API 位于命名空间 `speculo`，通过 `import speculo;` 使用。
> `Option` / `Result` 指 `crab_cpp::Option` / `crab_cpp::Result`（由 speculo 模块重导出别名，见 §1）。

## 0. 最小使用示例（目标形态）

```cpp
import std;
import speculo;

struct Point { int x; int y; double norm() const { return std::sqrt(x*x + y*y); } };

int main()
{
    const Class& cls = speculo::class_of<Point>();

    // Java 风格
    Field  fx = cls.get_field("x").unwrap();
    Method fn = cls.get_method("norm").unwrap();

    Point p{3, 4};
    int   ten = 10;
    fx.set(p, ten).unwrap();                          // Status
    Object r = fn.invoke(p, {}).unwrap();             // rvalue unwrap() moves the Object out (crab 0.4.0)
    double v = r.as<double>().unwrap();

    // C++ 特有
    std::println("{}: size={} align={}", cls.get_name(), cls.get_size(), cls.get_alignment());
    std::println("x: size={} offset={} align={}", fx.get_size(), fx.get_offset(), fx.get_alignment());
}
```

## 1. error 分区：错误模型

```cpp
namespace speculo {

enum class ErrorKind {
    NotFound,            // 名字/签名查不到
    TypeMismatch,        // 实参类型与形参不符 / Object 内含类型与 as<T> 不符
    NullInstance,        // 对空 ObjectRef 操作
    NotSupported,        // 能力缺失（位域取引用、不可拷贝类型拷贝、抽象类构造…）
    ReadOnly,            // 写 const 字段 / const 对象 / 引用成员
    Ambiguous,           // 重载/多重继承路径不唯一
    InvalidArgument,     // 实参个数不对、索引越界等
};

struct Error {
    ErrorKind   kind;
    std::string message;   // 含类型名/成员名等上下文
};

constexpr std::string_view to_string(ErrorKind kind);
// std::formatter<ErrorKind> -> "TypeMismatch"；std::formatter<Error> -> "TypeMismatch: <message>"

using Status = Result<std::monostate, Error>;   // None 即成功
template <typename T>
using Outcome = Result<T, Error>;

}
```

说明：`Status` 里 `std::monostate` 与 crab_cpp 的 `None` 对齐；写操作成功返回 `None`。

`message` 的约定：以「操作(成员)」开头，如 `Field::set(p6::Point::x)`、`Method::invoke(int p6::Point::scale(int) const)`，后面写具体原因，并带上相关类型的 `qualified_name`（实参类型与形参类型、接收者类型与声明类、基类转换的两端）、实参下标、期望/实际个数、数组下标与长度。消息是诊断文本，不保证稳定，程序逻辑应判断 `kind`。

## 2. modifier 分区：修饰符

### 2.1 `Modifier`

Java 风格的位掩码工具类（不可继承、值语义），覆盖 Java 概念 + C++ 扩展：

```cpp
class Modifier {
public:
    // —— Java 对应位 ——
    static constexpr std::uint32_t PUBLIC      = 1 << 0;
    static constexpr std::uint32_t PROTECTED   = 1 << 1;
    static constexpr std::uint32_t PRIVATE     = 1 << 2;
    static constexpr std::uint32_t STATIC      = 1 << 3;
    static constexpr std::uint32_t ABSTRACT    = 1 << 4;   // 类：含纯虚；方法：纯虚
    static constexpr std::uint32_t INTERFACE   = 1 << 5;   // 类：抽象、无非静态数据成员（含基类）、基类也是 interface、非静态成员函数全纯虚（特殊成员、静态函数除外）
    static constexpr std::uint32_t FINAL       = 1 << 6;   // C++ final 类/虚函数
    // —— C++ 扩展位 ——
    static constexpr std::uint32_t CONST       = 1 << 8;   // const（方法 cv / 字段顶层 const / 指针所指 const）
    static constexpr std::uint32_t VOLATILE    = 1 << 9;
    static constexpr std::uint32_t VIRTUAL     = 1 << 10;
    static constexpr std::uint32_t INLINE      = 1 << 11;  // 预留：GCC 16.2 的 <meta> 没有 is_inline，不会置位，也没有 is_inline()
    static constexpr std::uint32_t EXPLICIT    = 1 << 12;  // explicit 构造/转换
    static constexpr std::uint32_t NOEXCEPT    = 1 << 13;
    static constexpr std::uint32_t CONSTEXPR   = 1 << 14;  // 预留：同 INLINE（无 is_constexpr）
    static constexpr std::uint32_t CONSTEVAL   = 1 << 15;  // 预留：同 INLINE（无 is_consteval）
    static constexpr std::uint32_t MUTABLE     = 1 << 16;  // mutable 字段
    static constexpr std::uint32_t BITFIELD    = 1 << 17;
    static constexpr std::uint32_t REFERENCE   = 1 << 18;  // 引用成员（T& 字段）
    static constexpr std::uint32_t DEFAULTED   = 1 << 19;  // = default 的特殊成员
    static constexpr std::uint32_t DELETED     = 1 << 20;  // = delete
    static constexpr std::uint32_t OVERRIDE    = 1 << 21;

    constexpr bool is_public() const;    // …除预留位外每个位一个 is_xxx()，共 16 个
    constexpr std::uint32_t bits() const;
    constexpr std::string to_string() const;  // "public static"（Java 习惯，空格分隔）

    constexpr Modifier() = default;
    // 值语义，可 ==/!=，可用 | 组合（friend operator|）
};
```

访问位（PUBLIC/PROTECTED/PRIVATE）互斥且恰好出现一位。类的访问位：嵌套类取其声明的访问级别，命名空间作用域的类为 `PUBLIC`。

### 2.2 `RefQualifier`

```cpp
enum class RefQualifier { None, Lvalue /* & */, Rvalue /* && */ };
```

### 2.3 `TypeKind`

`Class` 的一级分类（便于用户快速分派）：

```cpp
enum class TypeKind {
    Void, Fundamental, Pointer, MemberPointer, Array, Enum,
    Union, Class, Function, Reference
};
// 注：引用类型**不再归一到被引用类型** —— class_of<T&>/class_of<T&&> 与 class_of<T> 互不相等，kind 为
//     Reference。理由：重载解析 / get_method<Params...> / get_method(name, {&cls...}) 都是**按类型给键**，
//     若把 & / && 归一，f(T&) 与 f(T&&)（及拷贝构 vs 移动构）的形参类型会相同，无法选出正确重载。
//     「某成员/返回值是引用」仍可用 Modifier::REFERENCE 表达（与类型身份并存，不冲突）。
//     ⚠️ 但引用类型只作**声明类型**：Object/ObjectRef 的 type 永远是对象类型（见 design §5 不变式）。
```

## 3. object 分区：类型擦除对象

### 3.1 `ObjectRef`（non-owning）

```cpp
class ObjectRef {
public:
    ObjectRef() noexcept;                       // 空
    template <typename T>
    ObjectRef(T& obj) noexcept;                 // 从具体对象
    ObjectRef(const void* data, const Class& type) noexcept;

    bool is_null() const noexcept;
    bool is_const() const noexcept;
    bool is_volatile() const noexcept;          // volatile 视图：as<T> 需写成 as<volatile T>；按值读写 / 非 volatile 调用被拒绝
    const Class& type() const;                  // 空时 panic
    const void* raw_data() const noexcept;

    template <typename T> Option<const T&> as() const noexcept;
    template <typename T> Option<T&>       as() noexcept;   // const 视图 + 非 const T → None；as<const T>() 可读
    // raw_data() 只有 const void* 版本：视图可能指向 const 对象，不提供可写地址

    Result<Object, Error> clone() const;        // 拷贝出新 owning 对象；类型不可拷贝 → NotSupported

    // 比较：同类型同地址为相等（可选，初版可不做）
};
```

### 3.2 `Object`（owning）

```cpp
class Object {
public:
    Object() noexcept;                          // 空
    template <typename T>                       // 从值构造（decay）
    static Outcome<Object> from_value(T value);
    template <typename T, typename... Args>     // 原位构造
    static Outcome<Object> make(Args&&... args);

    Object(Object&&) noexcept;
    Object& operator=(Object&&) noexcept;
    Outcome<Object> clone() const;              // 类型可拷贝才可复制
    ~Object();

    bool is_null() const noexcept;
    const Class& type() const;
    void* raw_data() noexcept;
    const void* raw_data() const noexcept;

    template <typename T> Option<T&> as() noexcept;         // TypeMismatch → None
    template <typename T> Option<const T&> as() const noexcept;
    template <typename T> Outcome<T> take() &&;             // 移出值，对象置空

    operator ObjectRef() noexcept;              // 隐式转引用视图
    ObjectRef ref() noexcept;
    /*const*/ ObjectRef cref() const noexcept;

    Object(const Object&) = delete;             // 显式走 clone()，避免意外深拷贝
    Object& operator=(const Object&) = delete;
};
```

### 3.3 `Args`：实参包装器（SSO）

`Method::invoke` / `Constructor::new_instance` / `Field::set` 等接收可变实参的 API 统一使用 `Args`——一个**小对象优化**的 `ObjectRef` 集合，替代 `std::span<const ObjectRef>`，解决 `Object` 无法隐式进入 `span` 的问题。

```cpp
class Args {
public:
    // —— 构造（统一转成 ObjectRef 存入）——
    Args() noexcept;                                    // 空
    // 花括号实参列表。元素类型 Arg 隐式接受：ObjectRef、Object / 具体类型的左值（取视图）、
    // 以及**临时值**（活到整条调用表达式结束）；字符串字面量按 const char* 传。
    //   cls.new_instance({3, 4});  method.invoke(obj, {Vec(1, 2), "text", true});
    // 仍是精确类型匹配：{3} 是 int、{short{3}} 是 short，不做数值提升。
    // 注意 Args 只是视图：临时值写在列表里时，不要把 Args 存下来在之后的语句中使用。
    Args(std::initializer_list<Arg> args);
    // 模板：任意可隐式转 ObjectRef 的引用混合（Object、具体类型 T& 等）
    template <typename... Ts>
        requires (std::convertible_to<Ts&, ObjectRef> && ...)
    Args(Ts&... objs);

    // —— 访问 ——
    std::span<const ObjectRef> span() const noexcept;
    std::size_t size() const noexcept;
    bool empty() const noexcept;
    const ObjectRef& operator[](std::size_t i) const;

    // —— 语义 ——
    Args(Args&&) noexcept;                  // 可移动
    Args& operator=(Args&&) noexcept;
    Args(const Args&) = delete;             // 不可拷贝（避免意外复制缓冲）
    Args& operator=(const Args&) = delete;
    ~Args();                                // 堆缓冲时释放

private:
    static constexpr std::size_t kInlineCapacity = 8;   // 内联 8 个 ObjectRef = 128 字节
    // 内部：union { array<ObjectRef, 8>; vector<ObjectRef>*; } + size + 标志位
};
```

**行为规则**：

- **≤ 8 个实参**：零堆分配，全部走栈上内联缓冲（8 × 16 字节 = 128 字节栈开销）。
- **> 8 个实参**：自动降级为 `std::vector<ObjectRef>` 堆分配，析构时释放。
- `Args` 本身**不拥有**任何对象——它只是 `ObjectRef` 的容器，生命周期由调用者保证（和 `span` 语义一致，只是解决了构造便利性）。
- 构造发生在调用点，通常随 `invoke(obj, {a, b, c})` 的临时量一起销毁，无额外开销。

```cpp
// 使用示例
method.invoke(p, {});                 // 无参：空 Args
method.invoke(p, {x});                // 1 个：内联
method.invoke(p, {x, y, name});       // 3 个：内联
method.invoke(p, {a,b,c,d,e,f,g,h,i,j}); // 10 个：自动堆分配
ctor.new_instance({arg1, arg2});      // 构造参数同样用 Args
```

## 4. type 分区：`Class`

`Class` 是所有类型的统一描述（对应 Java 的 `java.lang.Class`，但扩展到 C++ 全部类型范畴）。句柄语义：8/16 字节、可拷贝、可比较；同一个底层类型全程序返回等价句柄。

命名空间 `std` 中的类型（`std::string`、`std::vector<T>`、`std::endian` 等）做**浅物化**：`get_size` / `get_alignment` / 名字、以及 `Object` 的拷贝与析构仍然有效，但 `get_fields` / `get_methods` / `get_constructors` / `get_bases` / `get_enumerators` 为空。用户类上类型为 `std::` 的字段，以及参数或返回值用到 `std::` 的成员函数，照常出现在该用户类的成员表里，可以 `get` / `set` / `get_ref` / `invoke`。

### 4.1 身份与名字

```cpp
class Class {
public:
    std::string_view get_name() const;           // "Point"（Java: getSimpleName）
    std::string_view get_simple_name() const;    // 同 get_name，别名
    std::string_view get_qualified_name() const; // "my_ns::Point"（Java: getName 的对应物）；编译器的拼写（display_string_of），
                                                 // 如 GCC 的 "long long unsigned int"，随编译器版本变化
    std::string_view get_normalized_name() const; // 库规范化的拼写："unsigned long long"、"const int* const"、"int[3]"、"std::string_view"
                                                  // （规则见 detail::normalize_type_name；与 qualified_name 都是注册键）
    // 是否有规范名字（可用作注册键 / for_name 查找）
    // 判定：无 internal linkage（排除匿名命名空间）且祖先作用域链上无函数（排除函数局部类型）。
    // 注意：fundamental / 指针 / 引用 / 模板特化类型**有**规范名（如 "int"、"std::vector<int>"），
    //       尽管 GCC 的 std::meta::has_identifier 对它们返回 false。
    bool has_canonical_name() const;

    bool operator==(const Class&) const noexcept;   // 同类型即相等
    bool operator!=(const Class&) const noexcept;
};
```

### 4.2 类型分类与属性

```cpp
    TypeKind kind() const;

    bool is_void() const;
    bool is_fundamental() const;    // 算术类型 + nullptr_t
    bool is_pointer() const;
    bool is_member_pointer() const;
    bool is_array() const;
    bool is_enum() const;
    bool is_union() const;
    bool is_class() const;          // struct/class（不含 union）
    bool is_function() const;
    bool is_polymorphic() const;
    bool is_abstract() const;
    bool is_final() const;
    bool is_aggregate() const;
    bool is_empty() const;
    bool is_scoped_enum() const;    // enum class
```

### 4.3 ★ C++ 特有：尺寸、对齐、组合信息

```cpp
    std::size_t get_size() const;       // sizeof；void/函数/incomplete → 0（配合 kind 判断）
    std::size_t get_alignment() const;  // alignof；同上 → 0

    // 指针/成员指针/数组的分解（kind 不符 → None）
    Option<const Class&> get_pointee_type() const;        // T* → T
    Option<const Class&> get_element_type() const;        // T[N] → T
    Option<std::size_t>  get_array_extent() const;        // T[N] → N；T[] → 0
    std::size_t          get_array_rank() const;          // 维度数；非数组 → 0
    Option<const Class&> get_pointee_class_type() const;  // M C::* → C

    // cv 组合类型的分解（如 const int*）：顶层类型的 cv 以修饰符呈现
    bool is_const_qualified() const;
    bool is_volatile_qualified() const;
    Option<const Class&> get_unqualified_type() const;    // 去顶层 cv；无顶层 cv → None

    // 引用类型的分解（is_reference() 为真时有效；class_of<T&>/<T&&> 与 class_of<T> 互不相等）
    bool is_reference() const;                            // T& / T&&
    RefQualifier get_reference_kind() const;              // Lvalue(&) / Rvalue(&&)；非引用 → None
    Option<const Class&> get_referenced_type() const;     // T& → T、T&& → T（保留被引用类型的 cv）

    // 枚举
    Option<const Class&> get_underlying_type() const;     // enum → 底层类型
    std::span<const Enumerator> get_enumerators() const;  // 非枚举 → 空 span

    // 枚举互转（仅 is_enum() 时有效，否则 → None）
    // 名字 → 枚举值（对齐 Java Enum.valueOf）
    Option<Object> enum_from_name(std::string_view name) const;
    // 数值 → 枚举值（值不在枚举项中 → None）。T 为任意整数类型（不含 bool / 字符类型），
    // 按数学值比较：-1 不会匹配无符号枚举项，0xFFFF...ull 也不用转成有符号数
    template <typename T> Option<Object> enum_from_value(T value) const;
    // 枚举值 → 名字（obj 类型不符或值不在枚举项中 → None）
    Option<std::string_view> enum_name_of(ObjectRef value) const;
```

### 4.4 成员访问（Java 风格双视图）

```cpp
    // public 视图（含继承）
    std::span<const Field>  get_fields() const;
    std::span<const Method> get_methods() const;        // 重载全部返回
    std::span<const Constructor> get_constructors() const;

    // 声明视图（本类全部访问级别，不含继承）
    std::span<const Field>  get_declared_fields() const;
    std::span<const Method> get_declared_methods() const;
    std::span<const Constructor> get_declared_constructors() const;

    // 按名查找（找不到 → None）
    Option<Field> get_field(std::string_view name) const;
    Option<Field> get_declared_field(std::string_view name) const;

    // 按签名精确取重载（Class* 指针比较：快、精确，不依赖注册表）
    // 注：形参列表用 std::initializer_list，故文档写法 get_method("scale", {&int_cls}) 成立
    //     （std::span 无法从 braced-init-list {&int_cls} 构造）。
    Option<Method> get_method(std::string_view name,
                              std::initializer_list<const Class*> param_types) const;
    Option<Method> get_declared_method(std::string_view name,
                              std::initializer_list<const Class*> param_types) const;

    // 按类型名取重载：每个名字经注册表 for_name 解析为 Class* 后复用上面逻辑。
    // 名字须为限定名或注册别名；任一名字解析失败（未注册/拼错）→ None。
    // 注意：字符串查找比指针比较慢，热路径请缓存解析结果或改用上面的重载。
    // 形参列表用 initializer_list 以便 get_method("scale", {"int"}) 成立
    // （std::span 无法从 braced-init-list 构造）；空 {} 与其 Class* 版歧义，
    // 0 参请用 get_method(name)。
    Option<Method> get_method(std::string_view name,
                              std::initializer_list<std::string_view> param_type_names) const;
    Option<Method> get_declared_method(std::string_view name,
                              std::initializer_list<std::string_view> param_type_names) const;

    // ── 函数模板重载：编译期解析（推荐热路径使用）──
    // 模板参数即形参类型列表，物化时直接生成 class_of<Args>() 指针数组，
    // 运行期只做指针比较——零字符串查找、零注册表依赖。
    // 匹配规则与 Class* 版本一致：精确指针比较，不做隐式转换/派生匹配（同 Java getMethod 语义）。
    // 注意：class_of<Args>() 会触发参数类型的物化（若尚未物化），编译期保证存在。
    template <typename... Params>
    Option<Method> get_method(std::string_view name) const;
    template <typename... Params>
    Option<Method> get_declared_method(std::string_view name) const;

    // 名字也可上模板参数（libstdc++ 的 std::string_view 非 structural → 用 speculo::FixedString，
    // 写法不变）：get_method<"scale", int, double>() —— 连名字都是编译期常量。
    template <FixedString Name, typename... Params>
    Option<Method> get_method() const;
    template <FixedString Name, typename... Params>
    Option<Method> get_declared_method() const;

    // 字段按名 + 期望类型一并校验（类型不符 → None）
    template <typename T>
    Option<Field> get_field(std::string_view name) const;
    template <typename T>
    Option<Field> get_declared_field(std::string_view name) const;

    // 继承成员（get_fields()/get_methods() 仍只含本类自己的 public 成员）：沿 public 基向上查，C++ 名字查找语义
    //   - 近处的声明隐藏基类同名成员；多个基类给出不同成员、或同一非 static 成员位于不同子对象（非虚菱形）→ Ambiguous；
    //     共享虚基只算一个子对象；不实现虚基的支配规则（兄弟路径同名时 Ambiguous）
    //   - protected / private 基不遍历（与 cast_to 同一张图）
    //   - 命中的成员属于 get_declaring_class()，取值/调用前用 cast_to(obj, member.get_declaring_class()) 取基类子对象
    Result<Field, Error>  get_field_recursive(std::string_view name) const;
    Result<Method, Error> get_method_recursive(std::string_view name) const;   // 最近声明该名字的类里有重载 → Ambiguous
    Result<Method, Error> get_method_recursive(std::string_view name,
                                               std::initializer_list<const Class*> param_types) const;
    // 带形参的版本每一层都按「名字 + 形参」匹配：派生类里同名异参的函数不会隐藏基类重载（与 C++ 不同）

    // 构造函数：同样提供 Class* 与类型名两种运行期重载
    Option<Constructor> get_constructor(std::initializer_list<const Class*> param_types = {}) const;
    Option<Constructor> get_declared_constructor(std::initializer_list<const Class*> param_types = {}) const;
    // 类型名版本（依赖注册表）：
    Option<Constructor> get_constructor(std::initializer_list<std::string_view> param_type_names) const;
    Option<Constructor> get_declared_constructor(std::initializer_list<std::string_view> param_type_names) const;

    // 构造函数模板重载：编译期指定参数类型
    template <typename... Params>
    Option<Constructor> get_constructor() const;
    template <typename... Params>
    Option<Constructor> get_declared_constructor() const;

    // 继承
    std::span<const BaseClass> get_bases() const;         // 直接基类
    Option<const Class&> get_superclass() const;          // Java 兼容便捷：唯一非虚基类 → Some；0 个 → None；多个/含虚基 → None
    // 取指定直接基类（C++ 多继承：A : B, C 时按类型精确取 B 或 C）
    template <typename Base> Option<const Class&> get_base() const;
    Option<const Class&> get_base(const Class& base) const;
    Option<const Class&> get_base(std::string_view qualified_name) const;  // 名字经注册表解析
    // 静态类型判定（无 RTTI，非 Java 的动态 instanceof）：this ← other 可赋值 / obj 的（静态）类型派生于 this
    bool is_assignable_from(const Class& other) const;
    bool is_instance(ObjectRef obj) const;
```

按名取重载示例（`std::string` / 字面量 / `const char*` 均可隐式转 `std::string_view`，零拷贝）：

```cpp
// Class* 版本：不依赖注册表，指针比较
const Class& int_cls = class_of<int>();
Method m1 = cls.get_method("scale", {&int_cls}).unwrap();

// 类型名版本：走注册表解析（要求 int/Foo 已被注册——class_of<T>() 首次调用即自动注册）
Method m2 = cls.get_method("scale", {"int"}).unwrap();
Constructor c = cls.get_constructor({"int", "double"}).unwrap();

std::string pname = "double";                 // std::string → string_view，OK
Method m3 = cls.get_method("translate", {pname, pname}).unwrap();

// 模板重载：编译期指定参数类型，运行期只做指针比较（最快路径）
Method m4 = cls.get_method<int>("scale").unwrap();
Method m5 = cls.get_method<double, double>("translate").unwrap();
Constructor c2 = cls.get_constructor<int, double>().unwrap();

// 名字也上模板参数：整个查询编译期确定
Method m6 = cls.get_method<"scale", int>().unwrap();

// 字段按名 + 类型校验
Field fx = cls.get_field<int>("x").unwrap();   // 名字对但类型不是 int → None
```

### 4.5 构造 / 析构 / 转换

```cpp
    Outcome<Object> new_instance(Args args = {}) const;
        // 匹配构造函数并调用；抽象/不可构造/无匹配重载 → 相应 Error

    Result<ObjectRef, Error> cast_to(ObjectRef obj, const Class& base) const;
        // 沿继承链**向上**转换指针（thunk 处理偏移与虚基调整）；仅 public 路径
        // 0 条路径 → TypeMismatch；>1 条（非虚菱形）→ Ambiguous；const 由 ObjectRef 传播
        // 向下/交叉转换需 RTTI → NotSupported（见 §11 限制）

    Modifier modifiers() const;      // 类级修饰符（ABSTRACT/FINAL/INTERFACE…）
    Outcome<Object> get_static_field(std::string_view name) const;  // 静态数据成员便捷读取
```

### 4.6 入口函数与注册表

```cpp
// 主入口（模板，编译期物化）
template <typename T> const Class& class_of();            // T 任意：值/cv/指针/数组/函数…

// 便捷自由函数别名
template <typename T> const Class& type_of();             // = class_of<T>()

// 按名字（依赖 registry 分区）
static Option<const Class&> Class::for_name(std::string_view qualified_name);
```

## 5. field 分区：`Field`

```cpp
class Field {
public:
    // —— Java 风格 ——
    std::string_view get_name() const;
    const Class& get_declaring_class() const;
    const Class& get_type() const;           // 字段的**精确**类型（引用成员为 class_of<T&> / class_of<T&&>）
    Modifier modifiers() const;
    bool is_static() const;                  // = modifiers().is_static()

    Outcome<Object> get(ObjectRef obj) const;            // 读（值拷贝）
    Status set(ObjectRef obj, ObjectRef value) const;    // 写（拷贝赋值/转换赋值）

    // 便捷存取：const 正确性在运行期校验
    template <typename T> Outcome<T> get_value(ObjectRef obj) const;
    template <typename T> Status set_value(ObjectRef obj, T&& value) const;

    // —— C++ 特有 ——
    std::size_t get_size() const;           // sizeof(字段类型)；位域 → 打包后按 get_bit_width 解释
    std::size_t get_alignment() const;
    std::size_t get_offset() const;         // 相对宿主对象的字节偏移（std::meta::offset_of）
    std::size_t get_bit_offset() const;     // 位偏移；位域时使用，非位域 = offset*8
    bool is_bitfield() const;
    Option<std::size_t> get_bit_width() const;
    bool is_mutable() const;
    bool is_reference() const;              // T& 成员

    // 子对象引用视图：返回指向字段本身的 ObjectRef
    // 位域 → NotSupported；引用成员返回被引用对象的视图（见下方规则）
    Result<ObjectRef, Error> get_ref(ObjectRef obj) const;

    bool operator==(const Field&) const noexcept;   // 同声明即相等
};
```

规则细节：

- `set` 目标为 const 字段或 const 对象 → `ErrorKind::ReadOnly`。
- 静态字段的 `get/set` 忽略 `obj`（可传空）。
- 位域：`get` 正常（值拷贝），`set` 经位掩码 thunk，`get_ref` → `NotSupported`。
- 引用成员：`get_type()` 返回**精确类型**（`class_of<T&>` / `class_of<T&&>`）；`get` 读被引用值，
  `set` 赋值到被引用对象（不能重绑引用——C++ 语义）；**`get_ref()` 返回的 `ObjectRef` 的 `type()` 是 referent
  的对象类型 `T`**（对象句柄不携带引用类型，见 design §5 不变式），是否 const 只取决于被引用类型；
  const 对象不会让引用成员的被引用对象变成 const（`set` / `get_ref` 同此规则）。
- `set` 在字段本身为 const（或引用成员的被引用类型为 const）时返回 `ReadOnly`；类型不可拷贝赋值（数组、
  `unique_ptr` 等）时返回 `NotSupported`，二者不混用。

## 6. method 分区：`Method`

```cpp
class Method {
public:
    // —— Java 风格 ——
    std::string_view get_name() const;                 // 运算符给出规范名："operator+", "operator=" …
    const Class& get_declaring_class() const;
    const Class& get_return_type() const;
    std::span<const Class* const> get_parameter_types() const;
    std::size_t get_parameter_count() const;
    Modifier modifiers() const;

    Outcome<Object> invoke(ObjectRef obj, Args args = {}) const;
    // 返回值 void → 空 Object；obj 仅对非静态成员函数有意义
    // 产物类型是返回类型的 remove_cvref；返回 T& 时得到 referent 的拷贝（不可拷贝 → NotSupported）
    Outcome<ObjectRef> invoke_ref(ObjectRef obj, Args args = {}) const;
    // 仅返回引用的方法：给出 referent 的视图（不拷贝，可用于不可拷贝类型）；type() 为 T，
    // const/volatile 取决于被引用类型；其余方法 → NotSupported
    bool returns_reference() const;
    bool is_ref_invokable() const;                     // invoke_ref 可用

    // —— C++ 特有 ——
    bool is_const() const;
    bool is_volatile() const;
    RefQualifier get_ref_qualifier() const;
    bool is_noexcept() const;
    bool is_virtual() const;
    bool is_pure_virtual() const;
    bool is_override() const;
    bool is_static_member() const;                     // 静态成员函数
    // 构造函数不在 Method 中，见 Constructor
    // 特殊成员识别（析构/运算符类别）——初版提供：
    bool is_destructor() const;
    bool is_operator() const;
    bool is_invokable() const;                         // false：`&&` 限定、析构、deleted、C 可变参数、thunk 无法表达
    bool is_deleted() const;                           // = delete（列在表中，invoke → NotSupported）
    bool is_defaulted() const;

    // 签名稳定字符串（用于诊断/注册键）："my_ns::Foo::bar(int, double) const &"
    std::string_view get_signature() const;

    bool operator==(const Method&) const noexcept;    // 同一次声明即相等（重载不相等）
};
```

调用约定：

- 实参个数不符 → `InvalidArgument`。
- 某个 `ObjectRef` 类型与形参不符 → `TypeMismatch`。**只做精确类型匹配**（描述符指针相等；引用形参按 cv 规则绑定），
  不做数值提升、算术转换、基类指针转换；基类对象请先 `Class::cast_to`。
- 对 const 对象调非 const 方法 → `ReadOnly`。
- `invoke` 内部对虚函数经 thunk 直接调用，**遵守虚派发语义**（C++ 天然如此）。
- 全项目以 `-fno-exceptions` 编译且禁止 `throw`：被调函数不会抛异常；错误一律经
  `Result`/`Status` 返回。若用户类型违反该约定（在反射调用路径上抛出），属未定义行为。
- `is_destructor()`：仅对**用户声明**的析构函数为真（隐式析构不入表）；析构 `Method` 不可
  `invoke`（返回 `NotSupported`），`get_return_type()` 返回 `class_of<void>()`。
- `is_operator()`：`operator+` 等运算符与转换运算符为真（它们无 identifier）。
- 成员表的内容：普通成员函数；**用户声明的**析构、拷贝 / 移动赋值（隐式声明的不列）；`= delete` 的函数
  （带 `DELETED`，不可 `invoke`）；C 可变参数函数（只列固定形参，不可 `invoke`）。
  **不列**：构造函数（见 Constructor）、其余特殊成员、函数模板（含 deducing this 的模板）、
  非模板的显式对象成员函数（`void f(this C&)`，形参表无法如实表达）。
  自由函数不是成员，不在这张表里，见 §6.1。

## 6.1 自由函数：`Function`

命名空间作用域的函数没有类可以挂。入口是用户点名的那一个函数，不枚举命名空间，也不做 ADL。

```cpp
class Function {
public:
    std::string_view get_name() const;
    std::string_view get_namespace() const;            // 全局命名空间为空
    std::string_view get_qualified_name() const;       // "my_ns::inner::scale"
    const Class& get_return_type() const;              // void 也有 Class
    std::span<const Class* const> get_parameter_types() const;
    std::size_t get_parameter_count() const;
    Modifier modifiers() const;                        // 只有 NOEXCEPT / DELETED；没有访问级别

    Outcome<Object> invoke(Args args = {}) const;     // 无接收者
    Outcome<ObjectRef> invoke_ref(Args args = {}) const;

    bool is_noexcept() const;
    bool is_deleted() const;
    bool is_operator() const;
    bool is_variadic() const;                          // C 可变参数；只列固定形参
    bool is_invokable() const;
    bool returns_reference() const;
    bool is_ref_invokable() const;
    bool has_canonical_name() const;                   // false：static / 匿名命名空间，不进 for_name

    std::string_view get_signature() const;            // for_name 的键，见下

    std::span<const Annotation> get_annotations() const;
    template <typename T> Option<const T&> get_annotation() const;
    std::span<const Annotation> get_parameter_annotations(std::size_t index) const;
    template <typename T> Option<const T&> get_parameter_annotation(std::size_t index) const;

    bool operator==(const Function&) const noexcept;  // 同一次声明即相等

    static Option<const Function&> for_name(std::string_view signature);
    static std::vector<std::string_view> registered_signatures();
};

template <auto Token>
const Function& function_of();

// 重载消歧：按形参类型选出函数指针，再交给 function_of。
// C 可变参数（...）用第二份。noexcept 函数也能绑到第一份，反射结果仍是原来的 noexcept 函数。
template <typename... Params, typename R>
consteval R (*overload_of(R& fn))(Params...);                 // fn 的类型是 R(Params...)
template <typename... Params, typename R>
consteval R (*overload_of(R& fn))(Params..., ...);            // fn 的类型是 R(Params..., ...)
```

点名方式（GCC 16.2：`reflect_function` 只能作用在函数指针常量上，不能作用在函数参数上，所以入口是非类型模板参数）：

```cpp
const Function& add = speculo::function_of<my_ns::add>();          // 名字唯一；写 &my_ns::add 相同
const Function& scale = speculo::function_of<speculo::overload_of<int, int>(my_ns::scale)>();
const Function& same = speculo::function_of<^^my_ns::add>();       // 与上一行 add 是同一个函数

int left = 2;
int right = 3;
Object sum = add.invoke({ left, right }).unwrap();
```

规则：

- 实参检查与 `Method::invoke` 相同：个数不符 → `InvalidArgument`；类型不符 → `TypeMismatch`；
  只做精确匹配。`T&&` 形参因为调用方已经点名了这个 `Function`，可以接受，并按右值转发。
- 返回值装箱与 `Method` 相同：`void` 得到空 `Object`；`T&` 的 `invoke` 拷贝 referent（不可拷贝 →
  `NotSupported`），`invoke_ref` 给出 referent 的视图。
- 不补默认实参。形参表是声明里的全部形参，调用时个数必须一致。
- C 可变参数、按值传递不可拷贝形参：句柄在，`invoke` / `invoke_ref` 为 `NotSupported`。
- **`= delete` 的自由函数点不了名**。GCC 16.2 把 `^^f` 放进模块模板的非类型参数时会 odr-use 这个函数，已删除的函数因此直接编不过。成员函数里的 `= delete` 仍由类的成员表列出（那条路径不取地址）。
- 函数模板本身不反射。点名一个已经实例化的特化（`function_of<my_ns::id<int>>()`）可以。
- 成员函数（含静态成员函数）不从这里进，用 `Class::get_method`。
- `get_signature()` 是编译器的 `display_string`，`noexcept` 函数再接上 `" noexcept"`
  （display 串本身不含 noexcept）。`for_name` 只认这串，不做空白规范化。
- 有规范名的函数在 `function_of` 时自动注册。`static` 和匿名命名空间里的函数
  `has_canonical_name()` 为 false：可以 `invoke`，`for_name` 查不到。
- 不支持：命名空间枚举、ADL、命名空间作用域变量、`consteval` 函数（GCC 16.2 没有
  `is_consteval`，点名可能直接编不过）。

## 7. ctor 分区：`Constructor`

```cpp
class Constructor {
public:
    const Class& get_declaring_class() const;
    std::span<const Class* const> get_parameter_types() const;
    std::size_t get_parameter_count() const;
    Modifier modifiers() const;        // PUBLIC…/EXPLICIT/DELETED/DEFAULTED/NOEXCEPT

    bool is_default_constructor() const;
    bool is_copy_constructor() const;
    bool is_move_constructor() const;
    bool is_explicit() const;
    bool is_deleted() const;
    bool is_defaulted() const;
    bool is_noexcept() const;

    Outcome<Object> new_instance(Args args = {}) const;   // 堆分配 owning
    // 原位构造（配合外部存储，如 placement 场景）：
    Status construct_at(void* storage, Args args = {}) const;
    Status destroy_at(void* storage) const;   // 类级析构便捷（也可走 Class 的 dtor 能力）

    std::string_view get_signature() const;   // "my_ns::Foo::Foo(int, double)"
    bool operator==(const Constructor&) const noexcept;
};
```

规则细节：

- `get_constructors`/`get_declared_constructors` 收录**隐式**默认/拷贝/移动构造函数（因此无用户构造
  的类也能 `new_instance`）。拷贝构形参为 `const T&`、移动构为 `T&&`，二者**类型不同**（引用进类型身份后），
  故可 `get_constructor<const T&>()` / `get_constructor<T&&>()` 直接区分；`is_copy_constructor()`/
  `is_move_constructor()` 仍可作判定。（P4 时二者曾归一为同一 `Class*`，属实现待 P5 修正。）
- 删除/抽象以及**私有/保护**构造函数不生成物化 thunk：`new_instance`/`construct_at` 返回
  `NotSupported`（私有构造因命名空间作用域的 `new` 无类内访问权）；但 declared 视图仍可见并带访问修饰符。
- `Class::new_instance(Args)` 按实参类型精确匹配 public 构造函数；无匹配 → `NotFound`，抽象类 → `NotSupported`。

## 8. type 分区附属：`BaseClass` / `Enumerator`

```cpp
struct BaseClass {
    const Class* type;          // 基类类型
    Modifier     modifiers;     // PUBLIC/PROTECTED/PRIVATE/VIRTUAL
    bool is_virtual() const;    // 虚基
    // 注意：不暴露 offset（虚基无静态偏移）；指针调整走 Class::cast_to 的 thunk
};

struct Enumerator {
    std::string_view name;                      // 枚举项名
    const Class*     declaring_enum;
    Outcome<Object>  value() const;             // 按枚举的底层类型装箱（enum : int → int，enum : unsigned char → unsigned char），
                                                // 类型即 get_underlying_type()
    std::int64_t  to_int64()  const;            // 有符号视图（越界语义按底层类型转换）
    std::uint64_t to_uint64() const;
};
// 枚举互转便捷函数见 §4.3 Class::enum_from_name / enum_from_value / enum_name_of
```

`Class`、`Field`、`Method`、`Constructor`、`BaseClass`、`Enumerator` 上的注解查询见 §8.1。

## 8.1 Annotation（C++26 `[[= expr]]`）

C++26 的注解（P3394）是挂在声明上的**常量值**，语法是 `[[= 常量表达式]]`。它和 `[[nodiscard]]` 这类普通 attribute 不是一回事：普通 attribute 可以被实现忽略，反射也看不到；注解的值必须保留，并通过 `std::meta::annotations_of` 取回。

本库把它收成运行期句柄。Java 的 `Annotation` 是「带成员的注解类型」；这里没有这层——注解就是一个值，类型是那个常量表达式的类型。

```cpp
class Annotation {
public:
    const Class& get_type() const;             // 值的类型；顶层 const 已去掉（编译器给出的 const Tag → class_of<Tag>()）

    bool is_readable() const;                  // 这个值在当前编译器上能不能取出来（见下方「可读性」）

    // 类型精确匹配（忽略顶层 const）且可读 → 指向物化时生成的静态副本。
    // 类型不符，或不可读 → None。不拷贝。
    template <typename T> Option<const T&> get() const;

    // 可读且可拷贝 → 装箱进 Object；不可读或不可拷贝 → NotSupported。
    Outcome<Object> value() const;

    bool operator==(const Annotation&) const noexcept;   // 同一处注解即相等，不比较值
};
```

挂在已有句柄上，不另做一套 declared/public 视图（注解不继承，见下）：

```cpp
// Class / Field / Method / Constructor / BaseClass / Enumerator
std::span<const Annotation> get_annotations() const;     // 声明顺序，可空

// 该类型的第一份。没有 → None。同类型有多份时，其余的从 get_annotations() 里按 get_type() 过滤。
template <typename T> Option<const T&> get_annotation() const;

// 函数形参没有独立句柄。下标与 get_parameter_types() 对齐；越界是前置条件错误（panic）。
// Method 与 Constructor 都有。
std::span<const Annotation> get_parameter_annotations(std::size_t index) const;
template <typename T> Option<const T&> get_parameter_annotation(std::size_t index) const;
```

使用形态：

```cpp
struct Tag {};
struct Label { int rank; };

struct [[= Tag{}]] Point {
    [[= Label{ 3 }]]
    [[= 1, = 2]]
    int x;

    void scale([[= Tag{}]] int factor) const;
};

const speculo::Class& cls = speculo::class_of<Point>();
const Tag& tag = cls.get_annotation<Tag>().unwrap();          // 类上的空标记

speculo::Field x = cls.get_field("x").unwrap();
const Label& label = x.get_annotation<Label>().unwrap();      // rank == 3
const int& first = x.get_annotation<int>().unwrap();          // 同类型多份时只要第一份：1
// 第二份 int 注解从整张表里按类型滤：
for (const speculo::Annotation& ann : x.get_annotations()) {
    if (ann.get_type() == speculo::class_of<int>() && ann.get<int>().unwrap() == 2) {
        break;
    }
}

speculo::Method scale = cls.get_method("scale").unwrap();
scale.get_parameter_annotation<Tag>(0).unwrap();
```

### 能挂在哪

对照 GCC 16.2 实测（`spikes/probe_annotation_api.cpp`）和 P3394。句柄一列是本库打算暴露的入口。

| 写在哪 | 本库从哪读 | GCC 16.2 |
|---|---|---|
| 类 / 联合 / 枚举类型 | `Class::get_annotations()` | 可以。`const Widget` 看得到同一份；`Widget&` / `Widget*` **看不到**，这两个 `Class` 返回空 |
| 非静态 / 静态数据成员 | `Field` | 可以。类内一份、类外再声明一份时，两处注解**按声明先后接在一起** |
| 成员函数 | `Method` | 同上，类内声明和类外定义的注解会累加 |
| 构造函数 | `Constructor` | 可以 |
| 枚举项 | `Enumerator` | 可以。语法是写在名字后面：`Red [[= 1]]`，写在名字前面会被当成枚举类型自己的 attribute |
| 基类说明 | `BaseClass` | 可以。这是**这条基类关系**上的注解，不是基类类型自己的 `Class::get_annotations()` |
| 函数形参 | `Method` / `Constructor` 的 `get_parameter_annotations(i)` | 可以 |
| typedef / using 别名 | 不暴露 | `^^Alias` 在 meta 层看得到，但 `class_of` 把别名折叠成底层类型，注解随之丢失（与 design §11「别名直接解析到底层类型」一致） |
| 自由函数 | `Function` | 可以。点名 `function_of`，不从命名空间枚举。GCC 16.2 上 `= delete` 的自由函数点不了名 |
| 命名空间、命名空间作用域变量 | 不做 | meta 看得到，本库没有这些句柄 |
| `[[nodiscard]]`、`[[deprecated]]` 等普通 attribute | 不暴露 | `annotations_of` 不返回它们。attribute 和注解也不能写进同一个 `[[ ]]`，要分成 `[[nodiscard]] [[= 1]]` |

不继承：派生类的 `get_annotations()` 不含基类上的注解，也不含基类成员上的注解。要看基类自己的，先 `get_bases()` / `get_superclass()` 再查。

`SPECULO_IMPL` 注入的 `get_class()` 仍不出现在方法表里，因此它上面的 `reflect_impl_tag` 也不会从注解接口漏出来。

`std` 类型仍然浅物化：不因为「std::string 内部可能有注解」去展开它。用户字段上写的 `[[= ...]]` 属于那个字段，跟字段类型是不是 `std::` 无关，照常能读到。

### 值怎么取

注解表达式必须是结构性类型的常量表达式，这条由语言保证；写错在声明处就编不过，轮不到 `class_of`。

物化时为每份注解保存类型，并在**可读**时把值复制进静态存储。`get<T>()` 比较 `class_of<remove_const_t<T>>()` 与 `get_type()`，命中就返回那份静态副本。

**可读性（GCC 16.2 的限制，不是语言的限制）**：`extract` 走 `reflect_constant`。实测：

- 可读：标量（`[[= 42]]`）、枚举、空类（`Tag{}`）、成员递归下去都是这些的类（`Label{ 3 }`）、`std::array<char, N>` 这类不含指针的数组。
- 不可读：指针，以及含指针 / 引用的类。字符串字面量 `[[= "literal"]]` 的类型是 `const char*`（数组衰成指针），`extract` 直接失败，报 `reflect_constant failed`。`struct Rename { const char* name; }` 同样取不出值。

不可读的注解**仍然出现在列表里**（`get_type()` 有效，例如 `class_of<const char*>()`），但 `is_readable()` 为 false，`get<T>()` 为 `None`，`value()` 为 `NotSupported`。物化时识别出这种类型就不去 `extract`，避免一份指针注解让整个 `class_of<用户类型>()` 编不过。

判定：从注解值的类型往下看，遇到指针、成员指针、引用即为不可读；数组看元素；类 / 联合看全部非静态数据成员。标量、枚举、空类可读。顶层 `const` 不影响判定。

`value()` 的装箱规则与 `Field::get` 相同：要能拷贝。不可拷贝但可读的值仍可用 `get<T>()` 拿到引用。

### 明确不做

- 不把注解继承进派生类，也不做 Java 的 `@Inherited` / retention。
- 不反射普通 attribute 的名字或实参。
- 不做 P3394 §4.5 里还没进 C++26 的能力：按条件加注解、注解回调、用注解往类里注入成员。
- 同一类型多份注解时，`get_annotation<T>()` 只给第一份，不报 Ambiguous（与「按名取重载」不同：注解没有重载决议，重复是合法的）。
- GCC 16.2 上 `= delete` 的函数不查注解（`annotations_of` 会实例化函数体，已删除的函数没有可实例化的体）。这类函数的 `get_annotations()` / 形参注解为空。

## 9. array 分区：`Array`（对应 `java.lang.reflect.Array`）

操作 C 风格数组 `T[N]` 与运行期长度未知的动态数组视图：

```cpp
class Array {
public:
    // —— 创建 ——
    static Outcome<Object> new_instance(const Class& element_type, std::size_t length);
        // 元素须可默认构造 → 否则 NotSupported

    // —— 访问（对 Class::is_array() 的对象或 Array::new_instance 的产物）——
    static Result<std::size_t, Error> get_length(ObjectRef array);
    static Outcome<Object>    get(ObjectRef array, std::size_t index);   // 值拷贝
    static Status             set(ObjectRef array, std::size_t index, ObjectRef value);
    static Result<ObjectRef, Error> get_ref(ObjectRef array, std::size_t index);
    static Option<const Class&> get_element_type(ObjectRef array);
};
```

越界 → `InvalidArgument`；非数组 → `TypeMismatch`。

补充语义（P5 实现为准）：

- 静态数组 `T[N]`：长度取数组 `Class` 的 extent；元素地址 = `base + i*sizeof(T)`，无需 thunk。
- `new_instance(E, n)`：堆分配「长度头 + n 个默认构造的 E」，返回 `Object`；`get_length` 从头部读回长度
  （该 `Object` 的 `Class` 为 `is_array()` 的动态视图）。元素须可默认构造/析构 → 否则 `NotSupported`。
- 多维 `T[M][N]`：`get`/`set` 只作用于数组**自身层级**的元素；rank>1 的元素类型是数组（不可按值拷贝），
  故对 rank>1 直接 `get`/`set` → `NotSupported`，应先用 `get_ref` 逐级降维（`get_ref` 返回内层数组视图）。
- 不支持「元素本身是数组」的动态 `new_instance`（如 `int[3][]`）。

## 10. registry 分区

```cpp
namespace speculo {

// 按限定名查找（自动注册由 class_of<T> 首次调用触发）；
// 定义在 speculo:registry 分区（Phase 6 已落地）。
// 名字口径：get_qualified_name()（编译器拼写）与 get_normalized_name()（库规范化拼写）两份都是键；
// 查询串先按原样查，查不到再规范化后查，因此空白、`{aka ...}`、`unsigned long long` 与
// `long long unsigned int` 等不同写法都能命中。无规范名者不注册、查不到。
// 规范化规则：丢掉 `{aka ...}`；空白只出现在两个单词之间以及 `* & ,` 之后、`> )` 后接单词之前；
// 基本类型用短写法（`unsigned long long` / `long` / `short` / `unsigned int` / `signed char` / `long double`），
// 非法组合（`long long long`）原样保留；`std::__cxx11::basic_string<char>` / `std::basic_string_view<char>`
// 记作 `std::string` / `std::string_view`。
static Option<const Class&> Class::for_name(std::string_view qualified_name);
// 诊断：当前注册表里的全部名字（两种拼写，已排序）。按类型名取重载（get_method(name, {"T"})）
// 在名字未注册时返回 NotFound，并在 message 里写出该名字和已注册名字（前 16 个）。
static std::vector<std::string_view> Class::registered_names();

// 显式注册（提前注册 / “只按名字用、从不 class_of<T>”的类型）
void register_class(const Class& cls);            // 幂等；同名冲突后者忽略并返回（设计取舍，初版简单）

// 自由函数按签名注册（function_of 首次调用时自动做）。键是 Function::get_signature()，
// 不是函数名：重载共享名字。无规范名者不注册。幂等，同键后者忽略。
void register_function(const Function& fn);
static Option<const Function&> Function::for_name(std::string_view signature);
static std::vector<std::string_view> Function::registered_signatures();

}

// 头内宏（用于“只按名字用”的类型，建议在类型定义处调用；Phase 6 已落地）
#define SPECULO_REGISTER(Type)   /* 命名空间作用域；静态初始化时注册 class_of<Type>() */
```

## 10.1 宏：`include/speculo/macros.h`

与 crab_cpp 的 `macros.h` 一样，speculo 的宏单独放在一个**普通头文件**里（宏无法从模块导出）。使用前提：当前 TU 已 `import speculo;`。

```cpp
#include <speculo/macros.h>

// 非虚版本（默认，零开销）：
#define SPECULO_IMPL(Type)                                        \
    const ::speculo::Class& get_class() const noexcept            \
    {                                                             \
        return ::speculo::class_of<Type>();                       \
    }

// 虚函数版本（多态/proxy 场景）：
#define SPECULO_IMPL_VIRTUAL(Type)                                \
    virtual const ::speculo::Class& get_class() const noexcept    \
    {                                                             \
        return ::speculo::class_of<Type>();                       \
    }
```

设计要点：

- **返回 `const Class&`**（不是指针）：`class_of<T>()` 必然有效，无空值语义；对齐 Java `Object.getClass()`。
- **`SPECULO_IMPL`（非虚，默认）**：不引入虚表、**不破坏聚合体**，开销为零。`get_class()` 按**静态类型**解析。绝大多数类型用这个。
- **`SPECULO_IMPL_VIRTUAL`（虚）**：派生类再用一次即自动 override，经基类指针/引用得到**动态类型**，与 Java `Object.getClass()` 行为一致。用于多态场景，可直接挂 Microsoft proxy 的 facade：
  `PRO_DEF_MEM_DISPATCH(MemGetClass, get_class);` + `pro::facade_builder::add_convention<MemGetClass, const speculo::Class&() const>`。
- **代价只对虚版本成立**：`SPECULO_IMPL_VIRTUAL` 使该类不再是聚合体、引入虚表。
- **不要混用**：同一继承链上要么全 `SPECULO_IMPL`、要么全 `SPECULO_IMPL_VIRTUAL`。基类非虚 + 派生类 virtual 只是名字隐藏而非 override，经基类访问会拿到基类类型。
- **注入的 `get_class()` 被排除在反射之外**：它带有 `[[= speculo::detail::reflect_impl_tag{}]]` 注解，
  `Class::get_methods()` / `get_declared_methods()` 不会列出它。原因：其返回类型 `const Class&` 会让物化该
  类型时把整个 `Class` 的签名闭包（含 crab_cpp 的 move-only 变体）拖进来，而这些库类型无法实例化。
  `get_class()` 是"反射桥接"，不是可反射成员。

```cpp
// 非多态类型：默认宏，仍是聚合体
struct Point { SPECULO_IMPL(Point) int x, y; };
Point p{3, 4};              // 聚合初始化照常
p.get_class();              // → class_of<Point>()

// 多态继承链：全链用虚版本
struct Shape  { SPECULO_IMPL_VIRTUAL(Shape)  virtual ~Shape() = default; };
struct Circle : Shape { SPECULO_IMPL_VIRTUAL(Circle) double r; };

Shape& s = circle;
s.get_class();   // → class_of<Circle>()（动态类型）
```

## 11. 概览表（Java ↔ C++ 扩展对照）

| 类别 | Java 风格成员 | C++ 扩展成员 |
|---|---|---|
| `Class` | get_name/get_simple_name/get_fields/get_declared_fields/get_methods/get_method/get_constructors/new_instance/get_superclass/is_assignable_from/is_instance/is_enum/is_array/is_interface(modifier) | get_size/get_alignment/get_pointee_type/get_element_type/get_array_extent/get_array_rank/get_unqualified_type/is_const_qualified/is_volatile_qualified/get_underlying_type/get_enumerators/enum_from_name/enum_from_value/enum_name_of/is_union/is_polymorphic/is_abstract/is_final/is_aggregate/kind/get_bases/cast_to/get_static_field；get_method/get_constructor/get_field 提供三种查找路径：**模板重载**（编译期物化、最快）、`Class*` 运行期重载、**类型名**（`string_view`）运行期重载 |
| `Field` | get_name/get_type/get_declaring_class/get/set/modifiers | get_size/get_alignment/get_offset/get_bit_offset/is_bitfield/get_bit_width/is_mutable/is_reference/get_ref |
| `Method` | get_name/get_return_type/get_parameter_types/invoke/modifiers | is_const/is_volatile/get_ref_qualifier/is_noexcept/is_virtual/is_pure_virtual/is_override/is_destructor/is_operator/get_signature |
| `Function`（§6.1，Java 无对应） | get_name/get_return_type/get_parameter_types/invoke/modifiers/for_name | get_namespace/get_qualified_name/get_signature/is_noexcept/is_deleted/is_operator/is_variadic/invoke_ref；入口 `function_of` / `overload_of` |
| `Constructor` | get_parameter_types/new_instance/modifiers | is_default_constructor/is_copy_constructor/is_move_constructor/is_explicit/is_deleted/is_defaulted/construct_at/destroy_at/get_signature |
| `Modifier` | PUBLIC/PROTECTED/PRIVATE/STATIC/FINAL/ABSTRACT/INTERFACE | CONST/VOLATILE/VIRTUAL/EXPLICIT/NOEXCEPT/CONSTEXPR/CONSTEVAL/MUTABLE/BITFIELD/REFERENCE/DEFAULTED/DELETED/OVERRIDE/INLINE |
| `Array` | new_instance/get_length/get/set | get_ref/get_element_type（含数组 Class 的 rank/extent） |
| `Annotation`（§8.1） | 无直接对应（Java 注解是类型，不是值） | get_annotations/get_annotation\<T\>/get_type/get\<T\>/value；形参注解走 get_parameter_annotations |
| —（Java 无对应） | — | `Object`/`ObjectRef`（值语义替代 GC 堆对象）、`Args`（实参 SSO 包装器）、`BaseClass`、`Enumerator`、`Error`/`Status`（Result 替代异常） |

## 12. Phase 1 spike 结论

> 全部待验证事项已关闭；结论与 GCC 16.2 `std::meta` 实测细节见 @.agents/review.md「Phase 1」，
> 验证源码见 `spikes/`。要点回顾：`access_context::unchecked()` 可用（private 成员可枚举 / 拼接 / `extract`）；
> 位偏移取 `offset_of().total_bits()`、位宽取 `bit_size_of`、`size_of`/`alignment_of` 须传类型而非成员反射；
> 运算符 / 转换运算符 / 析构会被枚举但没有 identifier；`obj.[:M:]` 拼接在 16.2 不再 ICE（字段 / 位域读写直接拼接）。
