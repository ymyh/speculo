# speculo API 详细设计（v2）

> 命名约定、命名空间、`Option` / `Result` 的约定见 @.agents/codeStyle.md「Naming」与
> 「API vocabulary and shape」。所有 API 位于命名空间 `speculo`，通过 `import speculo;` 使用。
>
> **范围**：v1（`Class` / `Field` / `Method` / `Constructor` / `Function` / `Annotation` /
> `Array` / `Modifier` / 注册表 / `SPECULO_IMPL` 家族）**已实现，实现即规范**——以 `src/`、
> `tests/`、`examples/` 为准，本文不再复述。本文只描述 **v2 待实现** 的 API。
>
> v2 沿用 v1 的一切基础约定，无一处例外：
> - 错误模型不变：查找 → `Option`，操作 → `Result<T, Error>` / `Status`，前置条件违反 → `panic`。
> - 无 RTTI、无异常；类型信息一律来自 constexpr 描述表。
> - 句柄语义不变：`Class` / `Field` / … 是 8/16 字节可拷贝的只读句柄，指向静态表。
> - 新增类型的公开名字一律 PascalCase，方法与自由函数 snake_case。

## 0. v2 的三条主线

| 主线 | 缺的是什么 | 对应章节 |
|---|---|---|
| **类型系统建模** | C++ 有而 Java 没有的类型构造器没进描述层：模板、函数类型、聚合、嵌套类型、布局能力 | §1–§6 |
| **操作语义** | C++ 有而 Java 没有的对象语义：值类别、移动、聚合初始化、默认实参、存储期 | §7–§9 |
| **工程化** | 能落地到真实项目的条件：编译期半层、零分配、分配器、身份、诊断、关系图 | §10–§16 |

原则：**先建模，再操作，最后工程化**。每加一个 API 都要回答「Java 给不出、C++ 必须要」。

## 0.1 错误优先级是 API 契约的一部分

多个参数同时非法时，返回哪个 `ErrorKind` 由**检查顺序**唯一决定。测试要对参数做组合覆盖
（@.agents/testing.md §3），每个格子都得有确定的期望值——所以顺序必须先写进文档，
否则矩阵无法断言。

**默认顺序**（v2 新 API 一律照此，除非本节下文另有说明）：

1. 实参**个数**不符 → `InvalidArgument`
2. 接收者 / 目标侧：空 → `NullInstance`；类型不符 → `TypeMismatch`；cv 不符 → `ReadOnly` /
   `NotSupported`
3. 接收者 / 目标侧自身的**能力**（字段不可写、方法不可调用、类型不可拷贝…）→ `ReadOnly` / `NotSupported`
4. 实参侧，**从左到右**：类型不符 → `TypeMismatch`；能力缺失 → `NotSupported`
5. 执行；执行期失败按具体原因返回

v1 的实现即此顺序（`Method::invoke` / `Constructor::validate` / `Field::get` / `Field::set`
四张表见 @.agents/testing.md §5）。唯一已知偏差：`Field::get` 先查「字段能否按值读」再查
接收者类型，因为实参无法改变该结果。

v2 要求：第 1–5 步有意的偏离（例如「先查能力再查类型」）必须在本文件对应章节写一行
`检查顺序：…`，并在测试里按该顺序断言组合格子。未声明的顺序视为设计未完成。


## 1. 模板信息

### 1.1 为什么是最大缺口

Java 泛型在运行期被擦除，所以 `java.lang.reflect` 拿不到 `List<String>` 的 `String`；
C++ 模板不擦除，`std::vector<int>` 的每个特化都是独立类型，`template_arguments_of`
在物化时**免费**可得。不给出来，本库就只对「扁平 struct」有用。

### 1.2 API

```cpp
class Class {
public:
    // 是否由类模板实例化而来（std::vector<int> → true；普通类、指针、cv 类型 → false）
    bool is_template_specialization() const;

    // 主模板：std::vector<int> → std::vector。
    // 返回的 Class 的 kind() 为 TypeKind::Template，get_size()/get_alignment() 为 0，
    // 成员表为空（主模板未实例化，成员类型依赖模板参数，不物化）。
    Option<const Class&> get_primary_template() const;

    // 实参列表，声明顺序。非特化 → 空 span。
    std::span<const TemplateArgument> get_template_arguments() const;

    // 按位置取类型实参的便捷式；该位置不是类型实参 → None
    Option<const Class&> get_template_argument_type(std::size_t index) const;
};

class TemplateArgument {
public:
    enum class Kind { Type, Value, Template, Pack };

    Kind kind() const;
    Option<const Class&> as_type() const;              // Kind::Type
    Option<Object> as_value() const;                   // Kind::Value（按 NTTP 的类型装箱）
    Option<const Class&> as_template() const;          // Kind::Template
    std::span<const TemplateArgument> as_pack() const; // Kind::Pack（逐项展开，空包 → 空 span）
    std::string_view get_spelling() const;             // "int" / "3" / "std::vector"
};
```

新增 `TypeKind::Template`（枚举尾部追加，v1 取值不变）。

> **spike S8.1 结论（GCC 16.2 实测）**
> - `std::vector<int>` 有 **2** 个实参（`int` + 默认的 `std::allocator<int>`），不是 1 个；
>   「按位置取类型实参」的示例要以真实实参表为准。
> - 参数包被**展平**成逐项实参（`std::tuple<int, char>` → 2 项），`Kind::Pack` / `as_pack()`
>   在 GCC 16.2 下永远不会产生。枚举臂保留（前向兼容），实现不写 Pack 分支，
>   文档统一写「包 = 多个独立实参」。
> - NTTP 实参：`is_value` 为真、`display_string_of` 给出 `3`；`constant_of` 之后
>   `extract<T>` 的 **T 必须与 `type_of(arg)` 完全一致**（`std::array<int,3>` 的 `3` 是
>   `unsigned long long`，`extract<int>` 抛「value cannot be extracted」）。实现按
>   `type_of(arg)` 分派装箱，不猜类型。
> - `template_of`：`has_template_arguments(primary) == false`；`identifier_of(primary)`
>   是 `"vector"`（可用作短名）；`display_string_of(primary)` 是
>   `"template<class _Tp, class _Alloc> class std::vector"`，**不能**直接当限定名，
>   `get_qualified_name()` 必须走名字拼接。
> - `substitute(template_of(T), template_arguments_of(T))` 与 `T` 的 `is_same_type` 为真，
>   四类实参（类型 / NTTP / template-template / 包）全部往返成功。


```cpp
// 用法
const Class& v = class_of<std::vector<int>>();
v.is_template_specialization();                             // true
v.get_primary_template()->get_qualified_name();             // "std::vector"
v.get_template_arguments()[0].as_type()->get_name();        // "int"

const Class& a = class_of<std::array<int, 3>>();
a.get_template_arguments()[1].as_value().unwrap().as<int>(); // 3
```

`Function` 同步获得同一组三个查询（`function_of<my_ns::id<int>>()` 能取到 `int`）；
`Function` 与 `Class` 共用 `TemplateArgument`。

### 1.3 边界

- **不按名取模板实例**。`Class::get_template_instance<T...>()` 不做：那是代码生成，
  不是反射，且 `substitute` 出的 `meta::info` 无法键在类型上（违反 design §2 的
  「物化只能键在类型 T」铁律）。
- **成员函数模板仍不进成员表**（v1 规则不变），`deducing this` 的模板函数同样排除。
- 主模板的 `Class` 是**声明级**描述：只有名字、模板形参个数、`has_canonical_name`。
  想知道成员，必须实例化后 `class_of` 特化类型。
- 主模板的限定名用「`identifier_of` + 父命名空间链」拼（S8.1：`display_string_of`
  带模板形参表，不能直接用）。
- 深特化（`std::map<std::string, std::vector<int>>`）会连带物化参数类型；
  闭包控制见 design §17。

## 2. 函数类型与可调用体

### 2.1 为什么

`class_of<void(int, double)>` 现在只到 `kind() == TypeKind::Function` 为止，连返回类型
都问不出来。C++ 里函数类型、函数指针、成员函数指针都是类型系统的一等公民，
而 `java.lang.reflect` 完全没有对应物。

### 2.2 函数类型分解

```cpp
class Class {
public:
    // 仅 is_function() 时有效（T、T&、T* 的 pointee 都可能）
    Option<const Class&> get_return_type() const;
    std::span<const Class* const> get_parameter_types() const;
    std::size_t get_parameter_count() const;
    bool is_variadic() const;        // C 可变参数 ...
    bool is_noexcept() const;
};
```

> **spike S8.4 结论（GCC 16.2 实测）**
> - 对**函数类型** `^^void(int, ...)`：`is_function_type` / `is_vararg_function` /
>   `return_type_of` 全部可用；`parameters_of` 只给固定形参（`...` 不算形参），且返回的是
>   **类型反射**（`is_type(p) == true`），不是形参实体——对函数**声明**才返回实体。
>   故 `get_parameter_types` 两种来源都要按 `is_type` 分派。
> - 对**成员函数指针类型** `^^M (C::*)(A...)`：`is_member_function_pointer_type` 可用，
>   但 `return_type_of` / `parameters_of` / `parent_of` 全部抛 `std::meta::exception`
>   （「reflection does not represent a function or function type with a return type」/
>   「... entity with parent」）。**没有**任何反射手段从 MFP 类型取 `C` 或函数类型。
> - 替代路线（已验证）：从**成员函数反射**取 `parent_of`（`C`）+ `type_of`（函数类型，
>   `const` / `&` / `&&` / `noexcept` 全保留），再拼 `F C::*` 模板并
>   `extract<F C::*>(M)` 成功；`Callable` 处理 MFP **类型**（字段值）时沿用 v1 已有的
>   `member_pointer_class<M C::*>` 类型级 trait（`src/object.cppm`），两者不冲突。


### 2.3 值层面的可调用体

字段里存着函数指针时，读出来的 `Object` 无法调用。补一个薄句柄：

```cpp
class Callable {
public:
    // 值是函数指针 / 成员函数指针 → Some；其余 → None
    static Option<Callable> of(ObjectRef value);

    const Class& get_type() const;                    // 指针类型本身
    const Class& get_function_type() const;           // 去掉指针后的函数类型
    bool is_member_function_pointer() const;          // M C::*

    Outcome<Object> invoke(Args args) const;                  // 自由函数指针
    Outcome<Object> invoke(ObjectRef self, Args args) const;   // 成员函数指针；self 必须是 C 或其派生
    Outcome<ObjectRef> invoke_ref(Args args) const;
};
```

实参匹配、返回值装箱、虚派发（成员函数指针走 `(obj->*fp)(...)` 时天然虚派发）与
`Method::invoke` 完全一致。thunk 在物化**函数指针类型**时生成，随该类型的 bundle 一起。

### 2.4 边界

- 不解析 lambda 的闭包（无 RTTI、无标准手段），只处理裸函数指针/成员指针。
- 成员函数指针的 `self` 不做向下转换，按 v1 规则只接受 `declaring_class` 及其派生（静态判定）。

## 3. 聚合构造

### 3.1 为什么

`is_aggregate()` 只给了个 bool，但没有任何按成员构造的入口。POD 配置结构是 C++ 最常见
的反射对象，而聚合初始化 `T{a, b, c}` 正是它的惯用构造方式——Java 根本没有这个概念。

### 3.2 API

```cpp
class Class {
public:
    // 按聚合初始化规则逐项构造（不经过任何构造函数）
    // 非聚合 → NotSupported；个数/类型不符 → InvalidArgument / TypeMismatch
    Outcome<Object> new_aggregate(Args args) const;

    // 伪构造函数：签名 = 聚合初始化实参类型序列，便于复用 Constructor 的匹配与诊断
    Option<Constructor> get_aggregate_initializer() const;
};

class Object {
public:
    // 编译期已知聚合的便捷式
    template <typename T, typename... Ts>
        requires std::is_aggregate_v<std::remove_cvref_t<T>>
    static Outcome<Object> make_aggregate(Ts&&... values);
};
```

规则：

- 顺序 = C++ 聚合初始化顺序：**public 基类在前**（按声明序），随后是**本类非静态数据成员**
  （按声明序）。私有/保护基类、私有/保护数据成员 → 该位置不可初始化，`NotSupported`。
- 位域成员可以按位置初始化（聚合初始化允许），走与 `Field::set` 相同的位掩码 thunk。
- 实参个数必须覆盖全部初始化项 → 少于 → `InvalidArgument`。默认成员初始化器
  （`int x = 5;`）在「被跳过」时由编译器生效，但本 API 要求写全，不模拟跳过。
- 数组类型、引用成员、`const` 成员按语言规则处理，不可初始化 → `NotSupported`。

> **spike S8.6 结论（GCC 16.2 实测）**
> - 初始化项序列 = `bases_of`（按 `is_public && !is_virtual` 过滤，声明序）+ `nonstatic_data_members_of`，
>   `Agg : B1, B2 { int x; double y; }` 得到 `(B1, B2, int, double)` 四项。
> - **槽位类型是基类本身**（`B1`），不是它的成员（`int`）：`Ty{ B1{...}, B2{...}, 3, 4.5 }` 合法
>   （基类子对象走拷贝初始化），而 `Ty{ 1, 2, 3, 4.5 }` 不合法。thunk 用
>   `std::tuple<B1, B2, int, double>` 承载实参、`typename[:T:]` + `std::get<I>` 展开，consteval 与
>   运行期两条路径同一份代码。
> - C++20 起「有 private/protected 非静态数据成员」的类**不是聚合**，`is_aggregate_type` 已挡掉；
>   `has_inaccessible_nonstatic_data_members(..., unprivileged)` 作为第二道闸（对含私有基的聚合有用）。
> - `substitute(^^std::tuple, 类型反射表)` 可直接得到实参元组类型，是「反射 → 类型世界」的正路。


## 4. 嵌套类型

### 4.1 为什么

`members_of` 里的 type 成员目前没进任何表。`Outer::Inner` 这种声明在类里的类型，
只能靠用户硬写 `class_of<Outer::Inner>()`，反射层面完全看不见。

### 4.2 API

```cpp
class Class {
public:
    // 本类内声明的类 / 联合 / 枚举（指向静态表，按声明序）
    std::span<const Class* const> get_declared_classes() const;

    // 外层类；命名空间作用域 → None
    Option<const Class&> get_enclosing_class() const;
};
```

规则：只返回**本类**声明的类型，不含继承（与 `get_declared_fields` 同一视图约定）；
访问级别由嵌套类型自己的 `modifiers()` 给出；嵌套的类模板声明（未特化）跳过，不计入。

> **spike S8.5 结论（GCC 16.2 实测）**
> - 分类顺序必须是「先 `is_type_alias`，再 `dealias` 后的 `is_class/union/enum_type`」：
>   `using AliasToNested = Inner;` 这一项 `is_class_type(alias)` 已经是 `true`（谓词会穿透别名），
>   先判 class 就会把它误当类实体。别名项要靠 `is_type_alias` + `dealias` 才能正确标注。
> - `parent_of` 对嵌套类 / 联合 / 枚举 / **别名** 都返回外层类；命名空间作用域的 `parent_of`
>   是命名空间，`has_parent` 可作分界。
> - 类模板实例里的嵌套类型：`parent_of(^^Wrapper<int>::InTemplate)` 是 `^^Wrapper<int>`
>   （实例化本身），而该嵌套类型**自己**的 `has_template_arguments` 为 `false`。
>   `substitute(^^Wrapper, {^^double})` + `members_of` 能定位到 `Wrapper<double>::InTemplate`。
> - `Class::get_name()` 对嵌套类型只有 `"Inner"`，限定名要沿 `parent_of` 拼「Outer::Inner」。


## 5. 布局与能力谓词

### 5.1 为什么

这是 v1 最薄的差异化区域：只暴露了 `is_empty` / `is_aggregate` / `is_polymorphic`。
真正决定「能否安全按字节搬运」的那组谓词缺失，直接堵死共享内存、零拷贝、FFI、
二进制序列化这几类场景——而这些恰恰是 Java 一个都给不出的。

### 5.2 API

```cpp
class Class {
public:
    // —— 生命周期与可搬运性 ——
    bool is_trivially_copyable() const;
    bool is_trivially_default_constructible() const;
    bool is_trivially_destructible() const;
    bool has_virtual_destructor() const;          // ⚠️ 见 §5.3

    // —— 布局 ——
    bool is_standard_layout() const;
    bool has_unique_object_representations() const;
    bool is_layout_compatible(const Class& other) const;
    bool is_pointer_interconvertible_with(const Class& base) const;  // 同 is_pointer_interconvertible_base_of

    // —— 库合成（无对应标准 trait）——
    bool is_bit_castable_to(const Class& other) const;   // 同 size 且双方 trivially copyable

    // —— 转换 ——
    bool is_convertible_to(const Class& other) const;
    bool is_nothrow_convertible_to(const Class& other) const;
};
```

谓词全部在物化期求值，存成 `TypeDescriptor` 的位；运行期是一次位测试。
非类类型上调用返回 `false`（不 panic），文档标注。

> **spike S8.2 结论（GCC 16.2 实测）**：九个谓词全部存在，但列表里的名字都带 **`_type`**
> 后缀（`is_trivially_copyable_type` / `is_trivially_default_constructible_type` /
> `is_trivially_destructible_type` / `is_standard_layout_type` / `is_layout_compatible_type`
> / `is_pointer_interconvertible_base_of_type` / `is_convertible_type` /
> `is_nothrow_convertible_type`；`has_unique_object_representations`、`has_virtual_destructor`
> 不带后缀）。裸名 `is_trivially_copyable(...)` 在 libstdc++ 16.2 中不存在。
> 取值在 20 余处与 `std::is_*_v` 交叉验证一致，`is_bit_castable_to` 按 §5.2 的库合成规则
> 实现（`size_of` 相等 && 双方 `is_trivially_copyable_type`）。

### 5.3 `has_virtual_destructor`

GCC 16.2 **提供** `std::meta::has_virtual_destructor`，在 11 个代表类上与
`std::has_virtual_destructor_v` 全部一致，**直接用谓词**。

~~此前的三条降级推导~~ 已作废：原第 2 条「`is_polymorphic()` 为真时析构必然为虚」是**错的**
（S8.3 反例：`struct P { virtual void f(); };` 的 `std::has_virtual_destructor_v<P>` 为 `false`
——隐式析构只有在**某个基类有虚析构**时才虚，本类有虚函数并不使它变虚，这正是
`-Wdelete-non-virtual-dtor` 的由来）。若将来需要不依赖谓词的降级路径，正确规则是：

1. 有用户声明的虚析构（`is_destructor() && is_user_declared() && is_virtual()`）→ 真；
2. 否则任一基类（递归）的虚析构为真 → 真；
3. 其余为假。

补充：GCC 的 `members_of()` 对**隐式虚析构**报 `is_virtual() == false`，不能用 `is_virtual`
反推虚性（S8.3 实测）。


## 6. 基类偏移

```cpp
struct BaseClass {
    // 非虚基：派生对象起始到基类子对象的字节偏移；虚基 / 不可访问基 → None
    Option<std::size_t> get_offset() const;
};
```

实现：物化时为每条基类关系生成 `offset_fn(void*) -> std::size_t`（就是 `cast` thunk
减一次指针差），非虚基在常量传播下退化为立即数。虚基没有静态偏移，仍只能走
`Class::cast_to`。v1「不暴露 offset」的理由（虚基无静态偏移）在区分虚/非虚后不再成立。

> **spike S8.10 结论（GCC 16.2，`-O2` 汇编核对）**
> - 非虚基 thunk 编译成 `leaq 4(%rcx), %rax`（一条 `lea` + 空指针保护），偏移是立即数；
>   `main` 里三处偏移直接折成 `$4` / `$8` / `$16` 常量。
> - 虚基 thunk 必须 `movq (%rcx), %rax; addq -24(%rax), %rcx`（读 vptr 再取 vbase offset）；
>   同一虚基在不同完整对象里偏移不同（`VA` 在 `VB` 里 16、在 `VD` 里 32），**没有静态偏移**。
> - 空基偏移 0（EBO 生效）。`get_offset()` 对虚基返回 `None` 的约定成立。
> - 陷阱：`is_virtual_base_of_type` / `is_base_of_type` 的实参顺序是 **(base, derived)**，
>   与 `std::is_*_v<Base, Derived>` 一致，与「先派生后基类」的直觉相反（S8.10 实测）。


## 7. 默认实参

### 7.1 语言层给不了

P2996 没有默认实参的反射能力，`parameters_of` 只给类型。因此默认值**只能由注解声明**，
这也是 v1 引入 `[[= expr]]` 注解体系后第一个真正的用法。

### 7.2 API

```cpp
namespace speculo {
template <typename T>
struct default_argument   // 与形参同型，仅作注解载体
{
    T value;
};
}

// 声明侧：注解给反射看，`= 0` 给编译器看，两处都要写
void f(int a, [[= speculo::default_argument{0}]] int b = 0);
```

```cpp
class Method / Constructor / Function {
public:
    Option<Annotation> get_parameter_default(std::size_t index) const;
    std::size_t get_required_parameter_count() const;   // 去掉尾部带默认值的形参

    // 按「最少实参」调用：缺的尾部实参用注解里的值补齐
    // 缺注解 → InvalidArgument（消息里指出第几个形参没有默认值）
    Outcome<Object> invoke_with_defaults(Args args) const;   // Function 无 self
};
```

规则：只补**尾部**形参；补进来的实参按 v1 的精确类型规则匹配；注解值不可读
（类型含指针/引用）→ `NotSupported`（与注解可读性判定一致）。

## 8. 值类别与移动语义

### 8.1 为什么

`ObjectRef` 没有值类别，后果是：`&&` 限定的成员函数 `is_invokable() == false`，
`T&&` 形参只能「按右值瞎转发」。C++ 的移动语义在 API 上几乎缺席；Java 没这个问题
是因为它压根没有值语义。

### 8.2 API

```cpp
enum class ValueCategory { Lvalue, Rvalue };

class ObjectRef {
public:
    ValueCategory value_category() const;
    ObjectRef as_rvalue() const;      // 同一地址，标记为右值视图
    ObjectRef as_lvalue() const;
};
```

值类别塞进 `ObjectRef` 已有的 flags 位（不改变 16 字节布局）。

> **spike S8.7 结论（GCC 16.2 实测）**：`is_lvalue_reference_qualified` /
> `is_rvalue_reference_qualified` 两个谓词都存在且互斥；`type_of(成员函数)` 保留
> `&` / `&&` / `const` / `noexcept`（显示为 `int() &`、`std::string() &&`），因此
> `extract<void (C::*)() &&>` 能精确取出。`(std::move(*p).*fp)()` 的右值语义**真的保留**：
> `std::string take() &&` 通过 thunk 调用后源对象被搬空（`s.empty() == true`）。
> `invoke_rvalue` 不需要任何编译器降级，按 §8.2 实现即可。

```cpp
class Method / Function {
public:
    Outcome<Object> invoke_rvalue(ObjectRef self, Args args = {}) const;  // 接收者按右值
    bool is_rvalue_invokable() const;   // `&&` 限定方法仅此路径可用
};

class Class {
public:
    Status assign(ObjectRef dst, ObjectRef src) const;  // 拷贝/移动赋值；不可赋值 → NotSupported
    Status swap(ObjectRef a, ObjectRef b) const;        // 需可移动赋值
    Status destroy(ObjectRef obj) const;                // 原地析构，不释放存储
};

class Object {
public:
    Status move_from(Object&& other);   // 把 other 的内容移进 this（原内容先析构）
};
```

规则：

- `Method::invoke` / `Function::invoke` 的语义**不变**（接收者按左值、`T&&` 形参仍按右值转发），
  v2 只是把「右值」变成可显式表达的视图，不破坏已有调用。
- `invoke_ref` 的返回视图类型不变；右值接收者的成员引用仍然是左值，按语言规则。
- 移动 thunk 随描述表生成（`MoveCtorFn` / `MoveAssignFn`）；不可移动 → 函数指针为
  `nullptr`，即能力缺失标志（与 v1 的 `clone` 一致）。
- `Class::destroy(obj)` 只析构、不释放：用于 placement 场景；与 `Constructor::destroy_at`
  的区别是作用于任意 `ObjectRef`，不需要知道构造签名。

## 9. 存储期与剩余修饰

```cpp
class Class / Field {
public:
    bool is_thread_local() const;                    // static thread_local 数据成员
    Option<const void*> get_static_address() const;  // 静态数据成员的地址（不含可写权限）
};

// 新增修饰位（v1 位值不变，仅在空位追加）
Modifier::THREAD_LOCAL        // 1u << 22
Modifier::NO_UNIQUE_ADDRESS   // 1u << 23，[[no_unique_address]]，信息性（偏移已如实给出）
Modifier::CONVERSION          // 1u << 24，转换运算符
Modifier::VARIADIC            // 1u << 25，C 可变参数（Method 侧补齐）

class Method {
public:
    bool is_conversion_operator() const;              // operator T()
    Option<const Class&> get_conversion_type() const; // 转换目标类型
    bool is_variadic() const;                         // C 可变参数；只列固定形参，invoke → NotSupported
};
```

`get_static_address()` 返回 `const void*`（与 v1「不提供可写裸地址」的取舍一致）；
需要写就继续用 `Field::set`。

> **spike S8.8 结论（GCC 16.2 实测）**：没有 `is_thread_local` 这个名字，对应谓词是
> **`has_thread_storage_duration`**，对 `thread_local` 命名空间变量与 `thread_local`
> 静态数据成员都给 `true`；`has_static_storage_duration` 对后者给 `false`（两种存储期互斥），
> 两者都要查。`Modifier::THREAD_LOCAL` 因此**可以**真正置位，不需要按 `INLINE`/`CONSTEXPR`
> 那样「留位不置位」。


## 10. 编译期半层：`speculo:compile`

### 10.1 为什么要单独一半

C++26 静态反射的真正卖点是编译期遍历 + splice 生成代码。v1 把这层完全封在
`detail` 里，用户只能走运行期。Java 做不到这件事，C++ 能——这是相对 Java 的结构性优势，
不该放弃。

### 10.2 分区

`export module speculo:compile;` —— **只依赖 `<meta>` 与 `std`，不 `import :object`**。
因此不违反「不要再拆模块分区」的铁律（那条铁律针对的是与 `Object`/`Class` 形成
强连通分量的分区）。纯模板 + `consteval`：无运行期状态、无描述表、无注册表。

### 10.3 API

```cpp
namespace speculo {

// 成员数量（编译期常量）
template <typename T> consteval std::size_t member_count();
template <typename T> consteval std::size_t field_count();

// 逐字段遍历：回调形状 f(std::string_view name, auto& value)
// 顺序 = 声明顺序（基类不展开）
template <typename T, typename F>
constexpr void for_each_field(T& obj, F&& f);

// 逐成员遍历（含方法）：只给名字与类别，仍不暴露 meta::info
template <typename T, typename F>
constexpr void for_each_member(F&& f);

// 编译期字段表：可直接当偏移索引用于 SoA / 序列化
struct field_info
{
    std::string_view name;
    std::size_t      offset;
    std::size_t      size;
    std::size_t      alignment;
    bool             is_bitfield;
};

template <typename T>
inline constexpr std::array<field_info, field_count<T>()> field_table{};
}
```

用法（按字段生成序列化，零运行期反射开销）：

```cpp
template <typename T>
std::string to_json(const T& obj)
{
    std::string out = "{";
    bool first = true;
    speculo::for_each_field(obj, [&](std::string_view name, const auto& value)
    {
        out += first ? "" : ",";
        first = false;
        out += std::format("\"{}\":{}", name, value);
    });
    out += "}";
    return out;
}

// 编译期字段表：可以直接当常量索引
constexpr auto offsets = speculo::field_table<Point>;
static_assert(offsets[0].offset == 0);
```

回调的第二个参数是**真实的字段类型**（`const auto&`），因此 `decltype(value)` 可用于
`if constexpr` 分派（数组、位域、嵌套结构体各走各的分支）——这正是运行期
`Field::get` 做不到的。

### 10.4 边界

- 不展开基类（与 `get_fields()` 同一约定）；要包含基类自己写递归。
- 遍历本身是 `constexpr` 展开，允许在运行期调用；回调不能是 `consteval`。
- 位域字段以「读出的值」形式传入（`auto value`），不提供引用；需要写回用 `Field::set`。
- `for_each_member` 不暴露 `meta::info`，方法只看到名字与类别；要调用仍走运行期 `Method`。

> **spike S8.9 结论（GCC 16.2 实测）**：`template for` 在**非模块的用户 TU** 可用，
> 三种形态都通：字面量 `constexpr` 数组、`std::define_static_array(反射范围)`（反射范围
> 必须先提升为 constexpr 范围，否则报「consteval-only variable `__for_range`」）、以及
> splice 出成员名。反射范围用 `define_static_array` 是硬要求。
> 128 字段大结构体（`define_aggregate` 生成）的完整遍历与偏移求和，编译开销约
> **+0.23 s**（同文件基线 1.7 s → 1.96 s，`-O0`，三次取样稳定），可接受。
> 另一条既有约束在 S8.9 复现：`consteval` 函数返回 `std::string` 时只能在
> manifestly constant-evaluated 上下文（`static constexpr` / `define_static_string`）里调用，
> 不能直接当运行期实参。


## 11. 零分配调用

### 11.1 为什么

`MethodDesc::invoke` 返回 `void*`——每次调用至少一次堆分配。对着 `std::function` 或
虚表做竞品时这是硬伤，proxy / RPC 热路径无法接受。

### 11.2 API

```cpp
class Method / Function {
public:
    // 结果直接构造进调用方存储；dst 必须按返回类型的 size/alignment 对齐
    // 返回 void → 不写 dst，只返回状态
    Status invoke_into(void* dst, ObjectRef self, Args args = {}) const;
    Status invoke_into(Object& dst, ObjectRef self, Args args = {}) const;  // dst 类型必须匹配
};
```

描述层新增 `InvokeIntoFn`（`self, args, dst`），物化时生成；`invoke` 变成
「allocate + `invoke_into` + 按 dtor thunk 收尾」的薄包装。

### 11.3 边界

- `dst` 对齐/容量不足是前置条件错误 → `panic`（不是可恢复错误）。
- 返回值是引用时 `invoke_into` 不做拷贝，返回 `NotSupported`（用 `invoke_ref`）。

## 12. 分配策略

### 12.1 API

```cpp
class Allocator {
public:
    virtual ~Allocator() = default;
    virtual void* allocate(std::size_t size, std::size_t alignment) = 0;
    virtual void deallocate(void* block) noexcept = 0;
};

class Object {
public:
    // 线程局部的默认分配器；nullptr → ::operator new / ::operator delete
    static void set_default_allocator(Allocator* allocator);
    static Allocator* get_default_allocator();
};

class Class {
public:
    Outcome<Object> new_instance_with(Allocator& allocator, Args args = {}) const;
    Outcome<Object> new_aggregate_with(Allocator& allocator, Args args = {}) const;
};
```

### 12.2 设计取舍

- `Object` 头里只存「释放函数指针 + 分配块指针」，**不存 Allocator 指针**（省 8 字节）：
  释放走生成它的分配路径留下的 deallocate thunk。
- 默认路径零开销：不设置分配器时，thunk 就是 `::operator delete` 的包装。
- 提供 `speculo::ArenaAllocator` 参考实现（单调递增 + 整体释放），用于一次反射批处理。
- **SSO 不做**。`Object` 内联存储需要 move thunk + 尺寸上限 + 「是否已内联」标志，
  复杂度高、收益有争议；v2 用分配器解决，取舍记入 design.md。

## 13. 类型身份（跨动态库）

### 13.1 问题

`Class::operator==` 比的是描述符地址。同一类型在多个 DLL / 多份静态链接副本里各物化
一份表，指针就裂开，注册表也各一份。Java 的 `Class` 是 per-loader 唯一对象，没这个坑。

### 13.2 API

```cpp
class Class {
public:
    bool operator==(const Class& other) const noexcept;   // 语义放宽（见下）
    std::uint64_t get_type_id() const;                    // 进程内稳定、跨 DSO 一致
};
```

### 13.3 规则

1. 指针相等 → `true`（热路径，绝大多数情况）。
2. 指针不等但双方 `has_canonical_name()` → 比 `get_normalized_name()`；相等即视为同一类型。
3. 其余 → `false`。

`get_type_id()` 由注册表按规范名分配（FNV-1a 哈希 + 冲突链），同一个名字在任意 DSO
里得到同一个 id。这是「跨 DSO 做键」的推荐方式：`operator==` 最坏退化为字符串比较，
`get_type_id()` 恒为整数比较。

**已知限制**（文档必须写死）：身份统一 ≠ thunk 通用。A 库物化出的 `Field` 作用在
B 库构造的对象上，依赖两边布局与调用约定一致；跨编译器 / 跨 ABI 版本不保证。

## 14. `std` 深物化白名单

v1 的浅物化是硬断点：`class_of<std::string>()` 是空壳，用户第一天就会撞上。

```cpp
#include <speculo/macros.h>

// 命名空间作用域；显式深物化一个 std 特化，只影响该特化自身
SPECULO_DEEP(std::vector<int>);
SPECULO_DEEP(std::string);
```

规则：

- 默认不变（`std` 仍是浅物化），避免一个字段把整棵 `basic_string` 签名闭包编进来。
- 宏触发一次 `detail::deep_materialize<T>()`，把该特化的字段/方法/构造/基类**按用户类型规则**
  物化；不传播到成员类型（`std::vector<int>` 的元素类型仍是浅的，除非也标注）。
- 重复标注幂等；`SPECULO_DEEP` 与 `class_of<T>()` 首次调用都可作为激活点。
- 代价可观测：`get_size()` 不变，成员表变大；编译期爆炸由 design §17 的成员上限诊断兜底。

## 15. 关系图：继承与虚函数覆盖

### 15.1 API

```cpp
class Class {
public:
    // 注册表里有记录的直接派生类 / 全部后代（顺序不保证，按注册顺序）
    std::vector<const Class*> get_derived_classes() const;
    std::vector<const Class*> get_all_derived_classes() const;

    bool is_derived_from(const Class& base) const;        // 传递闭包，仅 public 路径
    Result<std::vector<const Class*>, Error> get_conversion_path(const Class& base) const;
        // 向上多跳路径（含 identity）；0 条 → TypeMismatch，>1 条 → Ambiguous
};

class Method {
public:
    Option<Method> get_overridden_method() const;      // 沿基类链找同名、同形参退化类型、同 cv
    std::vector<Method> get_overrider_methods() const; // 注册表内所有覆盖它的方法
    Option<std::size_t> get_vtable_index() const;      // ⚠️ 待 spike，可能恒为 None
};
```

### 15.2 规则

- **派生查询只覆盖已注册类型**：`class_of<T>()` 调用过或 `SPECULO_REGISTER` 标注过的。
  文档必须显著写死，否则用户会以为本库是全知的。
- 注册表 v2 增加**反向边索引**（base → derived），插入时维护，查询 O(1) 取列表。
- `is_derived_from` 只沿 public 基路径（与 `cast_to` 无 RTTI 的取舍一致）。
- `get_overridden_method` 的匹配口径：名字 + 形参**退化**类型序列 + `const` 限定；
  协变返回不算覆盖（判定需要向上转换，留作 v3）。
- `get_overrider_methods` 同样受注册范围限制。

## 16. 诊断

```cpp
class Class {
public:
    std::string describe() const;   // 多行：名字/尺寸/对齐/修饰符/字段与方法签名
};

std::string to_string(ObjectRef value);   // 逐字段格式化；不可格式化字段写 "<unprintable>"
```

`std::formatter<speculo::Class>` / `std::formatter<speculo::Field>` / `std::formatter<speculo::Object>`
一律委托给上述函数。格式化走 `requires { std::formatter<T>{}; }` 检测，不引入异常；
`Object` 为空 → `"<null>"`。

## 17. v2 明确不做

1. 成员函数模板、类模板的按名实例化（§1.3）。
2. 命名空间枚举、命名空间作用域变量（除静态成员地址，§9）。
3. 协变返回的覆盖判定、真实的 vtable slot 编号（§15.2）。
4. 向下 / 交叉转换（仍需 RTTI）。
5. 跨 DSO 的 thunk 互换（§13.3 限制）。
6. `Object` 的 SSO（§12.2）。
7. 注解驱动的成员注入、条件注解（P3394 §4.5 未进 C++26，v1 已排除，v2 不变）。
