# AGENTS.md

## 项目简介

**speculo** 是一个基于 C++26 静态反射（P2996, `std::meta`）实现的**动态（运行期）反射库**。

- API 词汇参考 Java 的 `java.lang.reflect`（`Class` / `Field` / `Method` / `Constructor` / `Modifier`），
  但书写遵循 C++ 惯例：类名大驼峰，方法与自由函数蛇形命名（如 `get_name` / `get_declared_fields` / `new_instance`）。
  同时提供 Java 没有的 C++ 特有信息：字段大小、字节/位偏移、对齐、位域、cv 限定、引用限定、noexcept 等。
- 整个库以 **C++ 模块** 形式发布：`export module speculo;`，全部代码位于命名空间 `speculo`。
- 依赖 [crab_cpp](https://github.com/ymyh/crab-cpp)（同为 C++ 模块，`import crab_cpp;`），
  主要使用其中的 `Option<T>` / `Result<T, E>` 做查找与错误处理，**不使用异常、不使用 `std::optional/expected`**。

## 工具链要求

- **编译器：GCC >= 16**（目前唯一实现了 C++26 静态反射的编译器），编译需开启 `-freflection`、`-std=c++26`。
- 构建系统：同时兼容 **CMake**（>= 3.31，Ninja 生成器，需开启 `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD`）与 **xmake**。
- 标准库以模块方式使用：`import std;`。

## 文档索引（动手前必读）

- 总体设计：@.agents/design.md
- API 详细设计：@.agents/api_design.md
- 开发计划：@.agents/phases.md
- 代码风格：@.agents/codeStyle.md
- 测试策略：@.agents/testing.md（测试文件清单见 `tests/README.md`）
- 审核反馈：@.agents/review.md
- crab_cpp 依赖 API 偏差与升级适配：@.agents/crab_cpp_api_notes.md

## 基本规则

- 一切公开 API 位于命名空间 `speculo`，对外只暴露 `speculo` 模块（内部实现放实现分区/实现单元）。
- 错误处理统一使用 `crab_cpp::Option` / `crab_cpp::Result`（以及 speculo 封装的 `speculo::Error`），禁止抛出异常。
- **全项目禁用 RTTI 与异常**：编译期加 `-fno-rtti -fno-exceptions`，源码中禁止 `typeid` / `dynamic_cast` /
  `std::type_info` / `throw` / `try` / `catch`（`libspeculo.a` 中不得出现 `__cxa` / `type_info` 符号）。
  类型信息一律来自静态反射物化的描述表，错误一律走 `crab_cpp` 的 `Option` / `Result`。类型关系判定
  （`is_assignable_from` / `is_instance` / `cast_to`）全部基于描述层的静态信息，不依赖运行期类型识别。
- 宏单独放普通头文件 `include/speculo/macros.h`（`SPECULO_IMPL` / `SPECULO_IMPL_VIRTUAL` / `SPECULO_REGISTER`），不进模块。
- 静态反射代码全部是 `consteval`，运行期描述表只生成一次且只读；新增特性时遵循 @.agents/design.md 中的分层架构。
- 严格遵循 @.agents/codeStyle.md；拿不准时与周围已有代码保持一致。
- 改动保持最小化，不要顺手重排无关代码。

## 本机开发环境与构建命令

- 编译器：MSYS2 ucrt64 GCC 16.2.0（`D:\msys64\ucrt64\bin\g++.exe`）；编译需 `-std=c++26 -freflection`。
- 工具：CMake 4.4.2、Ninja 1.12.1、xmake 3.1.1。
- CMake：
  ```
  cmake -B build -G Ninja && cmake --build build
  ctest --test-dir build --output-on-failure
  ```
  离线可用 `-DFETCHCONTENT_SOURCE_DIR_CRAB_CPP=D:/programming/C++/crab-cpp` 指向本地 crab_cpp。
- xmake：Windows 上默认会选 MSVC，speculo 需要 GCC，必须先配置 mingw 工具链：
  ```
  xmake f -p mingw --mingw=D:\msys64\ucrt64 -a x86_64
  xmake && xmake test
  ```
- **双构建系统对等**：新增模块分区时，必须同时更新 `CMakeLists.txt` 的 `CXX_MODULES` 文件集与 `xmake.lua` 的 target 源文件。
- Spike 实验（Phase 1 遗留验证程序）：见 `spikes/`，用
  `pwsh -File spikes/run.ps1 <file>.cpp` 单独编译运行。
- 环境相关偏差（xmake 工具链、crab_cpp 取源、`-freflection` 需 `{force=true}`、
  MinGW `stdc++exp` 链接顺序）见 @.agents/review.md「Phase 0」。
