# Code Style

This document describes the code style used across `speculo`. The existing
code in `src/`, `tests/`, and `include/` is the source of truth; when in doubt,
match the file you are editing. All rules below are mandatory for new code.

Note: declarations in @.agents/api_design.md use the classic `Ret name(args)`
form for readability; implementation code follows the trailing-return-type
rule below.

## Language and layout basics

- Target **C++26** only. Use modules; do not add classic headers except the
  global module fragment (`module;` + `#include`) and `include/speculo/*.h`
  for macros.
- Indent with **4 spaces**. Never use tab characters for indentation.
- End files with a single trailing newline. Avoid trailing whitespace.
- Keep one blank line between logical blocks (e.g. before a `return` that
  follows a multi-line `if` body).
- Wrap code at a reasonable width; follow the existing files' line breaks
  rather than hard-wrapping at an arbitrary column.

## Braces (Microsoft style / Allman)

- Opening braces go on their **own line**, for namespaces, types, functions,
  control statements, and lambdas with statement bodies.

```cpp
if (cls.is_enum())
{
    return cls.get_enumerators();
}

return {};
```

- **Every control statement must have braces**, even when the body is a single
  statement. This is forbidden:

```cpp
// Bad
if (x > 0) return;

// Bad
if (x > 0)
    return;

// Good
if (x > 0)
{
    return;
}
```

- `else` goes on its own line after the closing brace:

```cpp
if (cond)
{
    // ...
}
else
{
    // ...
}
```

- Short single-line functions and one-line lambdas are acceptable when the body
  is a single expression, matching the existing style:

```cpp
auto is_null() const noexcept -> bool { return m_data == nullptr; }

auto names = fields | std::views::transform([](const Field& f) { return f.get_name(); });
```

## Whitespace and operators

- Put a space after control keywords: `if (`, `while (`, `for (`, `switch (`.
- Put a space around binary operators and after commas.
- Do **not** put a space between a function name and its argument list:
  `foo(x)`, not `foo (x)`.
- Prefer the trailing-comma, multi-line style already used for argument lists
  and templates.

## Pointers and references

- The `&` and `*` must be **attached to the type**, not the variable name.
  This applies to parameters, return types, locals, and casts.

```cpp
// Bad
void f(int &x);
void f(int *p);
int &ref = ...;

// Good
void f(int& x);
void f(int* p);
int& ref = ...;
```

- `const`/`volatile` qualifiers attach to the type as usual:
  `const std::byte* data`, `const T& x`.

## Naming

- Types (struct/class/enum/alias): `PascalCase` — `Class`, `Field`, `Method`,
  `ObjectRef`, `ErrorKind`.
- Functions and variables: `snake_case` — `get_name`, `get_declared_fields`,
  `new_instance`, `is_bitfield`.
- Private data members: `m_` prefix — `m_data`, `m_desc`, `m_size`.
- Template parameters: `PascalCase` for types (`T`, `Args`, `Self`, `F`).
- Namespaces: `snake_case` — `speculo`, `detail`.
- Macros: `SCREAMING_SNAKE_CASE` with the `SPECULO_` prefix where applicable —
  `SPECULO_IMPL`, `SPECULO_IMPL_VIRTUAL`, `SPECULO_REGISTER`, `SPECULO_DEEP`.

### API vocabulary and shape (Java words, C++ spelling)

- **Types use PascalCase** (Java style): `Class` / `Field` / `Method` /
  `ObjectRef` / `Constructor`.
- **Methods and free functions use snake_case**: `get_name` /
  `get_declared_fields` / `new_instance`. The vocabulary is taken directly from
  `java.lang.reflect`, but the spelling follows the C++ underscore convention.
- All public API lives in namespace `speculo` and is consumed via
  `import speculo;`. Free-function entry points are `snake_case`
  (`class_of<T>()`, `function_of<f>()`, `overload_of<...>(f)`).
- `Option` / `Result` mean `crab_cpp::Option` / `crab_cpp::Result`, re-exported
  as aliases by the `speculo` module (`Status` = `Result<std::monostate, Error>`,
  `Outcome<T>` = `Result<T, Error>`).
- Handles (`Class`, `Field`, `Method`, ...) are cheap copyable values pointing
  into constexpr tables; they never own anything and never throw.

## Functions and declarations

- Use **trailing return types**: `auto name(args) -> Ret`.

```cpp
[[nodiscard]] auto is_null() const noexcept -> bool
{
    return m_data == nullptr;
}
```

- Apply attributes consistently with the existing code:
    - `[[nodiscard]]` on queries and factory functions.
    - `noexcept` when the function cannot throw.
    - `constexpr` where the operation is compile-time capable.
    - `explicit` on single-argument constructors.
- Mark overridden operators and comparisons with `= default` where possible.
- Inside templates, use `this->` to access dependent members.
- Prefer `using` type aliases over `typedef`.

## Modules and includes

- Each module starts with its module declaration, then imports:

```cpp
export module speculo:field;

import std;
import crab_cpp;
import :error;
import :type;
```

- If a module ever needs a C header, use a global module fragment:

```cpp
module;

#include <some_c_header.h>

export module speculo:sub;

import std;
```

- The umbrella module re-exports all partitions:

```cpp
export module speculo;

export import :error;
export import :modifier;
export import :descriptor;
export import :object;
export import :function;
export import :registry;
```

- Module partition names are single lowercase words (`error`, `modifier`,
  `descriptor`, `object`, `function`, `registry`; `compile` is planned for the
  compile-time half, see @.agents/design.md §14), matching @.agents/design.md §4
  and the actual partition layout (see @.agents/review.md "Phase 3" for why
  `:type` / `:field` / `:method` converge into `:object`).
- Put public API in `export namespace speculo { ... }`. Internal implementation
  details go in namespace `speculo::detail` (never exported from the umbrella
  module), matching @.agents/design.md §3.

## Static reflection code

- `consteval` functions, `std::meta` calls, and splice syntax (`[: r :]`) are
  confined to `src/detail/` (the meta and thunk layers, see @.agents/design.md
  §3). Higher layers must never see `std::meta::info`.
- Materialized descriptor tables are `constexpr` and live in static storage;
  runtime handles (`Class`, `Field`, ...) only hold pointers into them.
- Do not sprinkle reflection intrinsics across API-layer code; if a new
  reflection capability is needed, add it to the meta layer first.

## Templates and concepts

- Declare template parameters with `template<typename ...>`.
- Use `requires` clauses and requires-expressions for constraints, matching the
  existing style. Put the `requires` clause on its own indented line below the
  template header:

```cpp
template<typename... Ts>
    requires (std::convertible_to<Ts&, ObjectRef> && ...)
Args(Ts&... objs)
{
    // ...
}
```

## Comments

- Use Doxygen-style `@brief` comments for public types and functions, including
  `@tparam`, `@param`, `@return`, and `@note`/`@warning` where relevant.

```cpp
/**
 * @brief Reads the field value from the given object
 * @param obj The object to read from (non-owning view)
 * @return The field value, boxed as an owning Object
 * @note Returns ErrorKind::NullInstance if obj is null
 */
```

- Use `//` for short inline or section comments.

## Error handling

- No exceptions for error reporting: lookups return `Option<T>`, operations
  return `Result<T, Error>` / `Status` (see @.agents/design.md §6).
- `crab_cpp::panic` is reserved for programmer errors only (violated
  preconditions, e.g. calling `Object::type()` on an empty object), never for
  regular error paths.

## Tests

- Tests live in `tests/*.cpp`, use GoogleTest, and `import speculo;`.
- Name test cases `TEST(SuiteName, CaseName)` with `PascalCase`, e.g.
  `TEST(FieldTest, GetSetIntMember)`. Matrix-style cases use `TEST_F` with a
  fixture; shared fixture types live in `tests/support/*.h`.
- Use `EXPECT_*` assertions; `EXPECT_DEATH` only for panic paths (see the
  error-handling rules above).
- Assert on `ErrorKind`, not on `Error::message` text (only
  `error_message_test.cpp` owns the text).
- Every API with more than one parameter keeps its **value-class matrix table**
  at the top of the test file (format in @.agents/testing.md §6); the table and
  the cases must stay in sync.
- Strategy, coverage model and the v2-specific techniques are in
  @.agents/testing.md; the file-to-API map is in `tests/README.md`.
