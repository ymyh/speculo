# Tests

Test sources are grouped by the public API they exercise, not by the development
phase that introduced them. Add a test to the file that matches its feature; a new
feature normally gets a new `*_test.cpp` file.

| File | Covers |
| --- | --- |
| `module_smoke_test.cpp` | Module imports and the `crab_cpp` dependency |
| `error_test.cpp` | `ErrorKind` / `Error` / `Status` / `Outcome` |
| `error_message_test.cpp` | Diagnostic text (member, argument index, types) |
| `modifier_test.cpp` | `Modifier` bits, predicates and `to_string()` |
| `args_test.cpp` | `Args` storage, literal arguments, inline/heap boundary |
| `object_test.cpp` | `Object` / `ObjectRef` ownership, clone and destruction |
| `class_test.cpp` | `Class` identity, fields/methods views, base navigation |
| `field_test.cpp` | `Field` get/set/ref, layout, bit-fields, static/mutable members |
| `method_test.cpp` | `Method` lookup, overloads, qualifiers, `invoke` / `invoke_ref` |
| `declared_view_test.cpp` | `get_declared_*` view of private/protected members |
| `constructor_test.cpp` | `Constructor`, `new_instance`, `construct_at`, ownership |
| `const_correctness_test.cpp` | const receivers, const arguments and `ObjectRef` constness |
| `volatile_correctness_test.cpp` | volatile receivers, fields and arguments |
| `inheritance_test.cpp` | `cast_to`, `is_assignable_from`, `is_instance`, inherited members |
| `type_identity_test.cpp` | Type identity and pointer/cv/reference/array decomposition |
| `enumerator_test.cpp` | `Enumerator`, underlying types, name/value round-trips |
| `array_test.cpp` | Static arrays and dynamic `Array::new_instance` |
| `registry_test.cpp` | `Class::for_name`, `register_class`, macros, thread safety |
| `std_library_test.cpp` | Reflecting standard-library types and anonymous unions |
| `annotation_test.cpp` | C++26 annotations (`[[= expr]]`) on classes, fields, methods |
| `function_test.cpp` | Free functions: lookup, overloads, invocation, registry |
| `cross_tu_support.cpp` | Support TU for the cross-translation-unit function test |

## Shared fixtures

Fixture types live in `tests/support/` and are included after the module imports,
because their inline members use `std`, `crab_cpp` and `speculo`:

```cpp
#include <gtest/gtest.h>

import std;
import crab_cpp;
import speculo;

#include "support/class_fixtures.h"
```

Each header keeps its types in an unnamed namespace, so every translation unit
gets its own copy (the same ownership the tests had when they shared one file).
`registry_fixtures.h` is the exception: its types live in the named namespaces
the registry tests look up (`p6`, `p6b`, `p6c`, `p6ann`).

## Mapping from the former phase files

| Former file | Now |
| --- | --- |
| `skeleton_test.cpp` | `module_smoke_test.cpp` |
| `phase2_test.cpp` | `error_test.cpp`, `modifier_test.cpp`, `args_test.cpp`, `class_test.cpp`, `object_test.cpp` |
| `phase3_test.cpp` | `class_test.cpp`, `field_test.cpp`, `method_test.cpp`, `declared_view_test.cpp` |
| `phase4_test.cpp` | `constructor_test.cpp`, `method_test.cpp`, `modifier_test.cpp`, `const_correctness_test.cpp`, `volatile_correctness_test.cpp`, `args_test.cpp`, `field_test.cpp` |
| `phase5_test.cpp` | `type_identity_test.cpp`, `inheritance_test.cpp`, `enumerator_test.cpp`, `array_test.cpp` |
| `phase6_test.cpp` | `registry_test.cpp`, `error_message_test.cpp`, `args_test.cpp`, `std_library_test.cpp`, `annotation_test.cpp` |
