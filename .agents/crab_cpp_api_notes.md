# crab_cpp 依赖：API 偏差与升级适配清单

> speculo 依赖 `crab_cpp`。**2026-11-04 已从 v0.4.0 升到 v0.4.1**（`v0.4.1` tag，commit `dadea50`，见其 `CHANGELOG.md`）。
> 本文记录：v0.3.0 时针对 `Option`/`Result` 记录的偏差、v0.4.0 / v0.4.1 的解决情况、为适配所做的改动，以及仍然遗留的约束。
> crab_cpp 源码在 `D:/programming/C++/crab-cpp`。

## 0. 依赖 pin

- `CMakeLists.txt`：`FetchContent_Declare(crab_cpp ... GIT_TAG v0.4.1)`（原 v0.4.0）。
- `xmake.lua`：`package("crab_cpp")` 的 `add_versions("0.4.1", "9eedf011d82932b22c3e8eb32b8c6a4f81ab5a4c9e772b6d3c1c4eadabd5522a")`
  + `add_versions("git:0.4.1", "v0.4.1")`（原 0.4.0）。
- 本地开发可用 `-DFETCHCONTENT_SOURCE_DIR_CRAB_CPP=D:/programming/C++/crab-cpp`；xmake 走 package 源
  （包缓存里换版本后会自动重新安装）。仓库默认不再是本地路径。

## 0.1 v0.4.1：两个「深物化就会硬错误」的表示问题 —— ✅ 已解决

speculo 以前对 `crab_cpp` 也做浅物化，就是为了绕开下面两处；0.4.1 修好后，`crab_cpp` 与其它三方库一样默认深物化。

1. **`Option<move-only T>` / `Result<move-only T, E>` 的默认拷贝赋值**：`operator=(const&) = default` 的隐式定义
   因载荷只能移动而被定义成 deleted，而 GCC 16.2 的 `std::meta::is_deleted` 会**实例化那份定义**才回答，
   于是硬报错。0.4.1 把拷贝赋值按 `std::is_copy_assignable_v<存储>` 拆成 `= default` / `= delete` 两个受约束重载。
   speculo 侧仍保留 `assignment_is_available` 守卫：用户自己的「move-only 载荷 + `= default` 拷贝赋值」还需要它。
2. **`Result<const T&, const E&>`（`as_ref()` 的返回类型）**：`result_storage` 里的 `value_repr` 持有
   `std::variant<T, E>`，引用载荷下这个 variant 不合法（引用不是 object type）；而反射判断嵌套类型
   「是否完整」时必须实例化它。0.4.1 把存储拆成 `result_repr<false, T, E>`（variant）与
   `result_repr<true, T, E>`（指针 + optional），引用载荷不再碰到 variant。

验证：`class_of<crab_cpp::Option<Object>>()` / `Result<Object, Error>` / `Result<const Object&, const Error&>`
都有成员表（测试见 `tests/std_library_test.cpp` 的 `StdLibraryTest.ThirdPartyTypesAreDeep`）。

## 1. `unwrap` / `expect` / `unwrap_err` 的值类别 —— ✅ 0.4.0 已解决

- v0.3.0：`unwrap(this Self&&) -> auto&&` 体内 `std::get<0>(self)` 用的是具名左值，右值也不 move，必须
  `std::move(res.unwrap())`。
- **v0.4.0**：`CHANGELOG` “Value category is the ownership rule for methods that take `this`”。
  `unwrap`/`expect`/`unwrap_unchecked` **按值返回**：lvalue 拷贝、非 const rvalue move-out（`Option` 变 `None`、
  `Result` 留 moved-from）、const rvalue 拷贝；引用替代返回该引用。
- **speculo 适配**：凡是“具名（lvalue）的 `Outcome<Object>`/`Option<Object>`”上调 `unwrap()` 取 move-only 值的地方，
  改为 `std::move(x).unwrap()`。已改动：
  - `examples/02_construct_and_call.cpp`：`clone()` 结果先 `std::move(cloned).unwrap()` 取出再打印（同时去掉误用的 gtest `ASSERT_TRUE`）。
  - `tests/object_test.cpp`：`std::move(made/cloned).unwrap()`；`ObjectTest.MakeAndTake` 的析构计数改用
    `taken.as_ref().unwrap().value` 读取（避免 lvalue `unwrap()` 多拷一份 `Counter`）。
  - `tests/field_test.cpp` / `tests/method_test.cpp`：`read` / `bumped` / `invoked` / `result`（两处 `Method::invoke`）改为 `std::move(...).unwrap()`。
  - `tests/args_test.cpp` / `tests/constructor_test.cpp` / `tests/method_test.cpp`：`scaled` / `made` / `result` 改 `std::move(...).unwrap()`。
  - `tests/enumerator_test.cpp` / `tests/array_test.cpp`：`by_name` / `by_value` / `recovered` / `boxed` / 两个 `Array::new_instance` 的 `made` /
    `empty` 改 `std::move(...).unwrap()`。
- `api_design.md §0` 注释由 `// owning result (unwrap() is an lvalue)` 改为
  `// rvalue unwrap() moves the Object out`（示例去掉多余 `std::move`）。
- 说明：`src/`（库本体）不受影响——库内 `.unwrap()` 都是可拷贝句柄或**引用载荷**（`Option<const Class&>`）。

## 2. `Option::take()` 语义 —— ✅ 0.4.0 已移除

- v0.3.0：`Option::take() -> T` 且 None 时 panic（= Rust `unwrap` + 清空），与 Rust 的
  `take(&mut self) -> Option<T>` 冲突；`Result` 无 `take`。
- **v0.4.0**：删除 `Option` 的 `take` / `take_if` / `take_or_default` / `take_or_else` / `expect_take`，
  以及 `Result::replace`。`std::move(opt).unwrap()` 取代 `take()`；`std::move(opt).expect(msg)` 取代 `expect_take`。
- **speculo 适配**：源码未使用这些 API（用到的 `take` 是 speculo 自己的 `Object::take<T>() -> Outcome<T>`，无关）。
  仅 `review.md` 的旧注释提到 `res.ok().take()` 需去掉该写法（见 §5）。

## 3. variant 实现 + move-only 载荷 —— ⚠️ 部分缓解，注解保留

- v0.3.0：`Option<move-only T>` 的某些成员函数（如 `or_` 体内 `return *this;`）对该 T 本身 ill-formed，
  被反射物化实例化即硬错误。
- **v0.4.0**：组合子多改为转发引用模板；但**直接物化 `Class` / crab 变体仍不可行**。实测（临时探针）：
  `class_of<speculo::Class>()`、`class_of<crab_cpp::Option<Object>>()`、`class_of<crab_cpp::Result<Object,Error>>()`
  仍会在 `result.cppm:143` / `option.cppm:142/355` / `object.cppm:2307`
  （invoker 里 `new R(lvalue)` 对返回 `Option<Object>&` 之类的方法拷贝 move-only）处硬错误。
- **v0.4.1**：上述三处现在都能物化了——`class_of<speculo::Class>()` / `ObjectRef` / `Annotation` 有成员表，
  `Option<Object>` / `Result<Object, Error>` / `Result<const Object&, const Error&>` 同样有（见 §0.1）。
  speculo 也不再对 `crab_cpp` 做浅物化。
- **结论**：`[[= detail::reflect_impl_tag{}]]` 注解（`src/object.cppm` + `include/speculo/macros.h`）
  **继续保留且必要**——它把 `SPECULO_IMPL` 注入的 `get_class()` 排除出成员表，避免把 `Class` 的签名闭包拖进来。
  语义上也成立：桥接方法不应出现在 `get_methods()`。

## 4. 升级验证状态（v0.4.1）

- ✅ **CMake**：删掉本地源覆盖后重新 configure，`FetchContent` 拉到 `v0.4.1`（commit `dadea50`）；
  `cmake --build build` 通过，`ctest` 7/7、`speculo_test` 413 用例全绿。
- ✅ **xmake**：包缓存里装出 `crab_cpp/0.4.1`；`xmake` 通过（228s），`xmake test` 7/7。

## 5. 其它（0.4.0 带来的可用改进）

- `Result::as_ref() -> Result<const T&, const E&>` / `as_mut() -> Result<T&, E&>`、`Option::as_ref/as_mut`
  （lvalue-only）：可在不拷贝的情况下读取载荷——本仓 `ObjectTest.MakeAndTake` 已改用 `as_ref().unwrap()`。
- 引用替代（`Result<T&, E>` 等以指针存储）：`Option<const Class&>` 等引用载荷继续可用（库内大量使用）。
- `Result::ok()`/`err()` 现在遵循值类别（lvalue 拷贝、不再总是 move-out）。
- `review.md`「Phase 3」里 `Object r = std::move(res.unwrap());`（或 `res.ok().take()`）的注释应更新为
  `Object r = res.unwrap();`（rvalue 直接 move），`take()` 写法已不存在。
