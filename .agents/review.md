# speculo 代码审核（2026-10-15）

静态阅读 `src/`、`include/speculo/macros.h`、`tests/`、`examples/`，并对照 `.agents/design.md`、`.agents/api_design.md`、`.agents/phases.md`。没有改代码，也没有为下列条目逐条补编译探针；现有测试按阶段记录是绿的，但下面多数路径没有用例。

> **处理进度**：✅ 已处理（章节内有「已处理」说明和对应测试），🟡 部分处理。截至目前已完成 2.1、2.2、2.3、3.1–3.9、4.1–4.10、5.1、5.2，5.3 部分处理（invoker 按需实例化不做）；5.4 起尚未处理。正文保留审核当时的描述，不再随修复改写。

严重程度：

| 级别 | 含义 |
|---|---|
| P0 | 静默做错，或让常见类型的 `class_of<T>()` 直接编译失败 |
| P1 | 行为与 C++ / 本库自己的错误模型不符，调用方会拿到错误结果或错误种类 |
| P2 | 设计与易用性：能用，但和文档或日常写法对不上 |
| P3 | 性能、工程、文档债 |

---

## 1. 总览

| 级别 | 问题 | 位置 |
|---|---|---|
| P0 | ✅ `cast_to` / `Field` / `Method::invoke` 不校验接收者类型，错对象会返回成功 | `object.cppm` `cast_to`、`Field::get/set`、`Method::invoke` |
| P0 | ✅ 运行期重载解析把 `const T&` 与 `T&&` 看成同一个类型，可能从左值移走，或让已删除构造挡住可用构造 | `matches_arguments`、`load_argument`、`Class::new_instance` |
| P0 | ✅ 按值接收 move-only、或返回 move-only 的引用时，物化期硬错误，整个类无法反射 | `make_invoker` |
| P1 | ✅ 虚继承菱形到同一虚基被报成 `Ambiguous` | `find_upcast` |
| P1 | ✅ `mutable` 在 const 对象上既不能 `set` 也不能 `get_ref` 写 | `Field::set`、`Field::get_ref` |
| P1 | ✅ cv / ref-qualifier 不参与重载匹配，同形参列表取声明顺序的第一个 | `find_method` |
| P1 | ✅ 不可访问的基类被报成「不是基类」 | `cast_to` |
| P1 | ✅ 引用成员 `get_ref` 一律失败；指向 move-only 时该成员无法访问 | `make_field_ref` |
| P1 | `const int[N]` 这类元素带 cv 的数组，`Array::get` / `as` 都读不出来 | `Array::get`、`Array::get_ref`、`ObjectRef::as` |
| P1 | 非常量 `ObjectRef` 上看 const 对象时，`as<const T>()` 也是 `None` | `ObjectRef::as` |
| P1 | 对齐超过 `operator new` 默认对齐的元素，动态数组元素地址未对齐 | `new_array_thunk` |
| P1 | 动态数组长度相乘可回绕，分配块小于随后按 `count` 构造的范围 | `new_array_thunk` |
| P2 | ✅ 实参只能是左值；字面量、临时 `Object`、字符串字面量对不上形参 | `Args`、`ObjectRef` |
| P2 | ✅ 按名字 `get_method(name)` 在重载存在时静默返回第一个，空 `{}` 又有二义性（前半随 3.3 已解决；空 `{}` 随 4.2 已解决） | `Class::get_method` |
| P2 | ✅ 动态数组描述符是半初始化的：空名字、`has_canonical_name == true`、不能 `clone` | `TypeBundle` 动态数组段 |
| P2 | ✅ `INTERFACE` / `INLINE` / 函数级 `FINAL` / `CONSTEXPR` / `CONSTEVAL` 从未置位 | `make_descriptor`、`make_method_desc` |
| P2 | `detail` 整体导出，空句柄解引用，错误字符串没有类型名/成员名 | 模块边界、`Error` |
| P3 | `invoke` / `new_instance` 热路径堆分配；注册表在静态初始化阶段不可靠 | `Method::invoke`、`SPECULO_REGISTER` |

下面每条都是「现状 → 后果 → 方向」。方向只说明该改哪一层，不是补丁。

---

## 2. P0

### ✅ 2.1 接收者不校验，`cast_to` 会成功返回错误地址

> **已处理。** `cast_to` 要求 `obj.type()` 就是发起转换的类；`Field::get/set/get_ref` 与 `Method::invoke` 要求接收者的描述符就是声明类（静态成员除外），否则 `TypeMismatch`。用基类 `Method` 调派生对象要先 `cast_to`。测试：`ReceiverTest.ObjectOfAnotherTypeIsRejected`、`InheritanceTest.CastToRequiresTheReceiverToBeThisClass`。

`Class::cast_to` 用 `*this` 的布局去调整 `obj.raw_data()`，不看 `obj.type()`。`Field::get` / `set` / `get_ref` 和 `Method::invoke` 同样把 `void*` 直接 `static_cast` 成声明类。

`tests/phase5_test.cpp` 的向下转换用例之所以得到 `TypeMismatch`，是因为从 `Root` 向上找不到 `Multi`，不是因为发现了对象的静态类型是 `Multi`。反向的错误用法会走成功路径：对一块并不是 `Multi` 的内存调用 `class_of<Multi>().cast_to(ref, class_of<Root>())`，只要 `Multi` 的基类图里有 `Root`，就返回 `Ok` 的 `ObjectRef`，类型写成 `Root`，地址按 `Multi` 的偏移去算。随后 `as<Root>()` 也会成功，读到的是邻接内存。

`Field` / `Method` 更直接：声明类是 `A` 时，传入 `B` 的对象就是按 `A*` 用。多继承且基类不在偏移 0 时，即使用基类 `Method` 去调派生对象（想靠虚函数派发），指针也没有先调整到基类子对象。

建议：这三个入口先要求 `obj.type()` 与声明类是同一个描述符（`cast_to` 则以 `obj.type()` 为起点，并且 `obj.type()` 必须就是发起转换的那个类）。不满足返回 `TypeMismatch`。库没有 RTTI，做不到对「静态类型是基类、动态类型是派生类」的对象自动调整，这个限制应写进 API，而不是静默假设指针已经是声明类。

### ✅ 2.2 拷贝构造和移动构造在运行期是同一个键

> **已处理。** 新增 `detail::accepts_argument`：匹配键带引用类别和 const（const 实参不绑定非 const 引用）。`T&&` 形参只在调用方显式取到具体 `Constructor` / `Method` 时接受，`Class::new_instance` 的自动选择不会选它；已删除候选被跳过，多个可用候选返回 `Ambiguous`，只有已删除候选匹配时返回 `NotSupported`。未做：`T&` 与 `const T&` 同时可用时的 C++ 式优先级（现为 `Ambiguous`）。测试：`ConstructorTest.*`（phase4）。

声明侧的形参 `Class*` 保留了引用：`const T&` 和 `T&&` 是两个 `Class`，`get_constructor<const T&>()` / `get_constructor<T&&>()` 能分开。运行期匹配用的是 `parameter_decays`（`remove_cvref`），两者都变成 `T`。

`Class::new_instance` 按 `get_constructors()` 的顺序取第一个键相同的构造函数，不会在多个命中时返回 `Ambiguous`，也不会在 `NotSupported` 之后试下一个。

- 先遇到移动构造：`load_argument` 对 `T&&` 做 `static_cast<P&&>`，从调用方的左值移走。`ObjectRef` 没有值类别，调用方没有写 `std::move`。
- 先遇到已删除的那个：`Constructor::new_instance` 直接 `NotSupported`，后面的可用构造被挡住。典型的 move-only 类型（拷贝 `= delete`、移动可用）是否能 `new_instance` 取决于 `members_of` 的顺序，而这个顺序没有测试锁定。

`tests/phase4_test.cpp` 里仍写着「拷贝/移动的形参归一成同一个 `Class*`」。P5 之后精确形参已经分开，归一的是 decay 键。这句注释说明两套类型身份很容易被用错。

同一规则也作用在普通函数上：`void f(T&)`、`void f(const T&)`、`void f(T&&)`、`void f(T)` 的 decay 键都是 `T`。`Method::invoke` 同样取第一个，并且对 `T&&` 右值转发。`phases.md` 把「`T&&` 形参按右值转发」记成了已知限制；`Class::new_instance` 在拷贝/移动之间自动挑选，把这个限制放大成了静默移动或错误的 `NotSupported`。

建议：运行期匹配键带上引用类别（左值引用 / 右值引用 / 按值）和顶层 const。`new_instance` 在 decay 键相同的构造之间，只自动选择「按值或 `const T&`」；`T&&` 仅当调用方显式取到移动构造时才使用。多个仍同时匹配则返回 `Ambiguous`。已删除或没有 thunk 的候选应跳过，而不是终止搜索。

### ✅ 2.3 物化期硬错误：一类成员就会让整个 `class_of<T>()` 编不过

> **已处理。** thunk 实例化不合法（按值接收不可拷贝类型、返回不可拷贝类型的引用）时，`invoke` / 构造的 thunk 置空，运行期 `NotSupported`，`class_of` 照常完成。**实测补充**：只含一个 `unique_ptr` 字段的类同样编不过（与本节成员无关）。GCC 16 会实例化「反射被存进 `constexpr` 变量」的函数定义，libstdc++ `_Tuple_impl::_M_swap(const&) const` 的函数体对 `default_delete` 不合法；已把标准库类型的非 public 成员排除出成员表。`invoke_ref` 已在 4.6 补上；未做：第三方库中同类成员、标准库的 public 成员仍可能触发同一硬错误。测试：`MethodTest.MoveOnlySignaturesDoNotBreakMaterialization`、`ConstructorTest.MoveOnlyByValueParameterIsNotInvokable`。

`make_invoker` 对非 `void` 返回值一律：

```cpp
return new std::remove_cvref_t<R>((receiver->*function)(...));
```

`load_argument` 对非右值引用形参返回 `P&`（左值）。因此下面两类成员在生成 thunk 时就病态，而不是运行期 `NotSupported`：

- 返回 `const U&` / `U&`，且 `U` 不可拷贝。`new U(左值)` 要拷贝。getter 返回 `const std::unique_ptr<T>&`、`const std::optional<MoveOnly>&` 都属于这一类。
- 按值接收不可拷贝类型。形参 `std::unique_ptr<T>` 被当成左值传入，调用点要拷贝。按值返回 `unique_ptr` 本身是可以的（返回值是纯右值，能移动）。

`is_reflectable_method` 会跳过函数模板、已删除函数和特殊成员，但不会跳过「这个 invoker 实例化后不合法」的普通成员。字段路径是反过来的：不可拷贝字段的 get/set thunk 置 `nullptr`。方法路径没有这个退让，所以 `struct S { std::unique_ptr<int> p; };` 可以反射（靠 `get_ref`），一旦加上 `void reset(std::unique_ptr<int>)` 或 `const std::unique_ptr<int>& get() const`，整个 `S` 都不能 `class_of`。

这和 `crab_cpp` 笔记里 `class_of<Option<Object>>()` 失败是同一处：`object.cppm` 的 invoker 对返回引用的 move-only 载荷做拷贝。`reflect_impl_tag` 只排除了宏注入的 `get_class()`，排除不了用户自己的这类成员。

建议：invoker 按返回类别分支。引用返回且不可拷贝时不要生成按值 thunk（`invoke == nullptr`，另提供 `invoke_ref` 返回 `ObjectRef`）。按值形参在类型不可拷贝时改为移动进形参，或者同样把 `invoke` 留空。判断放在 `if constexpr` 里，保证 `class_of` 始终能完成。

---

## 3. P1

### ✅ 3.1 虚继承菱形被当成有歧义

> **已处理。** `find_upcast` 命中同一地址只算一条路径，地址不同才累加。测试：`InheritanceTest.VirtualDiamondIsOnePath`、`VirtualAndNonVirtualPathsStayAmbiguous`。

`find_upcast` 每条能走到目标类型的 public 路径都 `++paths`，不比较最终地址。

```text
struct A { int a; };
struct B : virtual A {};
struct C : virtual A {};
struct D : B, C {};
```

`class_of<D>().cast_to(d, class_of<A>())` 会走出 B→A 和 C→A 两条路，两条路的 `static_cast` 落到同一个虚基子对象，`paths == 2`，返回 `Ambiguous`。非虚菱形（`tests/phase5_test.cpp` 的 `D : B1, B2`）这样报是对的；虚基应当在地址相同时算一条路径。现在的测试只覆盖了单虚基 `VLeaf : virtual VRoot`，没有覆盖菱形虚继承。

建议：命中时若 `found == pointer` 则不要增加 `paths`；地址不同才累加。

### ✅ 3.2 `mutable` 只出现在元数据里

> **已处理。** const 对象上 `mutable` 字段可 `set`、`get_ref` 得到可写视图；静态字段的 `get_ref` 不再受实例 const 影响。引用成员在 const 对象上的规则见 3.5。测试：`FieldTest.MutableFieldIsWritableOnConstObject`、`StaticFieldViewIgnoresInstanceConstness`。

`Field::set` 在看到 `obj.is_const()` 时直接 `ReadOnly`，不看 `is_mutable()`。`get_ref` 用 `obj.is_const() || modifiers().is_const()` 决定视图是否 const，同样忽略 `mutable`。const 对象上的 `mutable` 成员因此不能写，和 C++ 语义相反。`is_mutable()` 目前只是查询位。

建议：const 对象上，仅 `mutable` 且字段本身非 const 时允许 `set` / 可变 `get_ref`。

静态字段的 `get_ref` 用了同一表达式：传入一个 const 实例时，非 const 的静态字段也会变成 const 视图。静态字段的 const 只应来自字段自己的修饰符。

### ✅ 3.3 重载解析不考虑 cv 和 ref-qualifier

> **已处理（API 有破坏性变更）。** `Class` 的全部方法查找（`get_method` / `get_declared_method` 的名字、`Class*`、类型名、模板重载）改为返回 `Result<Method, Error>`：无匹配 `NotFound`，多个匹配 `Ambiguous`，不再静默取第一个。新增 `MemberQualifiers` 与带限定符的精确查找，新增 `Method::is_invokable()`。构造函数查找仍返回 `Option`（键是精确形参，不会同键歧义）。下文「查找 API 今天返回 `Option`」是审核时的状态。未做：模板形式的带限定符查找；按接收者 constness 自动选重载。测试：`MethodTest.QualifierOnlyOverloadsAreAmbiguous`、`QualifiersSelectBetweenOtherwiseIdenticalOverloads`。

`find_method` 只比较名字和形参 `Class*`。`void scale(int)` 与 `void scale(int) const`、`void f() &` 与 `void f() &&` 的形参列表相同，结果是声明顺序中的第一个。

`&&` 成员的 `invoke` 被故意置空（`ObjectRef` 没有值类别）。若 `&&` 重载排在 `&` 重载前面，`get_method<>("f")` 拿到的是一个调用必然 `NotSupported` 的方法，可用的 `&` 重载被藏住。const 重载同理：const 对象上可能选中非 const 重载并得到 `ReadOnly`，即使存在 const 重载。

`ErrorKind::Ambiguous` 的注释写的是「重载或继承路径不唯一」。继承路径用了它，重载没有：多个命中时静默取第一个。

建议：匹配键加上 const / volatile / `RefQualifier`。模板查找可以做成 `get_method<int>(name)` 继续表示「不关心 cv」，另提供带 cv 的键；只命中一个时返回它，命中多个时返回 `Ambiguous`（查找 API 今天返回 `Option`，多命中需要改成 `Result`，或者在 `Option` 之外另给一个会报告歧义的入口）。

### ✅ 3.4 不可访问基类的错误种类是错的

> **已处理。** `is_assignable_from` / `is_instance` 改为只沿 public 基（与 `cast_to` 同一张图）；`cast_to` 对「仅经 protected / private 继承可达」返回 `NotSupported`，真正无关的类型仍是 `TypeMismatch`。新增 `is_base_of_ignoring_access` 保留原来的纯类型关系。测试：`InheritanceTest.InaccessibleBaseIsNotAssignableAndCastSaysWhy`。

public 基才生成 `cast` thunk，protected / private 基的 `cast` 是 `nullptr`，`find_upcast` 直接跳过。唯一路径不可访问时 `paths` 仍是 0，错误信息是「the target is not a base of this class」。

同时 `is_assignable_from` 用 `unchecked` 收集到的全部基类，忽略访问级别，private 基也会返回 true。调用方先看到「可以赋值」，再 `cast_to` 得到「不是基类」。

建议：不可访问路径与「图里不存在」分开。前者用现有 `NotSupported`（或单独的访问错误），文案说明是访问级别而不是继承关系。`is_assignable_from` 要么与 `cast_to` 使用同一 public 图，要么在文档里把「忽略访问」写成返回值不能预测 `cast_to` 成功。

### ✅ 3.5 引用成员的 `get_ref`，以及 move-only 引用成员

> **已处理。** 引用成员的 `get_ref` 返回被引用对象的视图（类型是 `T`，const 只取决于被引用类型，const 拥有者不传递）；`int&` 成员在 const 对象上可 `set`。`set` 的空 thunk 区分原因：声明上 const → `ReadOnly`，不可拷贝赋值 / 数组 → `NotSupported`。`api_design.md` §5 已同步。测试：`FieldTest.ReferenceMemberViewIsTheReferent`、`NonCopyableMembersAreNotSupportedNotReadOnly`。

`make_field_ref` 对位域和引用类型都返回 `nullptr`，`Field::get_ref` 因此是 `NotSupported`。位域没有地址，这个处理是对的。引用成员有地址（被引用对象的地址），`api_design.md` §5 的规则段要求 `get_ref()` 的 `type()` 是 referent 的对象类型；同一节前面的注释又说引用成员按规则返回 `Error`。实现走了错误那条。

对 `int&` 还可以 `get` / `set`（拷贝和穿引用赋值）。对 `std::unique_ptr<int>&`：`get` 因不可拷贝没有 thunk，`set` 因不可拷贝赋值没有 thunk，`get_ref` 也没有 thunk。这个成员在运行期只剩名字和偏移，值进不去也出不来。

`set == nullptr` 一律报 `ReadOnly` / "this field is not writable"。const 字段用 `ReadOnly` 合适；不可拷贝、数组成员用这个种类会把「不支持」说成「只读」。

建议：引用成员的 `get_ref` 返回 referent 的 decay 类型，const 跟随被引用类型。不可拷贝导致的失败改成 `NotSupported`。

### ✅ 3.6 const 对象的 `as` 要先把句柄本身变成 const

> **已处理。** 非常量 `as<T>()` 仅在「视图为 const 且 `T` 不带 const」时返回 `None`，`ref.as<const Widget>()` 可经非常量句柄读取 const 对象并得到 `const Widget&`。`ObjectRef::raw_data()` 去掉 `void*` 重载，只保留 `const void*`；库内需要可写地址的 `Field::set` / `get_ref` / `Method::invoke` 在 const 检查之后显式 `const_cast`。`Object::raw_data()`（拥有者，非 const 视图）不变。测试：`ConstCorrectnessTest.ConstQualifiedAsReadsConstObjectThroughNonConstHandle`、`RawDataOfNonConstHandleIsReadOnlyPointer`。

`ObjectRef::as()` 的非常量重载在 `m_is_const` 时直接 `None`，并且 `remove_cv_t` 之后去和 decay 描述符比较。非常量句柄上的 `ref.as<const Widget>()` 仍然选中非常量重载，结果还是 `None`。唯一能读的写法是把句柄改成 `const ObjectRef` 再 `as<Widget>()`，`tests/phase4_test.cpp` 测的就是这条。API 草图里「const 重载返回 `const T&`」在句柄是左值时到不了。

`raw_data()` 的非常量重载对同一句柄做 `const_cast` 返回 `void*`。`set` / `invoke` 守住的 const，可以被 `raw_data()` 绕开。

建议：非常量 `as<T>()` 在视图为 const、且 `T` 带 const（或调用方请求的是常量引用）时，返回 `const U&`。`raw_data()` 在 `m_is_const` 时只提供 `const void*`。

### ✅ 3.7 元素类型带 cv 的数组读不出来

> **已处理。** `Array` 的内部视图改用元素的 decay 描述符，并把元素顶层 const 折进 `ObjectRef::is_const()`：`get` 走无 cv 元素的 `clone`；`get_ref` 得到键为 `int`、`is_const()` 为 true 的句柄，`as<const int>()` 可读；`set` 对 const 元素返回 `ReadOnly`（原先是 `NotSupported`）。没有改描述符层的 `clone` / `copy_assign` 生成条件，cv 元素描述符本身仍无这两个 thunk，由 `Array` 层统一回落到 decay 描述符。`get_element_type` 仍返回带 cv 的元素 `Class`。测试：`ArrayTest.ConstElementArraysAreReadableThroughUnqualifiedHandles`。

对象句柄的不变量是：`Object` / `ObjectRef` 键在 `remove_cvref_t`，const 用 `is_const()` 表示。`Field::get_ref` 遵守这一点（用 `type->decayed`）。`Array::get_ref` 把 `element` 描述符原样放进 `ObjectRef`，const 只复制数组视图的 `is_const()`。

`const int[N]` 的元素描述符是 `const int`。`Array::get` 走元素的 `clone`，而 `const int` 不是 `is_unqualified_object`，`clone` 为空，读值得到 `NotSupported`（`int` 明明可以拷贝）。`get_ref` 得到的句柄类型是 `const int` 且 `is_const() == false`，`as<int>()` 和 `as<const int>()` 都因为描述符对不上 `bundle<int>` 而失败。元素既不能按值读，也不能 `as` 出来。

建议：`Array::get_ref` 使用元素的 decay 描述符，并把元素顶层 const/volatile 折叠进 `ObjectRef::is_const()`。`clone` / `copy_assign` 对「去掉顶层 cv 后可拷贝」的类型生成，而不是要求描述符本身无 cv。

### ✅ 3.8 动态数组：过对齐与长度回绕

> **已处理。** `new_array_thunk` 分配前检查 `Offset + count * sizeof(E)` 不溢出，溢出返回 `nullptr`，`Array::new_instance` 据此返回 `InvalidArgument`（"the requested array length is too large"）。`alignof(E)` 超过 `__STDCPP_DEFAULT_NEW_ALIGNMENT__` 时用 `operator new(size, std::align_val_t)` 分配，释放用对应的 `operator delete(ptr, std::align_val_t)`；块首对齐到 `alignof(E)`，`Offset` 又是它的倍数，所以首元素也对齐。测试：`ArrayTest.DynamicArrayHonorsOverAlignedElements`、`DynamicArrayRejectsOverflowingLength`。

`new_array_thunk` 使用 `::operator new(Offset + count * sizeof(E))`。默认 `operator new` 只保证 `__STDCPP_DEFAULT_NEW_ALIGNMENT__`（常见是 16）。`dynamic_array_offset` 只是把「长度头之后」凑成 `alignof(E)` 的倍数；块本身只有 16 字节对齐时，`alignas(32)` 及更高的元素仍然未对齐。释放用的是不带 `align_val_t` 的 `operator delete`，和过对齐分配也不成对。

`Offset + count * sizeof(E)` 在 `count` 很大时回绕成一个很小的分配，随后仍按原始 `count` 默认构造。这是越界写。长度来自 `Array::new_instance` 的参数，序列化或脚本层会把不可信整数传进来。

建议：分配前检查乘法与加法是否溢出；对齐大于默认新对齐时改用 `operator new(size, std::align_val_t(alignment))`，释放使用对应的 `operator delete`。

### ✅ 3.9 volatile 被丢掉

> **已处理（采用「记录 `is_volatile()`」方向）。** `ObjectRef` 新增 `is_volatile()`，`ObjectRef(T&)` 保留 `volatile T`；键仍是 `remove_cvref_t`。规则：
> - `as<T>()`（两个重载）在视图为 volatile 而 `T` 不带 volatile 时返回 `None`，`as<volatile T>()` 是显式的 volatile 访问。
> - `Field::get` / `Field::set` 对 volatile 接收者返回 `NotSupported`（引用成员与静态字段除外，与 const 的规则一致；`mutable` 不抵消 volatile）。`Field::get_ref` 的视图继承 volatile，声明为 `volatile` 的成员（或引用成员的被引用类型带 volatile）本身也给 volatile 视图。
> - `Method::invoke` 在 volatile 接收者上调用非 volatile 方法返回 `NotSupported`；volatile 方法可用于任意接收者。
> - 参数：volatile 对象不能按值传递（会按非 volatile 读取），也不能绑定到非 volatile 引用。`ObjectRef::clone()` 对 volatile 视图返回 `NotSupported`。
> - `Class::cast_to` 的结果继承 volatile。`Array` 把元素（及数组本身）的 volatile 折进视图：`get` / `set` 返回 `NotSupported`，`get_ref` 给 volatile 视图。
> - 顺带修了一个编译错误：含 `volatile` 数据成员的类，`get_ref` thunk 里 `volatile T*` → `const void*` 的 `static_cast` 无法编译，改为经 `const volatile void*`。
>
> 测试：`VolatileCorrectnessTest.*`（4 项）、`ArrayTest.VolatileElementsAreOnlyAccessibleThroughVolatileViews`。

`ObjectRef` 只记录 const，不记录 volatile。`volatile T` 的句柄键是 `remove_cvref_t<T>`，`is_const()` 为 false。之后的 `Field::set` / 非 volatile 方法调用会按非 volatile 对象访问。const 有运行期检查，volatile 没有。

建议：要么同样记录 `is_volatile()` 并在写和非 volatile 调用上拒绝，要么在 `TypeKind` / 文档中写明对象句柄不保留 volatile、对 volatile 对象使用反射是未定义行为。现在的中间状态（类型分解 API 承认 volatile，对象层悄悄丢掉）两边都不安全。

---

## 4. P2 设计与易用性

### ✅ 4.1 调用点写不出字面量

> **已处理。** 新增 `Arg`（`Args` 的花括号列表元素类型）：隐式接受 `ObjectRef`、`Object` / 具体类型的左值，以及**临时值**；`Args(std::initializer_list<Arg>)` 取代 `initializer_list<ObjectRef>`。临时值活到整条调用的完整表达式结束，所以 `cls.new_instance({3, 4})`、`method.invoke(obj, {Vec(1, 2), true})` 可以直接写。没有给 `ObjectRef` 本身加右值构造（`ObjectRef r = 3;` 仍然编译不过，避免悬空）；`Args` 仍是视图，临时值写在列表里时不要把 `Args` 存到之后的语句用。字符串字面量（`const char(&)[N]`）按 `const char*` 传，`char` 缓冲等非 const 数组保持原行为。
>
> 转换没有做：保持精确匹配（`{short{3}}` 仍是 `short`、不会变成 `int`），`api_design.md` §6 的「数值提升/基类指针」一句已改成「只做精确类型匹配」，与 `phases.md` 一致。基类指针转换未做，仍可以之后单独加。
>
> 测试：`ArgumentLiteralTest.ConstructWithLiterals`、`InvokeWithTemporariesAndStringLiterals`。
>
> **遗留（不属于 4.1；已在 5.3 修复）**：当时本机上反射 `std::string` 会让 GCC 汇编阶段报 `make_field_ref<... basic_string::_M_local_buf>` 局部符号重复定义，所以测试里没有用 `std::string`。原因和修法见 5.3 的备注；现在 `std::string` 可以物化，`method.invoke(obj, {std::string("x")})` 仍不能写（实参必须是左值），但先声明具名 `std::string` 变量再传入可以。

`ObjectRef` 只有 `T&` 构造函数，`Args` 的模板构造也是 `Ts&...`。纯右值绑不上：

```cpp
cls.new_instance({3, 4});                 // 不能编译
method.invoke(obj, {std::string("x")});   // 不能编译
```

字符串字面量是左值，能编译，但类型是 `const char[N]`，对不上 `const char*` 或 `std::string`。`bool` / `short` 也不会提升成 `int`。示例和测试全部先声明具名变量再传入，把这个限制藏住了。

`api_design.md` §6 还写着 invoke 会尝试数值提升和基类指针转换。实现是描述符指针相等，`phases.md` 的「当前语义」也写成精确匹配。文档两处互相矛盾，精确匹配才是代码。

建议：给 `Args` 一条拥有临时值的路径（内部 `Object` 小缓冲，活过这次调用），或提供 `arg(T&&)` 把右值装箱成左值 `Object`。转换如果仍不做，把 §6 那句从 API 文档里删掉，避免调用方以为会有提升。

按值类别区分之后，基类指针转换仍然可以单独做：它是指针描述符之间的关系，不必开放全部隐式转换。

### ✅ 4.2 按名字取方法不是「取零参重载」

> **已处理。** 关于返回类型：名字-only 查找在 3.3 已经改成 `Result<Method, Error>`（无 → `NotFound`，重名 → `Ambiguous`，唯一 → 返回），比建议里的 `Option` 信息更多，下文「否则 `None`」是审核时的状态。本条剩下的是：
> - 注释写明 `get_method(name)` 的含义是「叫这个名字的方法」（唯一即返回，不论形参），不是零参重载；零参请写 `get_method("name", {})` 或 `get_method<>("name")`。
> - 空 `{}` 二义性已解决：名字版（`initializer_list<string_view>`）两个重载（`get_method` / `get_declared_method`）改成带默认模板形参 `template<typename = void>` 的模板。它不参与推导，但作为模板在空列表上输给非模板的 `Class*` 版，所以 `get_method("f", {})` 取零参重载；`{"int"}` 仍走类型名版。
> - `get_constructor()` 与方法的「空实参」现在对称。
>
> 测试：`MethodTest.NameOnlyLookupAndTheNullaryOverload`。

`get_method(std::string_view)` 返回该名字的第一个方法，不看参数个数。`get_method<>("name")` 才是零参精确匹配，但 `get_method("name")` 会优先选中非模板重载，用户写不出那个空尖括号的机会。`get_method("name", {})` 又在 `initializer_list<const Class*>` 和 `initializer_list<string_view>` 之间二义（`phases.md` 已记录）。

`get_constructor()` 因为带默认实参，零参构造是对的。方法和构造的「空实参」不对称。

建议：名字-only 的查找在命中多于一个时不要返回第一个。零参方法用 `get_method<>("name")` 可以保留，但名字-only 重载应改成「仅当该名字唯一」否则 `None`，并在注释里写明零参必须走模板或 `Class*` 列表。

### ✅ 4.3 动态数组的 `Class` 是半截描述符

> **已处理。** 动态数组描述符补全：`name` / `qualified_name` 为稳定的 `"E[]"`（`E` 取元素的 identifier 或 display 串，存静态存储）；`has_canonical_name = false`，所以 `register_class` 跳过它，不再占 `""`；`decayed` 指向自己；元素可拷贝时新增 `dynamic_array_clone`（同长度、逐元素拷贝构造，用与分配相同的对齐分配），`Object::clone()` / `ObjectRef::clone()` 可深拷贝，元素不可拷贝仍是 `NotSupported`。`Class::get_array_extent()` 对动态数组返回 `None`（静态 `T[N]` 仍是 `Some(N)`，`T[]` 仍是 `Some(0)`），长度只走 `Array::get_length`。`size` 仍为 0：长度是每个对象的属性，类没有静态大小。测试：`ArrayTest.DynamicArrayClassIsAFullDescriptor`、`DynamicArrayCloneCopiesElements`、`DynamicArrayOfTrackedElementsClonesAndDestroys`。

`Array::new_instance` 的产物指向 `bundle<E>` 里的 `dynamic_array_desc`。这段初始化设置了 `kind`、`element`、`release`，没有设置：

- `name` / `qualified_name`（空 `string_view`）
- `has_canonical_name`（`TypeDescriptor` 默认值是 `true`）
- `decayed`（空指针；普通类型在 bundle 末尾都会写）
- `clone`（元素可拷贝时，整个动态数组仍然 `Object::clone()` → `NotSupported`）
- `size`（`get_size()` 为 0；运行期长度只在分配头里，`Class::get_array_extent()` 也是 0）

`register_class` 看到 `has_canonical_name == true` 会把空名字 `try_emplace` 进全局表。不同元素类型的动态数组如果被注册，第一个占住 `""`，其余被静默丢掉。`for_name("")` 就会碰到它。

建议：动态数组 `has_canonical_name = false`，名字做成稳定的 `"E[]"` 这类展示串；补上 `decayed`（指向自己）和按元素拷贝实现的 `clone`。`get_array_extent()` 对动态数组返回 `None`，长度只走 `Array::get_length`。

### ✅ 4.4 修饰符有位，但物化不置位

> **已处理。**
> - `INTERFACE`：类满足「抽象；自身无非静态数据成员；所有基类也是 interface；所有非静态成员函数都是纯虚（特殊成员如 `virtual ~I() = default;`、静态函数除外）」时置位。`modifiers().is_interface()` 不再恒假。
> - `FINAL`：`final` 虚函数现在置位（`is_final()`），非虚函数不会有。
> - 类的访问级别：嵌套类取其声明的 `PROTECTED` / `PRIVATE`，命名空间作用域的类仍是 `PUBLIC`。
> - `INLINE` / `CONSTEXPR` / `CONSTEVAL`：GCC 16.2 的 `<meta>` 没有 `is_inline` / `is_constexpr` / `is_consteval`，**算不出来**（审核建议的 `inline` 也一样）。采用审核的另一条路：删掉公共的 `is_inline()` / `is_constexpr()` / `is_consteval()` 和 `to_string()` 里的对应词，位值保留并注明「预留、永不置位」，这样调用方不会把「没填」当成「确定为 false」。这是对公共 API 的缩减。
>
> 测试：`ModifierTest.InterfaceIsAbstractStatelessAndAllPureVirtual`、`FinalOnVirtualFunctions`、`ClassAccessFollowsNesting`。

`Modifier` 对外有 21 个位。类修饰符只写了 `PUBLIC | ABSTRACT | FINAL`。方法修饰符写了访问、const、volatile、static、noexcept、virtual、pure、override。

始终为 false 的查询：

| 位 | 现状 |
|---|---|
| `INTERFACE` | 没有「无数据成员且全纯虚」的判断，`is_interface()` 恒假 |
| `INLINE` | 方法和字段都不置位 |
| `FINAL` | 只用于 `final` 类；`final` 虚函数不置位 |
| `CONSTEXPR` / `CONSTEVAL` | `phases.md` 已说明 GCC 16.2 没有对应谓词；位仍然留在公共 API 上 |
| 类的访问级别 | 任何类都标 `PUBLIC`，私有嵌套类也是 |

调用方无法区分「这个类型确实不是 interface」和「库没填这一位」。

建议：能算的（`INTERFACE`、函数 `final`、`inline`）填上。编译器没有谓词的位不要出现在公共 `is_xxx()` 里，或者 `is_constexpr()` 的注释标明恒为 false。

### ✅ 4.5 删除的函数和特殊成员被整段拿掉

> **已处理。**
> - **已删除的普通函数**留在表里：带 `DELETED`，`invoke == nullptr`，`invoke` 返回 `NotSupported`（消息写明 deleted），`Method::is_deleted()` / `is_defaulted()` 新增。带已删除重载的名字现在是「重名」：`get_method(name)` 返回 `Ambiguous`，用形参列表分别取。
> - **赋值运算符**：用户声明的拷贝 / 移动赋值现在列出（带 `DEFAULTED` / `DELETED`），可 `invoke`（移动赋值仍遵守 `T&&` 形参只在显式取到 `Method` 时接受）。隐式声明的不列出，避免每个类的表里都多两项。运算符没有 identifier，`get_name()` 是 display 串，按形参列表和 `get_signature()` 找。其余特殊成员（构造 / 析构）规则不变。
> - **C 可变参数**：列在表里（固定形参可见，`...` 只出现在 `get_signature()`），`invoke == nullptr`，不再因 `member_function_pointer` 没有特化而硬错误。
> - **函数模板**（含 deducing this 的模板，如 `template<class Self> int f(this Self&&)`）：`is_function` 为假，本来就不会进表，实测不影响物化。
> - **非模板的显式对象成员**（`int f(this Locked&, int)`）：不是模板，`is_function` 为真，且 `parameters_of` 把对象形参算作第一个，形参表会错、`member_function_pointer` 会拼出 `extract` 不接受的类型。现在**不枚举**。这一条与下文原文不同：审核原文把「deducing this」与可变参数并列，实际应分成「可变参数」「函数模板」两类；显式对象的非模板成员是第三种，单独处理。
>
> 测试：`MethodTest.UserDeclaredAssignmentOperatorsAreListed`、`DeletedAndVariadicFunctionsAreListedButNotInvokable`。

`is_reflectable_method` 排除 `is_special_member_function`、`is_template`、`is_deleted`。后果：

- 用户声明的拷贝赋值 / 移动赋值不会出现在 `get_methods()` 里，不能 `invoke`。
- 已删除的普通函数直接消失。构造函数的已删除重载却保留，并带 `DELETED`（而且会参与 2.2 的屏蔽）。
- 成员函数模板、C 可变参数（`...`，`member_function_pointer` 没有对应特化）、显式对象形参（deducing this）都会让物化失败或被静默跳过。可变参数是硬错误：`member_function_pointer` 匹配不到函数类型。

建议：已删除函数与已删除构造对齐——留在表里，`invoke == nullptr`，`NotSupported`。特殊成员里的赋值运算符至少要能列出来。可变参数和函数模板（含带推导的 deducing this）在进 invoker 之前剔除或标为不支持，避免一整个类编译失败。

### ✅ 4.6 返回引用时，盒子里的类型和 `get_return_type()` 不是同一个

> **已处理。**
> - `Method::invoke` 的文档写明：产物类型是返回类型的 `remove_cvref`，引用返回得到的是 referent 的**拷贝**；referent 不可拷贝时 `NotSupported`（消息提示改用 `invoke_ref`）。
> - 新增 `Method::invoke_ref(obj, args) -> Outcome<ObjectRef>`：对返回 `T&` / `const T&` / `T&&` 的方法给出 referent 的视图，不拷贝。视图的 `type()` 是 referent 的对象类型 `T`（句柄不携带引用，与 `Field::get_ref` 一致），`is_const()` / `is_volatile()` 取决于被引用类型；写入作用于原对象。接收者与实参的检查与 `invoke` 共用（`NullInstance` / `TypeMismatch` / `ReadOnly` / `InvalidArgument`）。非引用返回的方法调用 `invoke_ref` 是 `NotSupported`。
> - 新增 `Method::returns_reference()` 与 `Method::is_ref_invokable()`；`MethodDesc` 新增 `invoke_ref` thunk（引用返回的方法才生成）。
> - 2.3 里「不可拷贝引用返回」的成员（如 `const std::unique_ptr<T>&`）现在虽不能 `invoke`，但可以 `invoke_ref`；按值接收不可拷贝类型的形参仍同时禁用两者。
> - 视图的生命周期仍由被调函数的引用决定，库不延长（返回局部对象引用等属调用方的 UB）。
>
> 测试：`MethodTest.ReferenceReturnsBoxACopyUnlessInvokedByReference`、`InvokeRefKeepsConstnessChecksAndArguments`、`InvokeRefReachesNonCopyableReferents`。

引用返回在可拷贝时会被拷进新的 `Object`，盒子的 `type()` 是 decay 后的对象类型，`Method::get_return_type()` 仍是 `T&`。`result.as<T&>()` 失败，`result.as<T>()` 成功。调用方也摸不到原来的那个对象，只能改副本。

这和「对象句柄不携带引用」的不变量一致，但返回类型 API 没有告诉调用方「invoke 的产物已经去掉了引用」。不可拷贝时则升级成 2.3 的硬错误。

建议：`invoke` 的文档写明产物类型是 `remove_cvref` 后的返回类型；另加 `invoke_ref`，在返回引用时给出 referent 的 `ObjectRef`（生命周期仍由被调函数的引用绑定规则决定，库侧只避免多余拷贝和硬错误）。

### ✅ 4.7 继承成员要调用方自己爬

> **已处理。** `get_fields()` / `get_methods()` 不变（仍只含本类 public 成员）。新增 `Class::get_field_recursive(name)`、`get_method_recursive(name)` 与 `get_method_recursive(name, {形参类型})`，返回 `Result<Field/Method, Error>`，规则按 C++ 名字查找：
> - 本类命中即止，近处的声明隐藏基类同名成员；否则沿 public 基（有上转 thunk 的边，与 `cast_to` 同一张图）递归，protected / private 基不遍历。
> - 多个基类给出不同成员 → `Ambiguous`；同一非 static 成员位于不同子对象（非虚菱形、一虚一实）→ `Ambiguous`；共享虚基只算一个子对象，成员只命中一次；同一 static 成员经多个子对象到达不算歧义。子对象身份 = 最后一条虚继承边之后的类链，纯静态计算，不需要对象。
> - 最近声明该名字的类里该名字有重载（`get_method_recursive(name)`）→ `Ambiguous`，用带形参的重载区分。带形参版本每层按「名字 + 形参」匹配，所以派生类里同名异参的函数**不会**隐藏基类重载（与 C++ 不同，已写进注释）。
> - **未实现**虚基的支配规则（`B1` 覆盖共享虚基 `A` 的成员而兄弟 `B2` 未覆盖时，C++ 取 `B1`，这里按 `Ambiguous`）。
> - 命中成员属于 `get_declaring_class()`；调用前仍需 `cast_to(obj, member.get_declaring_class())` 取基类子对象，没有自动做这一步（`Field::get` / `Method::invoke` 的接收者必须是声明类本身）。
> - **实现备注**：最初的实现（`Class` 内嵌套模板 `MemberHit` + 非模板成员函数里的 lambda）会让 `speculo-object.gcm` 读取时报 `failed to read compiled module cluster: Bad file data`（GCC 16.2 模块流化问题）。改成 `detail::MemberHit` + 每层查找用 `Class` 内私有函数对象（`OwnFieldByName` 等）后正常；这两处是同时改的，没有单独确认哪一处是触发原因，改回去前请先重新验证。
>
> 测试：`InheritedMemberTest.*`（phase5）。

`get_fields()` / `get_methods()` 只含本类 public 成员，不含继承。这是相对 Java 的有意偏差，`design.md` §7 写了。日常使用的成本是：读基类字段必须先 `cast_to` 再在基类 `Class` 上查，而 `cast_to` 还有 2.1 的接收者前提。多继承时没有一个「从对象的静态类型出发、按名字找最近成员」的入口。

建议：保持不自动合并继承表（避免虚函数和隐藏成员的语义），但提供 `get_field_recursive` / `get_method_recursive`，沿 public 基查找，多条命中返回 `Ambiguous`。不在本次偏差上改默认的 `get_fields()`。

### ✅ 4.8 枚举值的盒子类型不是底层类型

> **已处理（API 有破坏性变更，库处于 0.1.0，按用户决定直接改）。**
> - `Enumerator::value()` 按真正的底层类型装箱：`enum : int` 得 `int`，`enum : short` 得 `short`，`enum : unsigned char` 得 `unsigned char`，盒子的 `type()` 等于 `Class::get_underlying_type()`。不再统一放大成 `int64_t` / `uint64_t`。实现：`TypeDescriptor` 新增 `enum_box` thunk，`value()` 用它构造底层类型的对象。`to_int64()` / `to_uint64()` 保留，给不关心底层类型的调用方。
> - `Class::enum_from_value` 改为模板，接受任意整数类型（不含 `bool` 和字符类型），**按数学值比较**，不再靠位模式：`-1` 不匹配 `unsigned long long` 的 `0xFFFF...` 枚举项；`0xFFFFFFFFFFFFFFFFull` 可以直接传，不用转成有符号数。负数与无符号枚举、过大的值与窄枚举都是 `None`。原来 `enum_from_value(static_cast<int64_t>(-1))` 回到无符号枚举的写法不再成立，已改测试。
> - `value()` 的注释现在与实现一致。
>
> 测试：`EnumeratorTest.ValueIsBoxedAsTheUnderlyingType`、`FromValueTakesAnyIntegerTypeByItsValue`，以及更新后的 `UnscopedAndUnsigned`。

`Enumerator::value()` 有符号底层一律装箱 `int64_t`，无符号一律装箱 `uint64_t`。`enum : int` 的 `value().as<int>()` 是 `None`，要 `as<int64_t>()`。函数注释写的是「boxed as its underlying integer type」，和实现不符；`api_design.md` 写的 int64/uint64 装箱才是实现。

`enum_from_value` 只收 `int64_t`。`unsigned long long` 的 `0xFFFF...` 要传 `static_cast<int64_t>(-1)` 才回得去，测试就是这么写的。能回环，但调用方必须知道位模式约定。

建议：`value()` 按真正的底层类型装箱（`unsigned` 就是 `unsigned`）。`enum_from_value` 增加 `uint64_t` 重载，或改收底层类型的 `Object`。注释改成和装箱规则一致。

### ✅ 4.9 名字就是 GCC 的 `display_string_of`

> **已处理。**
> - 新增 `Class::get_normalized_name()` 和 `detail::normalize_type_name`（constexpr，编译期给描述表、运行期给查询串，两边同一份实现）。规则：丢掉 `{aka ...}`；空白规范化（`int [3]` → `int[3]`、`const int *const` → `const int* const`）；基本类型用短写法（`long long unsigned int` → `unsigned long long`，`long unsigned int` → `unsigned long`，`short int` → `short`，`unsigned` → `unsigned int`，`signed int` → `int`，`signed char` / `long double` 保留）；非法组合（`unsigned long long long`）原样保留，不并入任何真实类型；`std::basic_string_view<char>` → `std::string_view`、`std::__cxx11::basic_string<char>` → `std::string`。
> - 注册表两份键都登记（编译器拼写 + 规范拼写，各自首个注册者胜出）。`for_name` 先按原样查，查不到再规范化查询串，所以 `"unsigned long long"`、`"long long unsigned int"`、`"  unsigned  long long "`、`"const p6::Point &"` 都命中同一个类型。`get_qualified_name()` 保持编译器拼写不变。
> - 失败可诊断：新增 `Class::registered_names()`（两种拼写、已排序）；按类型名取重载（`get_method(name, {"T"})`）先解析名字，未注册时返回 `NotFound`，message 写出该名字和已注册名字（前 16 个，其余计数）。以前这种情况与「没有这样的重载」无法区分。构造函数的按类型名查找仍返回 `Option`，没有错误通道。
> - **实测与审核描述的出入**：`display_string_of` 本身并不带 ` {aka ...}`（那是诊断信息的写法），`std::string_view` 在 GCC 16.2 下也是 `std::basic_string_view<char>` 而不是别名名。`{aka}` 的剥离只是防御性的。另外，实测时同时 `class_of` 了 `std::string` / `const std::string&` / `std::vector<int>`，汇编阶段报 `make_field_ref ... basic_string ... _M_local_buf` 符号重复定义（`std::map<std::string, int>` 另外还会触发 2.3 说的标准库成员硬错误）；没有逐个隔离是哪个类型，但看起来是 `std::string` 的问题，非本次引入。所以 `std::string` 这条别名只验证了规则、没有实测到类型。
> - 一个新的同名情形：真实的无界数组类型 `int[]` 现在以 `int[]` 注册（旧键是 `int []`），与动态数组的显示名 `int[]` 同形；动态数组本身仍不注册，`ArrayTest.DynamicArrayClassIsAFullDescriptor` 相应改成「不得查到动态数组类」。
> - 未做：对模板实参里的别名展开（如 `std::vector<std::string>` 里的 `std::string`，只替换了上面两个整串）、默认模板实参的省略；规范名只保证「同一类型的常见写法 → 同一串」，不保证跨编译器相同。
>
> 测试：`RegistryTest.NormalizedNamesAreTheSameOnEveryCompiler`、`ForNameAcceptsEveryReasonableSpelling`、`RegisteredNamesListsBothSpellingsSorted`、`ParameterTypeNamesUseTheSameSpellings`。

`for_name` / 类型名重载的键是 `display_string_of` 的原文。`std::string` 一类会带 `{aka std::basic_string<...>}`，别名（`std::string_view`）不是一个稳定的逻辑名。空格、`const` 的位置也必须和 GCC 16.2 的打印逐字相同，示例里的 `"const Vec&"` 就是这种契约。

`has_canonical_name` 用「无内部链接且祖先链上没有函数」代替 `has_identifier`，这个判断本身是对的（`phases.md` 记录过 `has_identifier` 对 `int` 为 false、对匿名命名空间为 true）。脆弱的是键的文本，不是「该不该注册」。

建议：注册两份键：展示串，以及一份库自己规范化的名字（至少去掉 ` {aka ...}`）。查不到时 `None` 可以接受，但应在 `Error` 或调试助手里把「已注册的规范名」打出来。现在失败就是 `None`，调用方不知道是没注册还是空格写错。

### ✅ 4.10 宏和注册表的使用前提容易踩

> **已处理（仅文档，不改行为）。** 逐条结论：
> - `SPECULO_IMPL(Type)` 信任宏参数：**不改**。写错类型本来就是调用方的错，在宏里加 `static_assert(std::is_same_v<decltype(*this), ...>)` 之类既挡不住基类写成派生类，又会让宏更重；决定由使用者自己负责。
> - 虚 / 非虚版本混用造成名字隐藏：**不改**。`macros.h` 里原本就写明「不要混用」，保持。
> - `SPECULO_REGISTER` 与静态初始化顺序：审核建议的「改成首次使用时注册」**做不到**。`class_of<T>()` 已经是首次使用时注册，宏存在的意义正是「类型从未被 `class_of` 过、只有名字」，这种类型除了静态初始化没有别的触发点。真正受影响的只有一种用法——在另一个翻译单元的静态初始化期里 `for_name`。注册表本身是函数局部静态，注册动作在任何顺序下都安全。处理：在 `macros.h` 的 `SPECULO_REGISTER` 说明和 `Class::for_name` 的文档里写明「不要在静态初始化期调用 `for_name`；`main()` 及之后没有问题」。
> - 宏放头文件时每个翻译单元各物化一次：**不改**，也无法在库侧合并编译时间。`macros.h` 里补了建议（放在 `.cpp` 里，或只在确有需要的头文件里用）。
>
> 无代码行为变化，没有新增测试。

- `SPECULO_IMPL(Type)` 信任宏参数。写错类型、或在基类里写成派生类，编译仍通过，`get_class()` 返回另一个 `Class`。
- 同一继承链混用虚与非虚版本时是名字隐藏，文档写了，编译器没有诊断。
- `SPECULO_REGISTER` 是各翻译单元里的静态初始化。`for_name` 若发生在另一个翻译单元的静态初始化期间，注册器可能还没跑。`class_of<T>()` 的自动注册是函数局部静态，谁先调用谁注册；宏这条路径没有这个保证。
- 宏放在头文件里会在每个包含它的翻译单元各物化一次该类型（链接期 `inline constexpr` 能合并副本，编译时间不会合并）。

建议：注册改到 `class_of` 已经在用的那种「第一次使用时」路径，宏只在动态初始化不可依赖的场景作为显式触发器，并在头文件注释里写明不要在静态初始化里 `for_name`。

---

## 5. 封装、错误模型和工程

### 5.1 `detail` 是公共 API

> **已处理。**
> - `speculo::detail` 不再导出，三个分区里原先嵌在 `export namespace speculo` 里的 `detail` 块都拆了出来；只保留导出的 `detail::reflect_impl_tag`（宏要用）。
> - C++ 模块的「导出」只决定名字能否被直接写出来，不决定可达性：用户仍能通过 `auto` 或成员访问拿到非导出类型的对象。所以真正挡住访问的是把入口收成 private：`Object::from_raw` / `Object::descriptor`、`ObjectRef::descriptor` 与内部构造、`Class::descriptor`、`Class` / `Field` / `Method` / `Constructor` / `Enumerator` 的描述符构造函数，以及 `Constructor::get_parameter_descriptors` 都改为 private，句柄类之间互为 friend，物化代码（`class_of`、`register_class` 等）通过 `detail::Access` 访问。
> - 空句柄统一为 `panic`：`Class` / `Field` / `Method` / `Constructor` / `Enumerator` 的 `m_desc` 换成 `detail::CheckedDesc<Desc>`，`operator->` 在空指针上 `crab_cpp::panic`，不再是未定义行为。`Object` / `ObjectRef` 原本就做了空检查（`type()` 在空对象上 `panic`），行为保持。
> - 测试：`phase5_test.cpp` 里原先直接读 `Class::descriptor()` 的断言删掉（该信息已无公共入口）。`-fno-exceptions` 下空句柄 `panic` 会终止进程，没有加死亡测试。

`codeStyle.md` 要求 `speculo::detail` 不从伞模块导出。`object.cppm` / `descriptor.cppm` / `registry.cppm` 都是 `export namespace speculo::detail`。伞模块 `export import` 这些分区之后，`TypeDescriptor`、thunk 函数指针、`TypeBundle` 都是用户可见的。

宏确实需要 `detail::reflect_impl_tag` 出现在别的翻译单元里。需要导出的是这个标注类型，不是整套物化机。

另外这些本该是内部的入口是 public 的：`Object::from_raw`、`Object::descriptor`、`ObjectRef::descriptor`、`Class::descriptor`、`Constructor::get_parameter_descriptors`，以及 `ObjectRef(const void*, const TypeDescriptor*, bool)`。默认构造的 `Class` / `Field` / `Method` 把 `m_desc == nullptr`，`get_name()` 等直接解引用，是未定义行为；`Object::type()` 在空对象上会 `panic`。句柄的空状态不统一。

建议：描述符类型留在模块私有部分；对外只导出 `reflect_impl_tag`。空句柄的查询统一 `panic`（程序员错误）或返回 `Option`。`from_raw` 收到内部。

### 5.2 错误没有上下文

> **已处理。**
> - 失败路径用 `std::format` 写上下文：句柄方法的错误以 `Field::set(p6::Point::x)` / `Method::invoke(<完整签名>)` / `Constructor::new_instance(<签名>)` 开头；实参不匹配会指出第几个实参、形参声明类型、实参实际类型，以及具体违反的规则（精确类型、const 实参绑非 const 引用、volatile、右值引用需显式选定）；实参个数不符写出期望和实际；接收者不符写出实际类型与声明类。新增内部函数 `detail::argument_mismatch` / `receiver_mismatch` / `describe_arguments`，`accepts_argument` 的判定规则没有变。
> - `Class::new_instance` 的 NotFound / Ambiguous / 已删除 / 抽象类，写出类名和实参类型列表（Ambiguous 还列出冲突的两个构造函数签名）；`cast_to` 写出两端类名；成员查找（`get_method*` / `get_field_recursive` / 递归查找的歧义）写出类名和成员名；`Array` 的越界写出下标与长度，类型不符写出两侧类型；`clone` / `take` 等也带上类型名。成员查找的几个私有辅助函数因此由 static 改为 const 成员。
> - `error.cppm` 增加 `std::formatter<ErrorKind>`（`"TypeMismatch"`，支持宽度/对齐）和 `std::formatter<Error>`（`"TypeMismatch: <message>"`）。
> - 消息只是诊断文本，没有结构化字段；程序逻辑仍应判断 `kind`。
> - 测试：`phase6_test.cpp` 的 `ErrorMessageTest.*`（7 个）覆盖字段 / 方法 / 构造函数 / 接收者 / 查找 / 类型转换 / 数组 / formatter。

`design.md` §6 要求 `Error` 的消息带类型名和成员名。实现里的消息是固定英文字符串（`"Method::invoke() argument type mismatch"` 这类），不包含哪个方法、第几个参数、期望类型和实际类型。重载一多，`TypeMismatch` 无法定位。

`Error` 也没有 `std::formatter`。库自己的示例用 `unwrap()`，失败路径直接终止，用户要自己拆 `kind` 和 `message`。

建议：失败路径用 `std::format` 把声明类、成员名、参数下标、`get_qualified_name()` 写进 `message`。给 `Error` / `ErrorKind` 提供 formatter。

### 5.3 热路径和「零运行时开销」

> **部分处理（按用户决定：签名闭包的递归物化保持现状）。**
> - 实参指针不再进堆：新增 `detail::ArgumentPointers`，≤ 8 个实参（与 `Args` 的内联容量一致）放在对象自身的数组里，超过才退回 `std::vector`。`Method::invoke` / `invoke_ref`、`Constructor::new_instance` / `construct_at` 都改用它。测试：`ArgumentPointersTest.InlineAndHeapCallsBothWork`（8 个和 10 个实参各一次）。
> - 按类型名查构造函数（`get_constructor({"int", "double"})` / `get_declared_constructor`）改为先把每个名字解析一次，再按 `Class*` 比较；取锁次数从「候选数 × 形参数」降到「名字个数」。名字未注册仍返回 `None`（返回类型是 `Option`，带不了消息；方法版本返回 `NotFound` 和提示）。删除了 `matches_type_names`。测试：`RegistryTest.ConstructorLookupByTypeNameResolvesNamesOnce`。
> - **没做：invoker 按需实例化。** invoker 是描述表里的函数指针，描述表是编译期常量，取地址即实例化；运行期按名字查找时没有类型信息，无法延后生成。要按需只能让用户在编译期列出成员，那会改变库的定位。真正的成本是「签名闭包」（返回 / 形参 / 字段类型递归物化为完整 bundle），不是 invoker 本身；可行的缓解是对某些类型做「浅物化」（例如默认不展开 `std::` 类型的成员），用户决定先不做。
> - **产物大小实测（GCC 16.2，Release，`-fno-rtti -fno-exceptions`，静态链接 libspeculo）**：只链接库、不反射任何类型的程序 252 KB；反射 1 个简单类（2 个字段、1 个方法）449 KB（`.obj` 233 KB vs 180 KB）；反射一条 4 个用户类互相引用的链（共约 14 个方法）530 KB（`.obj` 369 KB）。即首次 `class_of` 带来约 200 KB（注册表、描述表等固定部分），之后每多一个类约 +25～30 KB。用户类的闭包增长目前不可怕。
> - **实测时发现并已修复：含 `std::string` 成员的结构体编不过。** 原因有两个，都不是 `std::string` 本身的问题：
>   1. **匿名联合成员**（`basic_string` 里的 `_M_local_buf` 就在一个匿名联合里；用户写 `union { int a; float b; };` 一样复现）。`make_field_ref<M>` 里的 lambda 对联合的第一个成员会被 GCC 在同一个翻译单元里生成两份，汇编阶段报「符号重复定义」。两份来自同一个实例化（`public` 与 `declared` 两张表的反射值相等，实测同一特化），只有这个 lambda、只有联合首成员出现，普通类和 `make_field_get/set` 的 lambda 都没事；脱离模块的同样代码也没事，所以是 GCC 模块里 lambda 的问题。修复：`make_field_ref` 的非静态分支改用函数模板 `field_ref_thunk<C, M>`，不再用 lambda。
>   2. **`std::allocator<char>` 的成员**（`basic_string` 的基类 `_Alloc_hider` 带着它）。invoker 要取成员函数地址，而 `allocate` / `deallocate` / `allocate_at_least` 在 `import std` 下链接不到（`extern template class std::allocator<char>` 承诺了定义，libstdc++ 没提供；不用 speculo、单独取 `&std::allocator<char>::allocate` 就能复现，是工具链问题）。修复：`is_reflectable_method` 不再枚举 `std::allocator` 特化的成员（`is_std_allocator_specialization`）。试过把 invoker 改成直接调用（splice），`allocate` / `deallocate` 能链接，但 `allocate_at_least` 仍然没有定义，而且 private 成员不能 splice，所以放弃，保持取指针的写法。
>   - 现在 `class_of<std::string>()`、含 `std::string` / `std::vector<std::string>` 成员的结构体、签名里带 `const std::string&` 的方法都能物化并调用。测试：`StdLibraryTest.*`（3 个，含匿名联合）。
>   - **`std::string` 闭包的体积实测**（同上条件：GCC 16.2，Release，`-fno-rtti -fno-exceptions`；`strip` 后为纯代码体积）：

>     | 程序 | exe | strip 后 | `.obj` | 编译耗时 |
>     |---|---|---|---|---|
>     | 只链接库 | 253 KB | 159 KB | 181 KB | — |
>     | 1 个简单类（`int`、`double`、1 个方法） | 450 KB | — | 233 KB | 约 3 s |
>     | 加 1 个 `std::string` 成员 | 2.5 MB | 626 KB | 3.4 MB | 约 34 s |
>     | 再加 `vector<string>`、`vector<int>` 成员和一个取 `const string&` 的方法 | 5.0 MB | 1044 KB | 7.2 MB | 约 52 s |
>
>     结论：一个 `std::string` 成员大约让 exe 多 2 MB（strip 后约 470 KB）、编译多约 30 s，再加两个 `vector` 又多一倍。耗时是因为 `basic_string` 的全部成员都要物化。这是「签名闭包」的真实代价。
> - **现已改为浅物化（用户后来决定要做）。** 声明在 `std` 里的类 / 联合 / 枚举不再枚举字段、方法、构造函数、基类、枚举项；身份、布局和值操作（拷贝 / 析构 / 赋值）保留，所以 `Object` 仍能装箱一个 `std::string`。用户类型持有的 `std::` 成员、以及签名用到 `std::` 的函数，仍然生成 thunk，可以 `get` / `set` / `get_ref` / `invoke`。不能再 `class_of<std::string>().get_method("size")`。测试：`StdLibraryTest.StructWithStringMembersIsReflectable`、`StdTypesKeepIdentityWithoutMembers`。上面的体积数字是浅物化之前的测量。
>   - 没做：匿名联合在外层类里是一个无名字段（名字是编译器的显示串 `...::<anonymous union>`），其成员只能通过那个联合类型访问，没有做「展平」。

描述表是编译期常量，这一点成立。调用路径不是：

- `Method::invoke` 和 `Constructor::new_instance` 每次把实参指针收进 `std::vector`，即使参数个数 ≤ 8、`Args` 已经在栈上。
- 每个被反射的类会为所有可反射方法生成 invoker，并递归物化返回类型、形参类型、字段类型、基类。成员里出现 `std::string` 就会把 `basic_string` 的整套方法编进来。编译时间和二进制体积随「签名闭包」增长，而不是随用户实际调用的方法增长。
- 名字查找对每个候选的每个形参各做一次 `for_name`（各取一次共享锁）。

`design.md` §10 把热路径描述成「一次类型检查 + 一次函数指针调用」。类型检查之后还有一次堆分配。

建议：参数指针用与 `Args` 相同的 8 槽栈缓冲。物化可以先只生成元数据，invoker 改成按需实例化（第一次 `invoke` 或显式 `get_method<签名>` 时）。这会动到 bundle 的组织方式，但是现在「反射一个含 `std::string` 的结构体」的成本对使用方不透明。

### 5.4 工程

- 仓库没有 `README`。构建命令只写在 `AGENTS.md` 和 `phases.md`。`phases.md` 把 README 标成可选；作为要给人用的库，快速开始和「实参必须是左值、匹配是精确类型」这两条应该放在用户看得到的地方。
- 其它文档大量指向本文件的「Phase 0–7」偏差记录。本次审核开始时本文件是空的，那些阶段记录不在仓库里。阶段摘要还在 `phases.md`。本文件从这次起改为问题清单，不再补写一份阶段日志。
- `version_string()` 固定返回 `"0.1.0"`，没有和标签或变更记录对应。`phases.md` 已建议删掉或做成真版本。
- CMake 的测试源和示例源用 `file(GLOB)` 且没有 `CONFIGURE_DEPENDS`。新增文件不重新 configure 就不会编进去。`phases.md` 写过，仍然容易漏。
- MinGW 链接使用 `-Wl,--allow-multiple-definition` 掩盖 libstdc++ `import std` 的重复符号（GCC PR116746）。这是工具链限制，但也会放过本库自己的 ODR 违例。
- `src/object.cppm` 约 3200 行，API、thunk、物化全在一个分区。模块依赖环迫使如此（`phases.md` 的雷区 1）。文件内部至少应按「句柄 / thunk / bundle」分成清晰的区段边界；现在改一个匹配规则要在 3000 行里找所有 decay 比较点（`Field::set`、`Method::invoke`、`Constructor::validate`、`matches_arguments`、`Array::set` 各写了一次）。

---

## 6. 测试没有覆盖、但实现已经分叉的路径

现有用例覆盖了单继承、多继承偏移、单虚基、非虚菱形、const 方法、位域、引用成员的 get/set、枚举位模式回环、静态多维数组和动态数组的快乐路径。下面这些和本文的 P0/P1 直接对应，目前没有断言：

| 缺失用例 | 对应问题 |
|---|---|
| ✅ 虚继承菱形 `D : virtual A, virtual A` 的 `cast_to` | 3.1 |
| ✅ `cast_to` / `Field::get` 传入声明类之外的对象 | 2.1 |
| ✅ 同时存在拷贝与移动构造时 `new_instance` 是否移走源对象；删除其中一个后另一个是否仍可用 | 2.2 |
| ✅ `void f(int)` 与 `void f(int) const` 同时存在 | 3.3 |
| ✅ const 对象上的 `mutable` 字段 | 3.2 |
| ✅ `std::unique_ptr` 成员、按值参数、引用返回 | 2.3、3.5 |
| `const int[N]` 的 `Array::get` / `get_ref` / `as` | 3.7 |
| `alignas(64)` 元素的 `Array::new_instance` 地址对齐 | 3.8 |
| 非常量句柄上的 `as<const T>()` | 3.6 |
| ✅ private 基的 `is_assignable_from` 与 `cast_to` 是否一致 | 3.4 |

---

## 7. 建议的处理顺序

1. 物化退让（2.3）：让含 `unique_ptr` 的普通类能 `class_of`。这是使用面的门槛。
2. 运行期匹配键带上引用类别，`new_instance` 不再从左值移动，删除的候选不再挡住后面的构造（2.2）。
3. `cast_to` / 字段 / 调用校验接收者类型（2.1）；虚菱形按地址去重（3.1）。
4. `mutable`、引用成员 `get_ref`、数组元素 cv、`ObjectRef::as<const T>()`（3.2、3.5、3.6、3.7）。
5. 动态数组的对齐、溢出和描述符补全（3.8、4.3）。
6. `Args` 接受纯右值（4.1）。这一条对示例和文档的影响最大，适合和 README 一起做。

已知且应继续保持的限制（不是这次新发现的缺陷，避免以后当成漏实现去「修」掉）：

- 只支持 GCC 16+，无 RTTI、无异常。
- `get_fields` / `get_methods` 不合并继承链。
- 向下转换、交叉转换不做。
- 私有/保护构造在命名空间作用域的 thunk 里没有访问权，不能 `new_instance`（私有方法和私有字段可以，这是不对称，但是访问模型决定的，除非改用类内 friend thunk）。
- `&&` 限定的成员函数没有值类别可用，`invoke` 保持不支持，直到 `ObjectRef` 能表达值类别。
- 别名类型反射到底层类型，`using` 信息丢失。
