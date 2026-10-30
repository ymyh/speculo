# speculo 测试策略

> 分工：本文管**怎么测、测到什么程度算够、不许怎么测**；@.agents/codeStyle.md「Tests」管命名与写法；
> `tests/README.md` 管**文件清单**（哪个文件覆盖哪个 API）。三者不重复。
>
> 本文件的核心要求：**每个 API 的每个参数，每个取值类都要有一条断言；返回值是
> `Option` / `Result` / `Status` 的，两个分支都要有断言；多参数按组合覆盖（§3）。**

## 1. 四个层次，不要混用

| 层 | 位置 | 回答什么问题 | 何时写 |
|---|---|---|---|
| **spike** | `spikes/*.cpp` | GCC 16.2 的 `<meta>` 到底支持什么 | 用任何新语言能力**之前**（Phase 8 全部） |
| **单元** | `tests/*_test.cpp` | 这个 API 的行为是否如文档 | 每个公开 API |
| **交叉验证** | `tests/*_test.cpp` 内 | 库的结论是否与编译器/另一条路径一致 | 新增谓词、编译期遍历、身份判定 |
| **示例** | `examples/*.cpp` | 端到端能不能用（同时进 ctest） | 每个使用场景 |

spike 不是测试：它只要求「编得过、跑出结论」，结论写进 `review.md`，之后由 §9 的
「结论回归」把它固化成断言。**spike 通过 ≠ 功能有测试**。

## 2. 参数维度：先给每个参数划分值类

不能只测「一个正常值」。每个参数（**包含接收者 `self` / `obj`**）都按下表划分值类，
每一类至少要有一条用例落在某次调用里：

| 值类 | 含义 | 例子 |
|---|---|---|
| **V0 典型** | 最常见的合法值 | `int` 的 `42`、`Point` 实例、`"x"` 字段名 |
| **V1 边界** | 合法但踩在边界上 | `0` / `-1` / `INT_MAX`、空串、空 `Args`、下标 `0` / `N-1` / `N` / `N+1`、`>8` 个实参（`Args` 堆降级） |
| **V2 类型不符** | 类型不匹配的所有形态 | 错误类型、需要提升的数值（`short` 传给 `int`）、未 `cast_to` 的派生对象、cv 不符、引用绑定不符（`const` 实参传 `T&`） |
| **V3 空** | 空句柄 / 空 `ObjectRef` / 空 `Args` | `Class{}`、`ObjectRef{}`、`Args{}` |
| **V4 视图修饰** | 同一对象的不同视图 | `cref()` 产生的 const 视图、volatile 视图 |
| **V5 能力缺失** | 值合法但该操作不支持 | 不可拷贝类型、位域取引用、`= delete`、抽象类、缺注解的默认实参、未注册类型 |
| **N/A** | 该值类在此参数上无意义 | 参数是 `const Class&` 时「V3 空」只能 panic、不会返回 `Err` |

规则：

- **V0 与 V1 各至少一条**。只测典型值等于没测边界；只测边界值会掩盖正常路径。
- **V2 要按「为什么不符」拆开**，不是随便塞一个别的类型：数值提升、基类指针、cv、
  引用绑定是四种不同的拒绝理由，实现里也是四条分支。
- **N/A 必须写明理由**，写在矩阵表里（§6）。省略不等于 N/A。
- 参数是**句柄**（`Field` / `Method` / `Class`）时，V3 是「默认构造的空句柄」，
  其期望行为是 `panic`（`EXPECT_DEATH`），不是 `Err`。

## 3. 组合策略：笛卡尔积，超阈值降级为两两覆盖

设参数 `i` 的值类数为 `k_i`，格数 `P = ∏ k_i`。

| 条件 | 要求 |
|---|---|
| **P ≤ 64** | **全笛卡尔积**：每一格都要有断言。三个参数、每参 4 类（64 格）正好在上限 |
| **P > 64** | ① 单因素全覆盖：每个参数的每个值类至少在一条用例中出现；② **pairwise（2-way）**：任意两个参数的每个值对至少出现一次；③ 加两条对角线：全合法、全非法 |
| 任意 | 「多个参数同时非法」的格子必须有，且期望值由 §5 的错误优先级唯一确定 |

实现建议（都不需要手写几十个 `TEST`）：

- 参数化：`TEST_P` + `INSTANTIATE_TEST_SUITE_P`，参数是 `std::tuple<值类…>`，
  一条 `TEST_P` 里 `switch` 期望 `kind`。
- 表驱动：`tests/function_test.cpp` 的 `IntBindingMatrix` / `NumericExactMatchMatrix` /
  `PointerBindingMatrix` 是现成样板（fixture + 表 + 循环 + `SCOPED_TRACE`）。
- 无论哪种，**矩阵表要留在测试文件顶部**（§6），代码可读性靠它，不靠断言堆。

组合覆盖的是**值类**，不是具体值。不要用「10 个不同的 `int` 数值」冒充覆盖，
那只是重复 V0。

## 4. 返回值维度：每个分支都要有断言

| 返回类型 | 必须覆盖 |
|---|---|
| `Option<T>` | `Some` 至少 1 条；**每一条产生 `None` 的路径各 1 条**（名字不存在 / 类型不符 / 下标越界 / 非目标 kind） |
| `Result<T, Error>` | `Ok` 至少 1 条；**文档承诺的每一个 `ErrorKind` 各 1 条** |
| `Status` | 成功（`None`）至少 1 条；每个 `Err(kind)` 各 1 条 |
| `bool` 查询 | `true` 与 `false` 各 1 条（难构造的 false 写注释说明为什么做不到） |

补充规则：

- **一个 `Err` 不代表测完**。`EXPECT_TRUE(res.is_err())` 等于没测错误语义——
  必须断 `kind`（§8）。
- `Ok` 也有子维度，按文档逐个覆盖：返回值 `void` → 空 `Object`；`T&` → `invoke` 拷贝 /
  `invoke_ref` 给视图 / 不可拷贝 → `NotSupported`；空模板包 → 空数组。
- `None` 与「空句柄 panic」是两件事：前者走 `Option`，后者 `EXPECT_DEATH`，不要混。

## 5. 错误优先级：矩阵格子的期望值从哪来

笛卡尔积里必然出现「两个参数同时非法」的格子。**返回哪个 `ErrorKind` 由检查顺序决定**，
所以顺序必须是文档里的契约，不能靠实现巧合。

**v1 基线（实现为准，新 API 照此风格显式声明）**：

| API | 检查顺序 |
|---|---|
| `Method::invoke` | 实参个数 → 接收者空 → 接收者类型 → 非 const 方法遇 const 接收者 → 非 volatile 方法遇 volatile 接收者 → 逐参左到右，首个不符即返回 |
| `Constructor::validate` | 实参个数 → 逐参左到右 |
| `Field::get` | 接收者空 → 字段不可按值读 → 接收者类型 → volatile 接收者 |
| `Field::set` | 接收者空 → const 接收者（`mutable`/引用成员除外）→ 接收者类型 → volatile 接收者 → 字段不可写（const → `ReadOnly`，不可拷贝 → `NotSupported`）→ 值为空 → 值类型 |

规则：

- **v2 的每个新 API 必须先在 @.agents/api_design.md 写出检查顺序，再写测试。**
  顺序未定义 = 矩阵格子没有期望值 = 阻塞项，不许「随便断一个 kind」蒙过去。
- 顺序一旦写进文档就是契约。改顺序要同时改文档与测试，否则是隐性 breaking change。
- 「先查接收者还是先查实参个数」这类问题，v1 的选择是**先个数**（`Method` / `Constructor`），
  `Field` 没有个数维度所以先接收者。v2 保持一致，别引入第三种风格。

## 6. 组织与命名

- 文件按**公开 API** 分，不按开发阶段分（`phaseN_test.cpp` 已被拆散是 v1 的教训）。
- 用例名 `TEST(Suite, Case)` / `TEST_F(Fixture, Case)`，PascalCase；`Suite` 用被测类型名
  （`FieldTest`、`MethodTest`），跨 API 的用场景名（`InheritanceTest`）。
- 矩阵型用例用 `TEST_F` + fixture，样板见 `function_test.cpp` 三张 Matrix。
- **每个有组合矩阵的 API，在测试文件顶部留一张表**：

```cpp
// Field::set(obj, value)   —— 参数值类见 .agents/testing.md §2，组合见 §3
// obj \ value  | 匹配 | 空 | 错类型 | 不可拷贝 | 数组
//  正常         | None | TypeMismatch | TypeMismatch | NotSupported | NotSupported
//  空           | NullInstance（其余四格同，§5 先查接收者）
//  const        | ReadOnly（同上）
//  volatile     | NotSupported（同上）
//  错类型       | TypeMismatch（同上）
```

- fixture 类型放 `tests/support/*.h`，在三个 `import` **之后** `#include`；类型放匿名命名空间。
  例外：`registry_fixtures.h` 用命名空间（测试要按名字查表）。
- 新文件要同时更新 `tests/README.md` 的表格。

## 7. 怎么跑

```powershell
# CMake（双系统之一）
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure

# 单个用例：ctest 只有 speculo_test 一个条目，按用例过滤要直接跑二进制
build\speculo_test.exe --gtest_filter=FieldTest.*

# xmake（另一个，必须也绿）
xmake f -p mingw --mingw=D:\msys64\ucrt64 -a x86_64
xmake && xmake test
xmake run speculo_test --gtest_filter=FieldTest.*
```

- **新增测试文件后 CMake 要重新 configure**（`file(GLOB_RECURSE)` 不是 `CONFIGURE_DEPENDS`），
  否则新文件静默不参与构建——这是本项目最容易误判「测试通过」的坑。
- 双系统对等：一个绿一个红算红。

## 8. 断言纪律

- **断 `kind`，不断消息文本**。`Error::message` 是诊断文本，不保证稳定；
  `error_message_test.cpp` 是唯一允许断文本的地方（那里断「消息里包含成员名/实参下标」）。
- 不写依赖实现的断言：描述符地址、内部表长度、`Args` 的堆/栈阈值之外的东西。
  成员声明顺序是语言保证，可以断。
- 不用 `EXPECT_DEATH` 测可恢复错误：`Result` / `Option` 的错误路径必须能被正常断言。
- 用例之间不得有顺序依赖。注册表是进程级状态：registry 测试用独立命名空间，断言写成幂等。
- 线程用例只断「没有崩溃 + 结果一致 + 计数正确」，不断调度顺序。
- 编译通过不算测试。除非它是 `static_assert`（那是一次真正的断言，见 §9）。
- 矩阵用例用 `SCOPED_TRACE` 标出当前格子（行列名），失败时能直接定位到哪一格。

## 9. v2 新增的测试手段

| 手段 | 用于 | 做法 |
|---|---|---|
| **矩阵生成** | 所有组合矩阵（§3） | `tools/gen_matrix.py` 按值类枚举全部格子并打印 C++ 初始化器；改错误优先级先改脚本，再重贴进测试 |
| **trait 交叉验证** | 布局/能力谓词（api_design §5） | 同一批类型上，`cls.is_trivially_copyable()` 必须等于 `std::is_trivially_copyable_v<T>`；覆盖空类、虚基、位域、`[[no_unique_address]]`、`std::string` 成员 |
| **结论回归** | Phase 8 spike 的全部结论 | 每个 S8.x 定下的行为写一条断言，防止 GCC 升级悄悄改语义 |
| **两路一致性** | 编译期半层（api_design §10） | `get_fields()` 与 `for_each_field` 的字段集合、顺序、名字必须完全一致 |
| **布局静态断言** | `ObjectRef` / `Object` / 句柄 | `static_assert(sizeof(ObjectRef) == 16)`；加值类别后不变；`Class` 保持 8 字节 |
| **分配计数** | 零分配与分配器（api_design §11–§12） | 计数 `Allocator`：`invoke_into` 断言 0 次分配，`invoke` 断言恰好 1 次 |
| **golden 快照** | `describe()`（api_design §16） | 固定 fixture 的输出全文比对；改格式必须同时改快照 |
| **负面用例** | 关系图、默认实参 | 未注册的派生类不可见；无注解形参调 `invoke_with_defaults` → `InvalidArgument` |
| **跨 DSO 身份** | `operator==` 第三条分支（api_design §13） | **不能**用单进程两份 TU 假装覆盖（链接器会合并弱符号）。真覆盖需要第二个共享库目标（可选，视 CI 平台）；不具备时该分支只由代码审查 + 文档保证，不许写「看起来通过了」的假测试 |

v2 的每个 Phase 在 `phases.md` 里都列了新增测试文件；它们的矩阵按 §2–§5 执行。

## 10. 什么不算测试

- 跑通了 `examples/` 不等于覆盖了 API。
- 打印出来用眼睛看，不等于断言。
- 「在我机器上编过了」不等于双系统绿。
- 只断 `is_err()` / `is_none()` 而不断 `kind` / 不区分路径，等于没测错误语义。
- 只测全合法组合，等于只测了 §3 矩阵的一条对角线。

## 11. 覆盖率口径

不以行覆盖率百分比为门禁：`consteval` 物化代码的行覆盖率天然失真（模板实例只走一遍）。
验收口径是**矩阵**，不是百分比：

1. 每个公开 API 一张「参数值类 × 期望结果」表（§2/§6）；
2. 表里每一格至少一条断言，N/A 格写明理由；
3. `Option` / `Result` / `Status` 的每个分支都有断言（§4）；
4. 多参数 API 满足 §3 的组合要求（≤64 全笛卡尔，否则 pairwise + 对角线）。

这四条由 code review 与 `review.md` 的 Phase 验收共同保证。
