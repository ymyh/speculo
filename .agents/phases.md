# speculo 开发计划

> 原则：**风险前置**。GCC 16 的 P2996 是部分实现，所有不确定性必须在正式开发前用 spike 验证完毕。
> 每个阶段结束时更新 @.agents/review.md（记录验证结果、偏差、设计调整）。
> 构建系统（CMake + xmake）从 Phase 0 起就保持双系统对等，不许出现"CMake 能编 xmake 不能编"的债务。
>
> **新接手开发请直接跳到文末「交接备忘」与「Phase 7 接手要点」**；上面 Phase 0–6 的清单是完成记录。

## Phase 0：工程骨架（0.5 天）

目标：空模块能编过，`import speculo;` 可用。

- [x] `CMakeLists.txt`（GCC 16 + Ninja + `-freflection` + `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD`，FetchContent 引入 crab_cpp）
- [x] `xmake.lua`（对齐 crab_cpp 官方 package 样板，`set_languages("c++26")` + `-freflection`）
- [x] `src/mod.cppm`（主模块 `export module speculo;`，暂时空导出）
- [x] `tests/` 目录 + 一个 hello-world 测试（GoogleTest via FetchContent / `add_requires("gtest")`）
- [x] 双系统均编译通过、测试能跑

**验收**：`cmake -B build -G Ninja && cmake --build build && ctest` 与 `xmake && xmake test` 都绿。

## Phase 1：Spike——GCC 16 P2996 能力验证（1~2 天，**关键路径**）✅

> 已完成，详细结论见 @.agents/review.md「Phase 1」一节。一句话：
> GCC 16.2 + libstdc++ 16.2 实现了基本完整的 P2996，S1~S7 全部 ✅
> （个别为 ⚠️ 语义/命名偏差，无 ❌）。成员访问拼接 `obj.[:M:]` 在 16.2 已不 ICE，
> 故字段/位域访问比 dynref 的 offset+launder 方案更简单。

目标：把 api_design.md §12 的待验证事项全部变成确定结论。每个问题写最小可编译程序验证，结果记入 review.md。

- [x] **S1. access_context**：`unchecked()` 存在；命名空间作用域即可枚举/拼接/extract private 成员（无需 dynref 的宏方案）
- [x] **S2. 成员枚举**：`members_of`/`nonstatic_data_members_of` 均带 `access_context`；运算符/转换/析构被枚举但无 identifier，需 display_string_of + symbol_of；含隐式特殊成员
- [x] **S3. 数据布局**：`offset_of` 返回 `member_offset{bytes,bits}`（无 `bit_offset_of`，位偏移用 `total_bits()`）；位宽用 `bit_size_of`；`size_of/alignment_of` 不接受成员反射
- [x] **S4. thunk 合成**：字段/位域/静态/引用成员读写走成员访问拼接；invoker 走 `extract<精确成员函数指针>`；构造用 `new Self(args...)`；基类转换（含虚基）用 `static_cast` 链——全部可行
- [x] **S5. 枚举**：`enumerators_of`/`underlying_type` 可用；取值须 `extract<枚举类型>(e)`
- [x] **S6. 函数签名**：`parameters_of`/`return_type_of` 与全部 cv-ref-noexcept 谓词可用；`substitute(^^std::tuple, ...)` 回类型世界
- [x] **S7. 类型谓词**：P2996 类型谓词全集可用（含 is_polymorphic/abstract/final/aggregate/empty）

**验收**：review.md 中每个 S 项有明确结论（✅ 可用 / ⚠️ 有偏差（写明偏差与应对）/ ❌ 不可用（写明降级方案））；api_design.md §12 逐条关闭；若 S1/S4 出现 ❌，先调整 api_design.md 再继续。

## Phase 2：基础层（2~3 天）✅

> 已完成：`speculo:error` / `:modifier` / `:descriptor` / `:type` / `:object` 五个分区落地，
> CMake 与 xmake 双系统编译通过、`phase2_test.cpp` 全绿（16 用例）。偏差见 @.agents/review.md「Phase 2」。

目标：不依赖反射的纯运行期组件先行落地，带完整测试。

- [x] `speculo.error`：`ErrorKind` / `Error` / `Status` / `Outcome<T>`（并重导出 `crab_cpp` 的 `Option`/`Result`/`None`/`panic`）
- [x] `speculo.modifier`：`Modifier`（21 位 + `is_xxx()` + `to_string()`）、`RefQualifier`、`TypeKind`
- [x] `speculo.object`：`ObjectRef`（void* + Class*）、`Object`（owning，堆存储 + dtor thunk）
- [x] `Args`：SSO 容器（内联 8 个 ObjectRef，超出堆分配），构造/移动/访问全套测试（含 >8 参数降级路径）
- [x] 描述层结构体定义（`TypeDescriptor` / `FieldDesc` / `MethodDesc` / `CtorDesc` / `BaseDesc` / `EnumDesc`——完整前瞻布局；物化逻辑留给 Phase 3）
- [x] 附：最小 `Class` 句柄（`speculo:type`，身份/分类/尺寸/对齐/修饰符）与 `class_of<T>()` 入口（Phase 2 用纯模板 descriptor 占位，Phase 3 换真物化）

**验收**：上述组件单测全绿；`Object`/`ObjectRef`/`Args` 的生命周期行为有针对性测试（析构次数、空值、不可拷贝类型报错路径）。

## Phase 3：物化核心（3~5 天，**最复杂**）✅

> 已完成：`class_of<T>()` 真反射物化打通，`Class`/`Field`/`Method` 最小闭环落地，
> CMake 与 xmake 双系统 24 用例全绿。偏差见 @.agents/review.md「Phase 3」。

目标：`class_of<T>()` 打通，最小可用闭环。

- [x] meta 层：`consteval` 收集（public fields / methods；ctors/bases/enumerators 结构预留）
- [x] thunk 层：getter/setter/get_ref/invoker/release/destroy/clone 的编译期合成
- [x] `TypeDescriptor` 物化为 `constexpr` 静态表（`TypeBundle<T>`）
- [x] `class_of<T>()` / `type_of<T>()` 入口
- [x] `Class` 句柄：身份（`get_name` / `get_qualified_name`）、分类（`kind` / `is_xxx`）、尺寸（`get_size` / `get_alignment`）、成员双视图（public：`get_fields` / `get_field` / `get_methods` / `get_method`；declared：`get_declared_fields` / `get_declared_field` / `get_declared_methods` / `get_declared_method`，含 private/protected）
- [x] `Field`：`get` / `set` / `get_ref` + 偏移/尺寸/对齐/位域信息
- [x] 最小 `Method`：`invoke` + 参数/返回类型 + 签名（完整重载/虚派发留 P4）
- [x] 测试：算术/位域/引用/const/静态/mutable 字段与实例/静态方法的全量验证（`tests/phase3_test.cpp`）

**验收**：api_design.md §0 的最小示例（Point 的字段读写 + `norm` 调用）能跑通；位域、引用成员、静态字段的边界行为有测试。

## Phase 4：方法与构造（2~3 天）✅

> 已完成：`Constructor` + `Class` 构造视图 + `Method` 补全（谓词 / invoke const 校验）+
> 重载解析（模板 / `Class*`）+ `ObjectRef` const 建模；CMake 与 xmake 双系统 46 用例全绿。
> 偏差见 @.agents/review.md「Phase 4」。三级查找的「类型名（注册表）」路径随 P6 注册表落地。

- [x] `Method`：`invoke`（Args 解包 + 类型检查 + 调用 + 打包）、签名信息（参数/返回值/cv/ref/noexcept/virtual）、`is_operator`/`is_destructor`
- [x] `Constructor`：`new_instance` / `construct_at` / `destroy_at`、explicit/deleted/defaulted 识别
- [x] `Class` 构造视图：`get_constructors` + `get_declared_constructors`（fields/methods 的双视图已在 P3 完成）
- [x] 三级查找的模板重载与 `Class*` 两级（`get_method<Params...>` / `get_method(name, {&cls...})` / `get_constructor<...>` / `get_field<T>`）；类型名（注册表）一级随 P6 落地
- [x] 重载解析、虚函数派发、const 正确性运行期校验的测试（`tests/phase4_test.cpp`）

**验收**：重载、继承（含虚函数表派发）、静态成员函数、const 对象调非 const 方法报错等场景测试全绿。

## Phase 5：继承、枚举、数组（1~2 天）✅

- [x] `BaseClass` + `get_bases` / `get_superclass` / `is_assignable_from` / `is_instance` / `cast_to`（thunk 处理偏移与虚基）
  - [x] `BaseClass` + `get_bases()` + `get_superclass()` + `get_base<T>()` / `get_base(const Class&)`（提前到 P3 完成，见 review.md「Phase 3」偏差 11）
  - [x] `is_assignable_from` / `is_instance` / `cast_to`（`BaseClass` 增 `cast` thunk）
- [x] `Enumerator` + `get_enumerators` / `get_underlying_type` / `enum_from_name` / `enum_from_value` / `enum_name_of`
- [x] `Array`：`new_instance` / `get_length` / `get` / `set` / `get_ref` / `get_element_type`；数组 Class 的 rank/extent 分解
- [x] 指针/cv/成员指针类型的分解 API（`get_pointee_type` / `get_unqualified_type` / …）

**验收**：多重继承与虚基的 cast_to、枚举三向互转、多维数组 rank/extent 测试全绿。
（已完成：CMake 63/63、xmake 63/63，见 review.md「Phase 5」。）

## Phase 6：注册表与宏（1 天）✅

- [x] `speculo.registry`：全局表 + `Class::for_name` + `register_class` + `SPECULO_REGISTER` 宏
- [x] `class_of<T>()` 首次调用自动注册
- [x] `include/speculo/macros.h` 已有 `SPECULO_IMPL` / `SPECULO_IMPL_VIRTUAL`，补 `SPECULO_REGISTER` 并入此头
- [x] 线程安全验证（magic static + 注册表并发只读）

（已完成：CMake 74/74、xmake 74/74，见 review.md「Phase 6」。）

## Phase 7：收尾（1~2 天）

- [ ] proxy 集成示例（`examples/proxy_demo.cpp`：`SPECULO_IMPL` + facade 挂 `get_class`）
- [ ] 补全 @.agents/codeStyle.md（从 src/ 实际代码提炼）
- [ ] review.md 终审：API 与 api_design.md 的偏差清单、已知限制（对齐 design.md §11）
- [ ] README.md（可选）：快速上手 + 构建说明

**验收**：双构建系统全绿；示例能跑；文档与实现一致。

---

## 里程碑依赖

```
Phase 0 ──> Phase 1 (spike) ──> Phase 2 ──> Phase 3 ──> Phase 4 ──> Phase 5 ──> Phase 6 ──> Phase 7
              │
              └── 若 S1/S4 ❌：回到 api_design.md 调整设计
```

Phase 2 与 Phase 1 可部分并行（基础层不依赖反射）；Phase 3 是最大不确定性所在，时间盒若爆，优先砍 declared 私有视图降级方案（**已完成，无需降级**）。

---

## 交接备忘（2026-10-13，P6 完成后 —— 接手 Phase 7）

**当前进度**：Phase 0/1/2/3/4/5/6 ✅；**仅剩 P7（收尾）**。
双构建系统全绿（CMake 74/74、xmake 74/74）。逐阶段偏差与决策见 @.agents/review.md「Phase 2」～「Phase 6」。

**工具链硬约束**

- 编译器 **GCC >= 16**（唯一实现 P2996 `std::meta`），开关 **`-freflection -fno-rtti -fno-exceptions`**
  （RTTI 与异常全项目禁用；错误一律 `crab_cpp::Option`/`Result`，禁止 `throw`/`try`/`catch`）。
- 依赖 `crab_cpp`（模块，v0.4.0）；CMake（Ninja + `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD`）与 xmake 双系统对等，
  新增模块分区必须同时更新 `CMakeLists.txt` 的 `CXX_MODULES` 与 `xmake.lua`。
- 测试：CMake 用 `file(GLOB_RECURSE)`（非 `CONFIGURE_DEPENDS`）→ **新增测试文件后要重新 configure**；
  xmake 用 `src/*.cppm` + `tests/*.cpp` 自动收集。

**代码结构（现状）**

- `src/error.cppm`(`speculo:error`) / `src/modifier.cppm`(`:modifier`) / `src/descriptor.cppm`(`:descriptor`) /
  `src/object.cppm`(`:object`，**最大**，含全部 API + 反射物化) / `src/registry.cppm`(`:registry`，名字表 +
  `Class::for_name` + 自动注册钩子) / `src/mod.cppm`（伞模块）。
- `tests/`：`skeleton_test.cpp`（P0 导入链路）、`phase2_test.cpp`、`phase3_test.cpp`（P3 + 继承导航）、
  `phase4_test.cpp`（P4）、`phase5_test.cpp`（P5）、`phase6_test.cpp`（P6 注册表 / 名字查找 / 宏 / 并发）。
- `registry.cppm` 依赖 `:object`（**不可反向**）：`Class::for_name` 在 `:object` 声明、`:registry` 定义；
  `class_of<T>()`（在 `:object`）经 `detail::register_type`（`:object` 声明、`:registry` 定义）自动注册。
- `object.cppm` 内部顺序：`export namespace speculo`（FixedString/ObjectRef/Object/Args/Field/Method/BaseClass/
  Constructor/Enumerator/Class/Array）→ `module; #include <meta>,#include <new>` 全局片段 +
  `export namespace speculo::detail`（物化）→ `export namespace speculo`（deferred 成员 + `class_of`）。

**继续开发前必读的雷区（GCC 16.2 实测）**

1. **不要再拆模块分区**。`:type`/`:field`/`:method`/`:ctor` 会因「实体定义所在分区」依赖规则 + ObjectRef/Object/
   Class 的强连通分量形成 `dependency cycle`。新类型（Constructor/Array/Registry）应并入 `:object` 或更低分区，
   不要新建与之互相引用的分区。（`Constructor` 已按此并入 `:object`。）
2. **物化只能走单个 `TypeBundle<T>`（键在类型 `T`，不是 `meta::info`）**，所有内部指针都指向自身成员：
   - 互相引用的两个 `constexpr` 变量模板无法表达（函数前向引用报 *before definition complete*；
     `extern const` + `inline constexpr` 链接失败）；
   - `meta::info` NTTP 会让同类型不同拼写触发汇编期 *symbol already defined* → 一律键在**类型**上；
   - 「模板 lambda + 折叠 + splice」嵌套会 ICE → 用 pack 展开的 `std::array` 初值 + 普通循环。
   - **⚠️ 新雷区（P4）**：bundle 初始化器里引用其它 bundle 必须用**依赖拼写**（`bundle<FT>`/`bundle<RT>`/
     `bundle<参数类型>`）；一旦以**具体拼写**引用 `bundle<void>`（例如析构的返回类型），GCC 会把 `bundle<void>`
     错误地初始化成默认描述符（`kind=Class`、名字空、`cls.m_desc=null`），`class_of<void>()` 随即崩溃。
     故析构 `Method` 的 `return_type` 置 nullptr，`get_return_type()` 回退到 `class_of<void>()`。
3. **成员函数调用统一用 `meta::extract<精确成员函数指针>(M)` + `(obj->*fp)(...)`**，不要用 `obj.[:M:]`：
   private/protected 的拼接**调用**被 GCC 拒绝。精确类型由 `member_function_pointer<C, type_of(M)>`
   （24 种 cv/ref/noexcept 组合）/ `static_function_pointer` 重建。私有**数据成员**的拼接读写正常。
   经成员函数指针调用**天然虚派发**（P4 已验证）。
4. **`crab_cpp::Result::unwrap()` 返回左值引用**（右值也不 move）。move-only 的 `Object` 必须
   `std::move(res.unwrap())`（`api_design §0` 示例已按此修）。`unwrap_err()` 对 Ok 值会 `panic`。
   （**更新（crab 0.4.0）**：`unwrap()` 现按值类别返回——lvalue 拷贝、rvalue 直接 move，无需 `std::move`；
   `Option::take()` 系列已移除。见 @.agents/crab_cpp_api_notes.md。）
5. 构建：CMake 测试源用 `file(GLOB_RECURSE)`（**非 CONFIGURE_DEPENDS**）→ **新增测试文件后要重新 configure**；
   xmake 用 `src/*.cppm` + `tests/*.cpp`。xmake 的 `speculo` 是 `moduleonly` 目标、不单独构建，
   模块经 `speculo_test` 编译（与 crab_cpp 官方样板一致）。
6. **`std::string_view` 在 libstdc++ 16.2 不是 structural type**（私有 `_M_len`），不能直接做 NTTP；
   编译期名字用 `speculo::FixedString`（`get_method<"scale", int>()` 写法不变）。
7. **运行期 `Class*` 形参列表用 `std::initializer_list<const Class*>`**（`{&cls}` 无法绑定 `std::span`）。
8. **类型身份保真（P5 定案）**：`class_of<T>` 键在 **T 本身**（cv 与引用都不归一），返回 `bundle<T>.cls`；
   `Object`/`ObjectRef` 仍键在 `remove_cvref_t<T>`（对象句柄永不携带引用/cv）。描述层新增
   `decayed`/`unqualified`/`pointee`/`element`/`pointee_class`/`referenced`/`underlying`/`dynamic_array`
   等指针，全部在 bundle 初始化器里用**依赖拼写**引用其它 bundle。
9. **cv/引用类型的 bundle 不物化成员**：`bundle<const T>`/`bundle<T&>` 只做身份与分解，不生成字段/方法/
   构造/基类，也不生成 release/clone/`new_array`/`enum_make` thunk（`new const T*` 等无法转 `void*`，
   `static_cast<const T*>` 到基类被拒）。门控谓词：`is_unqualified_class` / `is_unqualified_object`。
10. **形参 `Class*` 反查描述符需要完整 `Class`**：`Method::invoke`/`Constructor::validate`/
    `Class::matches_arguments` 都在 `Class` 定义完成前编译，不能写 `param->descriptor()`。
    故描述层额外存 `parameter_decays`（退化 `TypeDescriptor*` 数组）供运行期匹配；`parameter_types`
    （精确 `Class*`）供用户与按类型查找。
11. **基类 cast thunk 仅对 public 基生成**：命名空间作用域的 `static_cast<Base*>` 无 private/protected
    基类访问权；不可访问基的 `cast = nullptr`，`cast_to` 找不到路径（返回 `TypeMismatch`）。

**当前 API 语义（实现为准，api_design/design 已同步）**

- 成员双视图：`get_fields`/`get_field`/`get_methods`/`get_method`/`get_constructors`/`get_constructor`（**本类 public**）、
  `get_declared_*`（本类全部，含 private/protected）。
- 继承：`get_fields` **不含**继承（有意偏离 Java 的自动聚合）；`get_bases()` / `get_superclass()` /
  `get_base<T>()` / `get_base(const Class&)` 显式上溯；`get_base` 只查**直接**基类。
- `Field::set` 仅精确类型；const 字段或 const 对象 → `ReadOnly`；位域 `get_ref` → `NotSupported`；
  引用成员读写穿引用；静态字段忽略 `obj`。
- `Method::invoke`：精确指针类型校验；const 对象调非 const 方法 → `ReadOnly`；虚函数走动态派发；
  析构 `Method` 不可 `invoke`（`NotSupported`）。
- `ObjectRef` 带 const 标志：非 const `as<T>()` 对 const 引用返 `None`，const 句柄的 `as` 返 `const T&`。
- 构造视图含**隐式**构造（默认/拷贝/移动）；私有/删除/抽象构造不可 `new_instance`/`construct_at`
  （`NotSupported`），但 declared 视图可见且带访问修饰符。
- 三级查找：模板（`get_method<Params...>` / `get_method<"name", Params...>` / `get_constructor<Params...>` /
  `get_field<T>`）与 `Class*`（`get_method(name, {&cls...})`）已实现；**类型名（注册表）待 P6**。
- **类型身份（P5）**：`class_of<T>()` 反映**精确类型**（cv 与引用都不归一）；`is_reference()`/`get_reference_kind()`/
  `get_referenced_type()`、`is_const_qualified()`/`get_unqualified_type()`、`get_pointee_type()`/
  `get_element_type()`/`get_array_extent()`/`get_array_rank()`/`get_pointee_class_type()` 全套分解可用。
  声明侧（字段/形参/返回类型）报**精确类型**；匹配/装箱用**退化类型**（`decayed`）。
- **继承导航（P5）**：`cast_to(obj, base)` 只做**向上**转换（含 identity），多继承/虚基偏移由 `BaseClass::cast`
  thunk（`static_cast`）处理，仅 public 路径；歧义 → `Ambiguous`，不可达 → `TypeMismatch`，null → `NullInstance`，
  const 传播。`is_assignable_from(other)` / `is_instance(obj)` 为**静态类型**判定（无 RTTI）。
- **枚举（P5）**：`get_enumerators()` / `get_underlying_type()` / `enum_from_name` / `enum_from_value` /
  `enum_name_of`；`Enumerator::value()` 按底层类型装箱（review 4.8 之前是按符号性装 `int64_t`/`uint64_t`），`to_int64()`/`to_uint64()` 双视图。
- **数组（P5）**：`Array::new_instance`（动态数组，长度存于分配头部）/`get_length`/`get`/`set`/`get_ref`/
  `get_element_type`；静态 `T[N]` 走 extent，多维逐级分解（`get`/`set` 只作用于数组自身层级的元素，rank>1 需先 `get_ref` 降维）。

---

## Phase 6 实现要点（已完成，留档）

**目标**：落地 `speculo:registry`，补上「按类型名查找」的第三级，提供显式注册与 `SPECULO_REGISTER` 宏。

**任务清单（全部完成）**

1. `src/registry.cppm`（`speculo:registry`）：全局表 `qualified_name → const Class*`；`register_class(const Class&)`（幂等）；
   `Class::for_name(std::string_view) -> Option<const Class&>`。
2. `class_of<T>()` 首次调用**自动注册**（函数内 magic static，每类型一次；`bundle<T>` / 描述表保持纯只读）。
3. `include/speculo/macros.h` 补 `SPECULO_REGISTER(Type)`（命名空间作用域；宏不进模块）。
4. 三级查找第三级：`get_method(name, {"int"})` / `get_declared_method(...)` / `get_constructor({...})` /
   `get_declared_constructor(...)` / `get_base("ns::B")`。
5. 线程安全：magic static + 注册表并发只读（`std::shared_mutex`）。
6. `tests/phase6_test.cpp`（11 用例，含并发注册/查找）。

**结论（与接手设想的两处出入）**

- 设计点 A：采用**独立 `:registry` 分区**（`Class::for_name` 在 `:object` 声明、`:registry` 定义；
  自动注册经 `detail::register_type` 回调）。跨分区定义成员函数/模板经 GCC 16.2 实测可行，无 `dependency cycle`。
- 设计点 C：`meta::has_identifier` **不是**可用的规范名判据（对 `int` 返 false、对匿名命名空间类型返 true）。
  改用 `!has_internal_linkage && 祖先链无函数`；`Class::has_canonical_name()` 语义随之修正。
- 名字版重载形参为 `std::initializer_list<std::string_view>`（非 `std::span`）；空 `{}` 曾与 `Class*` 版歧义（现已将名字版做成带默认模板形参的模板，空 `{}` 取 `Class*` 版即零参）。

细节见 @.agents/review.md「Phase 6」。

---

**已发现但未处理的小项**

- `speculo::version_string()` 仍是 P0 占位，可删。
- `Modifier::INLINE`/`CONSTEXPR`/`CONSTEVAL` 无 `<meta>` 谓词（GCC 16.2 无 `is_inline`/`is_constexpr`/`is_consteval`），
  任何成员都不设置这三位；review 4.4 起公共 API 不再提供对应的 `is_xxx()`，位值保留。
- 私有/保护构造函数无法 `new_instance`/`construct_at`（命名空间作用域 `new` 无类内访问权）。
- **P5 新增**：`Array::new_instance` 不支持「元素本身是数组」的类型（如 `int[3][]`）；`cast_to` 只走 public 基；
  `Method::invoke` 对 `T&&` 形参仍只能按右值转发（`ObjectRef` 无值类别）。
- **P6 新增**：`get_qualified_name()` 对带别名的显示类型不友好（如 `std::string_view {aka std::basic_string_view<char>}`），
  按名字查这类类型需用完整 display 串；名字版查询的空 `{}` 曾在两个 `initializer_list` 重载间歧义
  （review 4.2 已解决：`get_method("f", {})` 即零参重载）。
- **P7（crab_cpp v0.3.0 → v0.4.0 升级，已完成）**：v0.3.0 记录的三处偏差中 `unwrap*` 值类别与
  `Option::take()` 已由 0.4.0 解决，variant + move-only 一项仍存（`reflect_impl_tag` 注解保留）。
  适配与 pin 更新见 **@.agents/crab_cpp_api_notes.md**。

---

## Phase 7 接手要点（收尾，进行中）

**目标**：示例、文档、终审。

- [x] 基础示例 `examples/01_basics` / `02_construct_and_call` / `03_inheritance_enum_array` / `04_registry_and_macros`（可运行）
- [x] `examples/` 接入构建（CMake `option(SPECULO_BUILD_EXAMPLES)` + ctest；xmake `option("enable-example")` + `add_tests`，双系统对等）
- [x] 修复为使 `SPECULO_IMPL` 可用而暴露的 4 个潜在缺陷 + 引入注解排除机制（见 review.md「Phase 7」）
- [ ] `examples/proxy_demo.cpp`：`SPECULO_IMPL` / `SPECULO_IMPL_VIRTUAL` + facade 挂 `get_class` 的 Microsoft proxy 集成示例
- [ ] 补全 @.agents/codeStyle.md（从 `src/` 实际代码提炼）
- [ ] review.md 终审：API 与 api_design.md 的偏差清单、已知限制（对齐 design.md §11）
- [ ] README.md（可选）：快速上手 + 构建说明

**验收**：双构建系统全绿；示例能跑；文档与实现一致。
