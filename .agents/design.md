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

---

# 第二部分：v2 设计

> v1（§1–§11）解决的是「把 Java 反射搬到 C++」，已全部落地。v2 解决的是
> 「Java 反射给不出、而 C++ 必须有的东西」。API 形态见 @.agents/api_design.md，
> 排期见 @.agents/phases.md「Phase 8 起」。

## 12. v2 定位：补上 C++ 的另一半

v1 用 21 个修饰位、尺寸/偏移/位域、cv/引用分解，证明了「C++ 特有信息」这条路可行。
但缺口集中在三处，性质不同：

| 层 | 缺什么 | 为什么 v1 没覆盖 |
|---|---|---|
| 类型系统建模 | 模板实参、函数类型、聚合构造、嵌套类型、布局能力谓词 | v1 只建模了「对象类型」的**成员**，没建模「类型是怎么构造出来的」 |
| 操作语义 | 值类别、移动、聚合初始化、默认实参、存储期 | v1 的对象模型借自 Java（引用 + 装箱值），移动语义无处安放 |
| 工程化 | 编译期半层、零分配、分配器、跨 DSO 身份、诊断、关系图 | v1 的目标是「功能闭环」，不是「能上线」 |

三条判据，任何 v2 特性都要过：

1. **Java 给不出**（否则只是补作业）；
2. **C++ 必须要**（否则是猎奇）；
3. **无 RTTI / 无异常也能做**（否则违反 §1 的硬约束）。

## 13. 描述层扩展：从「类型身份」到「类型结构」

v1 的 `TypeDescriptor` 只回答「这是什么类型、有哪些成员」。v2 追加三组字段，
仍然**扁平追加、不引入继承/变体/虚函数**：

- **结构组**：`template_args` / `primary_template` / `function_return` / `function_params` /
  `function_flags`（variadic, noexcept）/ `nested_types` / `enclosing`。
- **能力组**：一组 `bool` 位（trivially_* / standard_layout / layout_compatible /
  unique_object_representations / pointer_interconvertible / has_virtual_dtor /
  convertible_to…）。求值全在物化期，运行期是位测试。
- **操作组**：`move_ctor` / `move_assign` / `swap` / `destroy` / `offset_of_base` /
  `invoke_into` 等新 thunk。约定不变：**nullptr = 能力缺失**。

新增三个子描述：`TemplateDesc`（主模板的声明级信息）、`FunctionTypeDesc`（函数类型分解）、
`CallableDesc`（值层可调用体：函数指针/成员指针的调用 thunk）。它们都由
`TypeDescriptor` 用指针引用，不改变「句柄是指向描述项的 8/16 字节值」这一性质。

两条铁律继续适用：

- 所有跨类型的引用在 bundle 初始化器里必须用**依赖拼写**（`bundle<Arg>`），
  禁止具体拼写（见 @.agents/review.md「Phase 4」雷区）。
- 不再新增与 `:object` 互相引用的模块分区。`TemplateArgument` / `Callable` 并入 `:object`。

**主模板的 `Class` 是声明级描述**：`TypeKind::Template`、size/alignment 为 0、成员表为空。
理由：主模板的成员类型依赖模板参数，物化它等于物化一个不存在的类型；而 `substitute`
出来的 `meta::info` 无法键在类型上（§2）。

## 14. 编译期半层的分层位置

v2 新增 `speculo:compile` 分区，放在分层图的最上层旁边，**不是中间**：

```
┌──────────────────────────────────────────────┐
│ 编译期层  for_each_field / field_table /      │  ← 新：纯 constexpr，无运行期状态
│           member_count（speculo:compile）     │
├──────────────────────────────────────────────┤
│ API 层    Class / Field / Method / …          │
└──────────────────────────────────────────────┘
```

关键设计：**收集规则只有一份**。`detail` 里抽出一个纯 `std::meta` 的「成员视图」
（`collect_fields<T>()` / `collect_methods<T>()`，返回 `std::array<meta::info, N>`），
运行期物化与编译期遍历都消费它。这样「哪些成员可反射、访问如何过滤、`std` 如何浅物化」
永远只有一处定义，不会两边漂移。

为什么不把编译期层做成运行期 API 的快路径：运行期需要类型擦除（`void*` + 描述符），
编译期需要类型保真（真实 `T&`）；合并会让两边都变慢，还会把 `meta::info` 拖进用户代码。
二者是两条独立的路，共享同一个收集器。

分区边界安全：`:compile` 只 `import std` + `<meta>`，**不 import `:object`**，
因此不会形成 §4 铁律所指的强连通分量。代价是它不能复用 v1 的句柄类型；
`FixedString` 需要随之下沉到一个不含反射的小分区，实现时定。

## 15. 操作语义：值类别、移动、零分配

### 15.1 值类别进 `ObjectRef`

`ObjectRef` 现在 16 字节：`const void* data` + `const TypeDescriptor* type`，
const/volatile 状态另存。v2 把 **value category** 放进同一批标志位，**布局不变**。

- `ObjectRef::as_rvalue()` 只是打标记，不搬数据。
- `Method::invoke` 的默认语义不变（接收者左值）；新增 `invoke_rvalue` 走右值路径，
  `&&` 限定的成员函数只在这条路径可用。
- `T&&` 形参仍按右值转发（v1 行为），但从此**可解释**：转发的是 `Args` 里的右值视图。

### 15.2 移动与对象操作

新增 `MoveCtorFn` / `MoveAssignFn` / `SwapFn`，与 `CloneFn` / `ReleaseFn` 同一 ABI 风格
（`void*(void*)` / `bool(void*, void*)`）。`Class::assign` / `swap` / `destroy` 是它们的
薄包装；`Object::move_from` 是 `destroy + move_ctor` 的组合。

不做 `Object` 的 SSO：内联存储要求每个 `Object` 携带「是否内联」标志与容量，
并把 `ReleaseFn` 变成两种语义（释放堆块 / 调用内联析构），复杂度换不到确定收益。
分配器（§15.3）覆盖同一需求。

### 15.3 零分配与分配器

- **零分配**靠 `InvokeIntoFn`（`self, args, dst`）：结果直接构造进调用方存储。
  `invoke` 退化为 `allocate + invoke_into + dtor`。
- **分配器**是线程局部的默认指针 + 每次调用可指定的 `Allocator&`。
  `Object` 头里**不存** Allocator 指针，只存释放 thunk —— 省 8 字节，且让 arena
  的「整体释放」语义自然成立（deallocate 可以是空操作）。
- 默认路径（不设分配器）保持 `::operator new`，零额外分支。

## 16. 身份：跨 DSO 与注册表 v2

`Class::operator==` 从「指针相等」放宽为三分支：

1. 描述符指针相等 → true（热路径）；
2. 都不等但都有规范名 → 比 `normalized_name`；
3. 否则 false。

代价：最坏 O(名字长度)。因此同时提供 `get_type_id()`（FNV-1a + 冲突链，注册表分配），
给「跨 DSO 做键」的场景一个整数比较。文档必须写死：**身份统一 ≠ thunk 通用**——
A 库物化出的 `Field` 用在 B 库构造的对象上，依赖两边布局与调用约定一致。

注册表 v2 加**反向边索引**（base → derived），插入时维护。这让
`get_derived_classes()` / `get_overrider_methods()` 成为 O(1) 取列表。同时必须在所有
相关 API 上写明口径：**只覆盖已注册类型**。这是本库与 Java 最大的语义差异之一
（Java 的 `Class` 全知），含糊过去会变成用户踩不完的坑。

## 17. 物化成本与闭包控制

v1 的成本源有三个，v2 全部放大：

| 成本源 | v1 表现 | v2 放大点 | 控制手段 |
|---|---|---|---|
| `std` 签名闭包 | 一个 `std::string` 成员会拖进整棵 `basic_string` | 深物化白名单 | 默认浅物化不变；`SPECULO_DEEP(T)` 显式开启，不传播 |
| 成员函数体实例化 | 物化会实例化成员函数体（§11.8） | 模板特化更多，闭包更大 | 保持 `reflect_impl_tag` 式注解排除；新增类型照此办理 |
| 模板实参递归 | 无 | `map<string, vector<int>>` 会连带物化多个类型 | 只物化「实参类型本身」，不物化其成员（除非它是用户类型且被显式 `class_of`） |

新增一条编译期护栏：单个类型的成员数 / 模板实参数超过阈值（默认 512 / 32）时
`static_assert` 失败，消息里带类型名，提示 `SPECULO_DEEP` 或拆分类型。目的是把
「编译一晚上」变成「编译时一句话」。

## 18. 兼容性与迁移

- **源码级兼容**，不承诺二进制兼容（模块 + `constexpr` 表，本来也没有 ABI 契约）。
- 唯一语义变更：`Class::operator==` 放宽。把 `Class` 当 `std::unordered_map` 键的代码
  需要改哈希（用 `get_type_id()`）；v2 不提供 `operator<`，有序容器同样建议用 id。
- 新增枚举值（`TypeKind::Template`）与修饰位（`THREAD_LOCAL` … `VARIADIC`）只追加，
  v1 取值不变。
- 构建系统对等要求不变：新分区 `speculo:compile` 必须同时进 `CMakeLists.txt` 的
  `CXX_MODULES` 文件集与 `xmake.lua` 的 target 源文件（@AGENTS.md）。
- v2 完成后的定位一句话：**运行期可枚举、编译期可展开、跨 DSO 可识别，且不需要 RTTI
  与异常**——这四件事 Java 一件也做不到，RTTI 方案也一件也做不到。
