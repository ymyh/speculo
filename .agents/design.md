# speculo 总体设计

## 1. 定位与目标

speculo 把 C++26 的**静态反射**（编译期、`consteval`、`std::meta::info`）包装成一套**动态反射**（运行期、类型擦除、可按名字查找/调用）的 Java 风格 API。

设计目标：

1. **Java 风格**：`Class` / `Field` / `Method` / `Constructor` / `Modifier` / `Array`，API 词汇取自 Java（`get_name()`、`get_declared_fields()`、`invoke()`、`new_instance()`），但书写遵循 C++ 蛇形命名惯例：类名大驼峰、方法与自由函数 snake_case。
2. **C++ 特有信息**：Java 反射给不出来的东西是本库的差异化价值——`sizeof`、对齐、字段字节偏移与位偏移、位域宽度、cv 限定、指针/引用层级、数组维度、ref-qualifier、noexcept、explicit、deleted/defaulted、枚举底层类型与底层值等。
3. **零运行时初始化成本**：所有反射信息在编译期物化为 `constexpr` 描述表，运行期只做指针跳转；不依赖 `__cxa` / RTTI / 异常。
4. **模块化**：整个库是一个 C++ 模块 `speculo`；依赖同为模块的 `crab_cpp` 提供 `Option` / `Result`。
5. **单编译器目标**：只面向 GCC >= 16（`-freflection`）。不为其他编译器做兼容层，保持实现简单。

非目标：

- 不做代码生成 / 外部 IDL / 宏标注驱动的反射（纯标准静态反射，用户类型零侵入）。
- 不做跨编译器抽象层。
- 不反射未实例化的模板本身（只反射具体类型）。

## 2. 核心概念：物化（Materialization）

静态反射的产物 `std::meta::info` 只能存在于常量求值期，无法在运行期传递。动态反射的本质是一次**受控的物化**：

```
编译期（consteval）                        运行期
-------------------                        -------------------
template <typename T>                      const Class& cls = speculo::class_of<T>();
consteval auto m = materialize<T>()  ==>   cls.get_field("x").get(obj)
  members_of(^^T, ctx) 遍历成员            —— 通过描述表里的类型擦除 thunk 完成
  为每个成员生成描述项 + thunk 函数指针
  汇总为 constexpr 静态数组
```

具体做法：

- `template <typename T> consteval` 函数用 `std::meta::members_of` / `nonstatic_data_members_of` 等（以 GCC 16 实际实现的 P2996 修订为准）收集成员。
- 对每个成员（字段/方法/构造函数/枚举项/基类），在编译期**合成一个无捕获 lambda 或函数模板实例**作为 thunk，例如：
  - 字段 getter：`+[](const void* self) -> Object { return Object::from_value(static_cast<const T*>(self)->[:member:]); }`（示意）
  - 方法 invoker：解包 `std::span<const Object>` 实参、检查类型、调用、打包返回值。
  - 构造函数：按参数包展开 `new T(args...)`；析构器：`destroy_at`。
- 描述项是平凡的结构体（`string_view` 名字、`size_t` 尺寸/偏移、`Modifier` 位掩码、thunk 函数指针、指向其他描述项的指针），组成 `constexpr std::array`，挂在函数内静态存储上。
- 运行期的 `Class` / `Field` / `Method` 只是**指向这些描述项的轻量句柄**（可拷贝、比较、跨 TU 传递）。

这样做的好处：每种类型的反射信息全程序只存在一份；`class_of<T>()` 就是取静态局部地址，O(1)；描述表天然只读、线程安全。

## 3. 分层架构

```
┌─────────────────────────────────────────────┐
│ API 层      Class / Field / Method /         │  ← 用户直接接触，均为只读句柄
│             Constructor / Function /         │
│             Modifier / Array                 │
├─────────────────────────────────────────────┤
│ 对象层      Object（owning）/ ObjectRef      │  ← 类型擦除的值与引用
├─────────────────────────────────────────────┤
│ 描述层      TypeDescriptor / FieldDesc /     │  ← constexpr 静态表，内部细节
│             MethodDesc / FunctionDesc /      │
│             CtorDesc / EnumDesc              │
├─────────────────────────────────────────────┤
│ thunk 层    getter/setter/invoker/ctor/dtor  │  ← 编译期生成的类型擦除函数指针
├─────────────────────────────────────────────┤
│ meta 层     consteval 收集：std::meta::*     │  ← 唯一接触 <meta> 的地方
└─────────────────────────────────────────────┘
         依赖：crab_cpp（Option / Result / panic）
```

依赖方向自上而下，禁止反向。meta 层和 thunk 层是唯一直接使用 `<meta>` 与拼接（splice）语法的地方；上层永远见不到 `std::meta::info`。

## 4. 模块划分

主模块 `speculo` 按分区组织（实际落地以本节为准；`:type`/`:field`/`:method` 因
GCC 模块依赖规则（详见 @.agents/review.md「Phase 3」偏差 1）收敛进 `:object`）：

| 模块分区 | 内容 |
|---|---|
| `speculo.error` | `ErrorKind`、`Error`、`Status`、`Result<T>` 别名 |
| `speculo.modifier` | `Modifier`（Java 风格位掩码工具类）、`RefQualifier`、`TypeKind` |
| `speculo.descriptor` | 描述层结构体（`TypeDescriptor`/`FieldDesc`/`MethodDesc`/...）与 thunk ABI（内部） |
| `speculo.object` | `Object` / `ObjectRef` / `Args` / `Class` / `Field` / `Method` / `Constructor`（Phase 3/4） |
| `speculo.function` | `Function` / `function_of` / `overload_of`（自由函数，点名物化） |
| ~~`speculo.ctor`~~ | `Constructor` 已并入 `speculo.object`（模块分区拆分限制，见 review.md Phase 3/4 偏差） |
| ~~`speculo.array`~~ | `Array` 已并入 `speculo.object`（沿用「不拆模块分区」约束，见 review.md Phase 5 偏差 4-8） |
| `speculo.registry` | 名字 → `Class`、签名 → `Function` 的全局注册表，`Class::for_name` / `Function::for_name` |
| `speculo`（主接口） | `export import` 以上所有分区 + 入口函数 `class_of<T>()` |

内部实现（物化逻辑、thunk 生成）放 `speculo::detail`（见 `src/object.cppm` 与
`src/function.cppm` 内的 `module; #include <meta>` 全局模块片段 + `detail` 命名空间）。
`:function` 依赖 `:object`，`:registry` 再依赖 `:function`，避免分区环。

## 5. 对象模型与生命周期

Java 里一切皆堆对象、有 GC；C++ 不行，因此区分两种类型擦除句柄：

- **`Object`**：owning。类似带类型描述的 `std::any`：内部保存值存储（SSO 可选，初版直接堆分配）+ 指向 `TypeDescriptor` 的指针，析构时通过 descriptor 里的 dtor thunk 销毁。拷贝/移动是否可用取决于被包类型是否支持（描述表中有相应 thunk，缺失则返回 `ErrorKind::NotSupported`）。
- **`ObjectRef`**：non-owning。`void*` + `const Class*`，用于指向已有对象或其子对象（字段 `get_ref` 的结果、数组元素等）。生命周期由调用者保证，文档中明确标注。
- **`Args`**：实参集合（`ObjectRef` 的 SSO 容器，内联 8 个、超出堆分配）。解决 `std::span<const ObjectRef>` 无法从 `Object` 隐式构造的问题；`invoke` / `new_instance` / `construct_at` 统一收 `Args`，调用端直接写 `invoke(obj, {a, b, c})` 即可混合传 `Object`、具体类型引用等一切可转 `ObjectRef` 的东西。

调用链统一走 `ObjectRef`：`Method::invoke(ObjectRef self, Args)`、`Field::get(ObjectRef)`；`Object` 可隐式转出 `ObjectRef`。

> **不变式（对象句柄只描述对象，不描述类型）**：`Object`/`ObjectRef` 的 `type` 永远是**对象类型**
> （referent 类型），**绝不携带引用类型**——运行期没有「引用对象」，`T&` 只是别名，`ObjectRef`
> 本身就是那个「引用/视图」机制。
> 引用类型因此只作**声明类型**出现在 `Field`/`Method`/`Constructor`
> （`Field::get_type()`、`get_parameter_types()`、`get_return_type()`）。
> 于是 `class_of<int&>` 与 `class_of<int>` 是**不同**的 `Class`（`TypeKind::Reference`，用于按类型选重载），
> 但 `ObjectRef(x)` 与 `ObjectRef(x 的某个 int& 别名)` 的 `type()` **都**是 `class_of<int>`。
> 详见 @.agents/review.md「Phase 5」。

## 6. 错误处理模型

全面采用 crab_cpp：

- **查找类**（可能不存在）：返回 `crab_cpp::Option<T>`，如 `Class::get_field(name) -> Option<Field>`。
- **操作类**（可能失败）：返回 `crab_cpp::Result<T, speculo::Error>`，如 `Method::invoke(...) -> Result<Object, Error>`。
- 无返回值的写操作返回 `Status`（`Result<std::monostate, Error>` 的别名，`None` 即成功）。
- `Error` = `ErrorKind` 枚举 + `std::string` 消息（含类型名、成员名等上下文）。
- 真正的程序错误（违反前置条件，如对空 `ObjectRef` 调用）才用 `crab_cpp::panic`。

## 7. 访问控制与 declared/non-declared 语义

照搬 Java 的两级视图，并与 P2996 的 `std::meta::access_context` 对应：

- `get_fields()` / `get_methods()` / `get_constructors()`：public 成员。
- `get_declared_fields()` / `get_declared_methods()` / `get_declared_constructors()`：本类声明的全部成员，含 private/protected，不含继承。

> **实现取舍（相对 Java 的有意偏差）**：`get_fields()`/`get_methods()` 只返回**本类**声明的
> public 成员，**不**自动聚合继承链（Java 的 `getFields()`/`getMethods()` 会自动包含继承来的
> public 成员）。继承成员统一通过 `get_bases()` / `get_superclass()` 显式上溯后再查询；
> Java 的单继承 `getSuperclass()` 对应 `get_superclass()`（唯一非虚基类），C++ 多继承/虚基
> 用 `get_bases()`。详见 @.agents/review.md「Phase 3」偏差 11。

私有成员的物化依赖 `access_context::unchecked()`（或 GCC 16 提供的等价能力）。若 GCC 16 的实现对私有成员的 splice/extract 仍有限制，则 `get_declared_xxx` 退化为仅返回可访问成员，并在文档与 `review.md` 中记录偏差。**这是本项目最大的实现风险点**，第一阶段就要用 spike 验证。

## 8. 注册表与 `Class::for_name` / `Function::for_name`

静态反射以类型为入口，没有“按字符串取类型”的天然能力，因此提供显式注册表：

- `class_of<T>()` 首次调用时自动把 `T` 注册进全局表（qualified name → descriptor 指针）。
- 另提供 `SPECULO_REGISTER(T)` 宏应对“只按名字查找、从不 `class_of<T>()`”的场景。
- `Class::for_name("ns::Foo") -> Option<const Class&>`。
- 自由函数没有类型这个根，不能枚举。`function_of<f>()` 首次调用时按**签名**注册
  （`Function::get_signature()` → descriptor 指针）；`Function::for_name` 用这串签名查。
  没人点过名的函数不在表里。
- 两张表都是运行期只增不改。实现：各自为**函数内 magic static**（首次访问才构造，天然线程安全），
  用 `std::shared_mutex` 保护；`class_of<T>()` / `function_of<f>()` 内各挂一个 magic static，保证每个实体只注册一次。
  描述表自身仍保持只读、纯 `constexpr`，注册只是其外的一次性副作用。
  （§10「零运行时初始化成本」指描述表物化；注册表是唯一的可变全局状态，见 §8/§10。）

## 9. 构建系统设计

两套构建系统保持功能对等，均产出模块库 `speculo`：

**CMake**（对齐 crab_cpp 的做法）：

- `cmake_minimum_required(VERSION 3.31)`，Ninja 生成器。
- 设置 `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD` gate 值以启用 `import std;`。
- `set(CMAKE_CXX_STANDARD 26)` + `CMAKE_CXX_MODULE_STD ON`。
- GCC 专属编译选项：`-freflection`（P2996）+ `-fno-rtti -fno-exceptions`（全项目禁用 RTTI 与异常）。
- 用 `FetchContent` 引入 crab_cpp（`https://github.com/ymyh/crab-cpp`）。
- 模块源文件通过 `FILE_SET CXX_MODULES` 挂到 `speculo` target。
- `option(SPECULO_BUILD_TEST ...)` 拉 GoogleTest 跑 `tests/`。

**xmake**：

- `set_languages("c++26")`，GCC 加 `-freflection`。
- 按 crab_cpp README 的样板写 `package("crab_cpp")` + `add_requires("crab_cpp")`。
- target 里 `add_files("src/**.cppm")` + `add_packages("crab_cpp")`。

**目录结构（规划）**：

```
speculo/
├── AGENTS.md
├── .agents/            # 本文档等
├── CMakeLists.txt
├── xmake.lua
├── include/
│   └── speculo/
│       └── macros.h    # SPECULO_IMPL / SPECULO_IMPL_VIRTUAL / SPECULO_REGISTER（宏不能进模块）
├── src/
│   ├── mod.cppm        # export module speculo（主接口）
│   ├── error.cppm
│   ├── modifier.cppm
│   ├── descriptor.cppm
│   ├── object.cppm     # Object/ObjectRef/Args/Class/Field/Method/Constructor + 物化
│   ├── function.cppm   # Function / function_of / overload_of（自由函数）
│   └── registry.cppm   # 名字 → Class，签名 → Function
├── tests/
└── examples/
    └── proxy_demo.cpp  # proxy 库集成示例（Phase 7）
```

## 10. 性能与线程安全

- 物化发生在编译期/首次取静态局部时（magic static，C++11 起线程安全）。
- 所有描述表 const，`Class`/`Field` 等句柄无内部状态，任意线程并发只读安全。
- 唯一的可变全局状态是两张注册表（类型名、函数签名），见 §8。
- `Method::invoke` / `Function::invoke` 等热路径 = 一次 `Args` 类型检查 + 一次函数指针调用 + 返回值打包；不做字符串比较（重载解析按 `Class` 指针比较）。
- 三级查找策略按速度递减：`get_method<Params...>(name)`（编译期物化参数指针数组，运行期纯指针比较）> `get_method(name, {&cls...})`（运行期指针比较）> `get_method(name, {"type_name"...})`（注册表字符串解析后再指针比较）。热路径应优先用模板重载。

## 11. 限制与已知边界

1. **仅 GCC 16+**，且其实现为“部分 P2996”——凡用到 GCC 尚未实现的元函数（如某些 `access_context` 能力、`enumerators_of` 细节），以编译验证为准，偏差记录到 `review.md`。
2. 位域不能取地址，读写必须走专门 thunk（按位掩码操作），`Field::get_ref` 对位域返回 `ErrorKind::NotSupported`。
3. 虚基类的指针调整不是常量偏移，基类转换统一走 thunk 而非暴露 offset；`cast_to` 仅做**向上**转换
   （向下/交叉转换需 RTTI，本项目无 RTTI → 不支持），且只沿 public 基路径。`is_instance` 因此是静态类型判定。
4. 无默认构造/不可拷贝类型的 `Object` 拷贝、数组 `Array::newInstance` 等操作在运行期报 `NotSupported`（thunk 为 nullptr 即能力缺失标志）。
5. 函数局部类型、匿名命名空间类型可反射但无法注册到全局表（名字不可寻址），`for_name` 查不到属预期行为。
6. 不反射：模板本身、宏、typedef/别名（别名直接解析到底层类型，`using` 信息丢失属预期）。
7. **`std` 类型浅物化**：声明在命名空间 `std`（含 `std::__cxx11` 等内联命名空间）里的类、联合、枚举只保留身份和值操作（名字、大小、对齐、拷贝/析构/赋值，以及 `Object` 装箱所需的 thunk），**不枚举**其字段、方法、构造函数、基类、枚举项。用户类型里的 `std::string` 字段、以及签名用到 `std::` 类型的成员函数，仍然正常反射、可 `get` / `set` / `get_ref` / `invoke`；只是不能再通过 `class_of<std::string>()` 去调用 `size()` 这类标准库成员。这是为了避免一个 `std::string` 成员把 `basic_string` 的整棵签名闭包编进程序。
8. **反射库类型（尤其类模板特化）可能不可物化**：物化会为每个成员函数生成 invoker（`extract` 成员指针），
   这会实例化其函数体。若某特化的成员函数体对其自身无效（典型：`Option<move-only T>::or_` 内 `return *this;`
   需要拷贝），物化即硬错误。故 `SPECULO_IMPL` 注入的 `get_class()` 用注解排除（见 §10.1），避免把整个
   `Class` 的签名闭包拖进来；直接 `class_of<crab_cpp::Option<Object>>()` 仍会失败。用户应只反射自己的类型。
9. **自由函数是点名入口，不挂在 `Class` 上**。`function_of<f>()` 物化用户点名的那一个非成员函数；
   重载用 `function_of<overload_of<形参...>(f)>()` 消歧。不枚举命名空间，不做 ADL，不补默认实参，
   不反射函数模板本身（点名一个具体特化可以）。`function_of<^^f>()` 与 `function_of<f>()` 是同一个函数。
   GCC 16.2 上 `= delete` 的自由函数不能点名：把 `^^f` 传入模块模板会 odr-use 它。
   成员函数里的 `= delete` 仍由类的成员表列出。内部链接（`static`、匿名命名空间）可以 `invoke`，
   但不进 `Function::for_name`。`consteval` 函数没有可用的 `<meta>` 谓词，点名可能直接编不过。
   成员函数仍走 `Method`。
