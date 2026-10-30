# speculo 开发计划（v2）

> 原则：**风险前置**。GCC 16 的 P2996 是部分实现，所有不确定性必须在正式开发前用 spike 验证完毕。
> 每个阶段结束时更新 @.agents/review.md（记录验证结果、偏差、设计调整）。
> 双构建系统（CMake + xmake）必须始终对等，不许出现「CMake 能编 xmake 不能编」的债务。
>
> **v1（Phase 0–7）已全部落地**，完成记录与逐阶段偏差见 @.agents/review.md；本文件只保留
> v2 计划。设计与 API 分别见 @.agents/design.md「第二部分」与 @.agents/api_design.md。

## 0. 起点与硬约束

**已完成**：`speculo` 模块（`error` / `modifier` / `descriptor` / `object` / `function` / `registry`）
提供完整 v1 API——`Class` / `Field` / `Method` / `Constructor` / `Function` / `Annotation` /
`Enumerator` / `BaseClass` / `Array` / `Modifier`、双视图成员访问、三级查找、继承与 `cast_to`、
注册表与 `SPECULO_IMPL` 家族。CMake 与 xmake 双系统全绿。

**测试现状**：268 个用例（CMake 全绿，`ctest` 5/5；xmake 同样 5/5）。v1 的测试已按
@.agents/testing.md §2–§5 对齐，**7 张组合矩阵**覆盖 `Method::invoke`（5 × 5）、`Field::set`
（5 × 5）、`Class::get_method`（5 × 4）、`Constructor::new_instance`（5 × 5）、`Array` 操作
（5 × 4 + Option 列）、`cast_to`（5 × 2）、枚举三向互转（4 × 6），`Function` 侧的 6 张沿用；
Phase 9 又加了 `Callable::invoke` 的接收者 × 实参表。错误优先级（api_design §0.1）由矩阵中
「同时两个错」的格子钉住。前 3 张的期望值由 `tools/gen_matrix.py` 枚举生成，其余按行内
数据表。9 个句柄各自的「空句柄 → `panic`」死亡用例也已补齐（共 15 个 `EXPECT_DEATH`，
约占 9 s 跑时，属预期开销）。

**工具链（硬约束，v2 不变）**

- 编译器 **GCC >= 16**，开关 **`-freflection -fno-rtti -fno-exceptions`**。
- 全项目禁用异常与 RTTI：错误一律 `crab_cpp::Option` / `Result`；源码禁止 `throw` / `try` /
  `catch` / `typeid` / `dynamic_cast`；`libspeculo.a` 中不得出现 `__cxa` / `type_info` 符号。
- 依赖 `crab_cpp`（模块，v0.4.0）。CMake（Ninja + `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD`）与
  xmake 双系统对等；新增模块分区必须同时更新 `CMakeLists.txt` 的 `CXX_MODULES` 与 `xmake.lua`。
- 测试：CMake 用 `file(GLOB_RECURSE)`（非 `CONFIGURE_DEPENDS`）→ 新增测试文件后要重新 configure。

**继续开发前必读的雷区（GCC 16.2 实测，v2 同样适用）**

1. **不要再拆与 `:object` 互相引用的模块分区**。新类型并入 `:object` 或更低分区。
   唯一的例外是 `:compile`（§Phase 11）：它只依赖 `<meta>` + `std`，不 import `:object`，
   不形成强连通分量。
2. **物化只能走单个 `TypeBundle<T>`（键在类型 `T`，不是 `meta::info`）**；bundle 初始化器里
   引用其它 bundle 必须用**依赖拼写**（`bundle<FT>`）。用具体拼写引用 `bundle<void>` 会把它
   错误初始化成默认描述符。
3. **成员函数调用统一用 `meta::extract<精确成员函数指针>` + `(obj->*fp)(...)`**，不要用
   `obj.[:M:]`（private/protected 的拼接调用被拒）。私有**数据成员**的拼接读写正常。
4. `crab_cpp::Result::unwrap()` 按值类别返回（crab 0.4.0）：lvalue 拷贝、rvalue move。
5. **`std::string_view` 在 libstdc++ 16.2 不是 structural type**，NTTP 名字用 `speculo::FixedString`。
6. 运行期 `Class*` 形参列表用 `std::initializer_list<const Class*>`（`{&cls}` 无法绑 `std::span`）。
7. **类型身份保真**：`class_of<T>` 键在 `T` 本身（cv 与引用不归一），`Object`/`ObjectRef`
   键在 `remove_cvref_t<T>`。新增查询一律沿用这条。
8. **cv/引用类型的 bundle 不物化成员**（只做身份与分解）。门控谓词：`is_unqualified_class` /
   `is_unqualified_object`。
9. **形参 `Class*` 反查描述符需要完整 `Class`**，故描述层额外存 `parameter_decays`
   （退化 `TypeDescriptor*`）供运行期匹配。v2 的 `TemplateArgument` 同理会需要一份「退化视图」。
10. **基类 cast thunk 仅对 public 基生成**（命名空间作用域的 `static_cast` 无访问权）。
11. 模板 lambda + 折叠 + splice 的嵌套会 ICE → 用 pack 展开的 `std::array` 初值 + 普通循环。

---

## Phase 8：v2 风险前置 spike（1~2 天，**关键路径**）—— ✅ 已完成（2026-10-30）

目标：把 v2 依赖的 P2996 能力全部变成确定结论。每项写最小可编译程序放 `spikes/`，
结论记入 `review.md`「Phase 8」。任一项 ❌ 都必须先改 api_design 再进入 Phase 9。

> 全部 10 项已验证，探针 `spikes/s8_1..s8_10*.cpp` 共 12 个文件，结论与 12 条雷区见
> `review.md` §8。两项 ⚠️（S8.3 虚析构推导、S8.4 MFP 不可分解）的偏差已回写
> `api_design.md` §2.2 / §5.2 / §5.3；其余 ✅ 项的关键事实也一并写进了对应章节。

- [x] **S8.1 模板**：`has_template_arguments` / `template_arguments_of` 对类型实参、非类型实参
  （`std::array<int,3>` 的 `3`）、模板模板参数、参数包（`std::tuple<int,char>` 的包）分别返回什么；
  `template_of` 返回的主模板能否 `display_string_of` 出 `std::vector`；`substitute` 能否从
  primary + args 回到类型世界（若可以，`get_primary_template` 的实现方式随之定）。
- [x] **S8.2 布局谓词**：逐一确认 GCC 16.2 `<meta>` 是否有 `is_trivially_copyable` /
  `is_trivially_default_constructible` / `is_trivially_destructible` / `is_standard_layout` /
  `is_layout_compatible` / `is_pointer_interconvertible_base_of` / `has_unique_object_representations` /
  `is_convertible` / `is_nothrow_convertible`。缺失项按 api_design §5 的降级路径处理。
- [x] **S8.3 虚析构**：是否有 `has_virtual_destructor`；无则验证 api_design §5.3 的三条推导
  （用户声明虚析构 / `is_polymorphic` 且无用户析构 / 其余为假）在 8~10 个类上是否与编译器一致。
- [x] **S8.4 函数类型分解**：`return_type_of` / `parameters_of` / `is_variadic` 对 `^^void(int, ...)`
  是否可用；对**成员函数指针**类型能否拿到 `C`、`M`、以及「去掉指针后的函数类型」。
- [x] **S8.5 嵌套类型**：`members_of` 里 type 成员如何区分类/联合/枚举/别名；`parent_of`
  （或等价能力）能否给出外层类；嵌套在类模板里的类型如何处理。
- [x] **S8.6 聚合构造**：能否在 `consteval` 里对 `T{args...}` 做 splat 构造（`std::meta::extract`
  + 参数包展开），以及 public 基类在前这一顺序能否用 `bases_of` 推出。
- [x] **S8.7 值类别**：对成员函数指针能否区分 `&` / `&&` 限定（`is_rvalue_qualified` 类谓词）；
  `std::move` 转发到 `(obj->*fp)` 是否保留右值语义（决定 `invoke_rvalue` 的实现方式）。
- [x] **S8.8 存储期**：是否存在 `is_thread_local` 类谓词；无则 `Modifier::THREAD_LOCAL` 位保留但不置位
  （与 `INLINE` / `CONSTEXPR` 同一处理方式）。
- [x] **S8.9 编译期遍历**：`template for` / splice 在**用户侧 TU**（非模块）展开是否正常；
  `for_each_field` 对大结构体（>100 字段）的编译期内存/时间是否可接受。
- [x] **S8.10 基类偏移**：`cast` thunk 的地址差能否在常量传播下化为立即数；
  虚基情况下 `offset_fn` 是否必须走运行期。

**验收**：`review.md` 中 S8.1–S8.10 每项有 ✅ / ⚠️（写明偏差与应对）/ ❌（写明降级方案）；
`spikes/s8_*.cpp` 全部可独立编译运行。


## Phase 9：类型系统建模（4~5 天）—— ✅ 已完成（2026-10-30）

目标：把「类型是怎么构造出来的」补进描述层。对应 api_design §1–§6。

> 落地记录与 6 条实现偏差见 `review.md`「Phase 9」。三处与下文清单不同：
> 4 个双类型谓词改为**自由函数模板**（运行期拿不到第二个类型），`Function` 只给
> `is_template_specialization` + `get_template_arguments`（没有主模板），`BaseClass::get_offset`
> 的偏移是物化期常量（`offset_of(基类)`，无需 `void*` thunk）。

- [x] `TypeKind::Template` + 主模板的声明级描述（以 `meta::info` 为键的 `TemplateBundle`）；
  `Class::is_template_specialization` / `get_primary_template` / `get_template_arguments` /
  `get_template_argument_type`
- [x] `TemplateArgument`（`Kind::{Type, Value, Template, Pack}` + 四个 `as_*` + `get_spelling`）；
  NTTP 值按参数类型装箱（`constant_of` + `extract`）
- [x] `Function` 同步获得模板查询（与 `Class` 共用 `TemplateArgument`；无 `get_primary_template`）
- [x] 函数类型分解：`Class::get_return_type` / `get_parameter_types` / `get_parameter_count` /
  `is_variadic` / `is_noexcept`（仅 `kind() == Function`）
- [x] `Callable` + `CallableDesc`：函数指针 / 成员函数指针的 `invoke` / `invoke_ref`；
  thunk 随函数指针类型的 bundle 生成
- [x] 聚合构造：`Class::new_aggregate` / `get_aggregate_initializer` / `Object::make_aggregate`；
  顺序 = public 基类（声明序）+ 本类非静态数据成员（声明序）
- [x] 嵌套类型：`Class::get_declared_classes` / `get_enclosing_class`（`nested_types` 表）
- [x] 布局与能力谓词：§5.2 全部 11 个（6 个单类型位 + `is_bit_castable_to` + 4 个双类型
  自由函数模板），单类型谓词存为 `TypeDescriptor` 能力位
- [x] 基类偏移：`BaseClass::get_offset`（物化期常量；虚基 / 不可访问基 → `None`）
- [x] 测试：`tests/template_test.cpp` / `callable_test.cpp` / `aggregate_test.cpp` /
  `nested_type_test.cpp` / `layout_traits_test.cpp` / `base_offset_test.cpp`

**验收**：`class_of<std::vector<int>>()` 能取到 `int`；`std::array<int,3>` 能取到 `3`；
函数指针字段可 `invoke`；POD 配置结构可 `new_aggregate`；`Outer::Inner` 可从 `Outer` 取到；
布局谓词对 10 个代表性类型与编译器内建 trait 交叉验证一致。

## Phase 10：操作语义（3~4 天）

目标：把 C++ 的对象语义（值类别、移动、默认实参、存储期）落进 API。对应 api_design §7–§9。

- [ ] `ValueCategory` + `ObjectRef::value_category` / `as_rvalue` / `as_lvalue`
  （塞进既有 flags，**16 字节布局不变**，加静态断言）
- [ ] `Method::invoke_rvalue` / `Function::invoke_rvalue` / `is_rvalue_invokable`；
  `&&` 限定成员函数从此可用
- [ ] 移动 thunk：`MoveCtorFn` / `MoveAssignFn` / `SwapFn`；`Class::assign` / `swap` / `destroy`；
  `Object::move_from`
- [ ] 默认实参：`default_argument<T>` 注解 + `get_parameter_default` /
  `get_required_parameter_count` / `invoke_with_defaults`（只补尾部）
- [ ] 存储期与剩余修饰：`is_thread_local` / `get_static_address`；
  `Modifier::THREAD_LOCAL` / `NO_UNIQUE_ADDRESS` / `CONVERSION` / `VARIADIC`；
  `Method::is_conversion_operator` / `get_conversion_type` / `is_variadic`
- [ ] 测试：`tests/value_category_test.cpp` / `move_test.cpp` / `default_argument_test.cpp` /
  `storage_test.cpp`；`ObjectRef` 布局静态断言

**验收**：`std::unique_ptr` 字段可在右值视图下 move；`&&` 限定成员函数可调用；
带默认实参的 `Function` 能按最少实参调用；`thread_local` 静态成员可识别。

## Phase 11：编译期半层（3~5 天）

目标：`speculo:compile` 分区，把「编译期遍历」这半条路开放给用户。对应 api_design §10。

- [ ] 抽出纯 meta 收集器：`detail::collect_fields<T>()` / `collect_methods<T>()`
  （**运行期物化与编译期遍历共用**，避免规则漂移）
- [ ] `src/compile.cppm`（`export module speculo:compile;`）：只 `import std` + `<meta>`
- [ ] `member_count` / `field_count` / `for_each_field` / `for_each_member` / `field_info` /
  `field_table<T>`
- [ ] `FixedString` 下沉到不含反射的小分区，供 `:compile` 与 `:object` 共用
- [ ] 构建系统：`CMakeLists.txt` 的 `CXX_MODULES` 与 `xmake.lua` 同时加入 `:compile`（双系统对等）
- [ ] 示例：`examples/05_compile_time.cpp`（一行生成 `operator==`、`to_json`、SoA 偏移表）
- [ ] 测试：`tests/compile_time_test.cpp`（含「运行期表与编译期遍历结果一致」的交叉校验）

**验收**：`for_each_field` 在用户 TU 可用；`field_table<T>` 为 `constexpr` 且可作偏移索引；
运行期 `get_fields()` 与编译期 `for_each_field` 的字段集合/顺序完全一致（同一收集器）。

## Phase 12：工程化（4~5 天）

目标：让它能进真实项目——不分配、可分配、跨库认得出、可诊断。对应 api_design §11–§14、§16。

- [ ] `InvokeIntoFn` + `Method::invoke_into` / `Function::invoke_into`；
  `invoke` 改为 `allocate + invoke_into + dtor` 的薄包装
- [ ] `Allocator` + `Object::set_default_allocator` / `get_default_allocator`；
  `Class::new_instance_with` / `new_aggregate_with`；`ArenaAllocator` 参考实现
- [ ] 类型身份：`Class::operator==` 三分支（指针 → 规范名 → false）+ `get_type_id`
  （FNV-1a + 冲突链）；`Class` 的 `std::hash` 特化指向 `get_type_id`
- [ ] `SPECULO_DEEP(Type)` + `detail::deep_materialize<T>()`（只影响该特化，不传播）
- [ ] 诊断：`Class::describe` / `to_string(ObjectRef)` + `std::formatter<Class/Field/Object>`
- [ ] 编译期护栏：成员数 / 模板实参数超阈值 → `static_assert`，消息带类型名与处置建议
- [ ] 测试：`tests/invoke_into_test.cpp` / `allocator_test.cpp` / `type_identity_test.cpp` 扩展 /
  `deep_std_test.cpp` / `diagnostics_test.cpp`

**验收**：proxy 风格的热路径调用零分配（以计数分配器验证）；同一类型在「两份静态副本」下
`operator==` 为真且 `get_type_id()` 相同；`SPECULO_DEEP(std::vector<int>)` 后能列出其方法；
`describe()` 输出稳定（快照测试）。

## Phase 13：关系图与收尾（3~4 天）

目标：继承/重写的关系图，加上 v1 遗留小项与文档终审。对应 api_design §15。

- [ ] 注册表 v2：反向边索引（base → derived），插入时维护
- [ ] `Class::get_derived_classes` / `get_all_derived_classes` / `is_derived_from` /
  `get_conversion_path`（0 条 → `TypeMismatch`，>1 条 → `Ambiguous`）
- [ ] `Method::get_overridden_method` / `get_overrider_methods`；`get_vtable_index`
  （S8.7 若不可得则恒 `None`，文档写明）
- [ ] tests：`tests/type_graph_test.cpp`（含「未注册派生类不可见」的负面用例）
- [ ] **v1 遗留小项**（不阻塞 v2，但顺手清）：
  - 删除 `speculo::version_string()` 占位
  - `get_qualified_name()` 对 `{aka ...}` 别名的显示问题（`normalized_name` 已有，检查 `for_name` 两条键是否覆盖全部情况）
  - 私有/保护构造函数仍不可 `new_instance`（命名空间作用域 `new` 无类内访问权，已知限制，文档写死）
  - `Array::new_instance` 不支持「元素本身是数组」（`int[3][]`）
- [ ] 文档终审：design.md / api_design.md / phases.md 与实现逐条对齐；`review.md` 补「Phase 8–13」
- [ ] `examples/proxy_demo.cpp`（Microsoft proxy 集成）+ `SECONDARY` 示例更新

**验收**：双构建系统全绿；`get_derived_classes` 在「注册 3 层继承」的用例上结果正确；
文档中不存在与实现不符的段落（逐节核对）。

---

## 里程碑依赖

```
Phase 8 (spike) ──> Phase 9 (类型建模) ──┬──> Phase 10 (操作语义) ──┐
                                        └──> Phase 11 (编译期半层) ─┴──> Phase 12 (工程化) ──> Phase 13 (关系图/收尾)
```

- Phase 9 是 v2 的地基，其余全部依赖它新增的描述层字段。
- Phase 10 与 Phase 11 可并行（前者动运行期对象模型，后者只读 meta 收集器）。
- Phase 12 的 `invoke_into` 依赖 Phase 10 的移动 thunk（结果直接构造进 dst）。

## 验收总则

- 每个 Phase 结束时：CMake 与 xmake 双系统全绿、`ctest` 无失败、`review.md` 记录偏差。
- 每个新增公开 API 必须交一张**参数值类矩阵表**（测试文件顶部，格式见 @.agents/testing.md §6）：
  - 每个参数（含接收者）的 V0–V5 每类至少一格（§2）；
  - 多参数按 §3 组合：格数 ≤ 64 全笛卡尔，否则单因素全覆盖 + pairwise + 两条对角线；
  - `Option` 的 `Some` 与**每条 `None` 路径**、`Result` 的 `Ok` 与**每个承诺的 `ErrorKind`**、
    `Status` 的成功与每个 `Err` 都要有断言（§4）；
  - 错误优先级先写进 @.agents/api_design.md §0.1 再写测试；
  - v2 专属手段（trait 交叉验证、两路一致性、布局静态断言、分配计数、golden 快照）见 §9。
- 禁止出现「文档写了、代码没有」或「代码有了、文档没写」；本文件与
  @.agents/api_design.md 是同一份契约的两面。

## 不在计划内

v2 明确不做的 7 项（成员函数模板按名实例化、命名空间枚举、协变返回覆盖、向下转换、
跨 DSO thunk 互换、`Object` 的 SSO、注解注入成员）见 @.agents/api_design.md §17。
