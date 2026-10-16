# speculo

speculo is a runtime reflection library for C++26. It turns GCC's compile-time reflection (`std::meta`, P2996) into handles you can look up and call by name: `Class`, `Field`, `Method`, `Constructor`.

The vocabulary follows `java.lang.reflect`. Names follow C++: `get_name`, `get_declared_fields`, `new_instance`. On top of that it reports things Java reflection cannot: size, alignment, byte and bit offsets, bit-fields, cv-qualifiers, ref-qualifiers, `noexcept`, and C++26 annotations (`[[= expr]]`).

Every descriptor is a compile-time constant. `class_of<T>()` is a pointer to that table. The library does not use RTTI or exceptions. Missing names come back as `Option`; failed operations come back as `Result`.

```cpp
import std;
import crab_cpp;
import speculo;

struct Point
{
    int x = 0;
    int y = 0;

    int scale(int factor) const { return x * factor; }
};

int main()
{
    const speculo::Class& cls = speculo::class_of<Point>();
    speculo::Field x = cls.get_field("x").unwrap();

    Point point{ 3, 4 };
    int ten = 10;
    x.set(point, ten).unwrap();

    speculo::Method scale = cls.get_method<int>("scale").unwrap();
    speculo::Object result = std::move(scale.invoke(point, { ten }).unwrap());
    // result.as<int>() == 100
}
```

## Requirements

- GCC 16 or newer. It is the compiler that implements `-freflection`.
- C++26, with `import std;`.
- CMake 3.31 or newer and the Ninja generator, or xmake.
- [crab_cpp](https://github.com/ymyh/crab-cpp) 0.4.0, fetched automatically.

Your own targets that `import speculo;` must also be built with `-std=c++26 -freflection -fno-rtti -fno-exceptions`.

## Building

### CMake

```sh
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Use a local crab_cpp checkout instead of cloning it:

```sh
cmake -B build -G Ninja -DFETCHCONTENT_SOURCE_DIR_CRAB_CPP=/path/to/crab-cpp
```

`SPECULO_BUILD_TEST` and `SPECULO_BUILD_EXAMPLES` both default to `ON`. Each file in `examples/` becomes its own executable, named `speculo_example_<file>`.

On Windows, point CMake at a GCC that has reflection, for example MSYS2 UCRT64. The Ninja generator is required because that is how CMake builds the `std` module.

### xmake

xmake on Windows selects MSVC unless you say otherwise. speculo needs GCC:

```sh
xmake f -p mingw --mingw=/path/to/ucrt64 -a x86_64
xmake
xmake test
```

On Linux the default GCC toolchain is enough, as long as it is GCC 16 or newer.

## Linking it into another target

CMake:

```cmake
target_link_libraries(your_app PRIVATE speculo)
target_compile_options(your_app PRIVATE -freflection -fno-rtti -fno-exceptions)
```

A translation unit looks like this:

```cpp
import std;
import crab_cpp;   // Option, Result, unwrap
import speculo;

#include <speculo/macros.h>   // only if you use SPECULO_REGISTER / SPECULO_IMPL
```

The macros live in a normal header. They cannot be exported from the module.

## What you can ask a type

`class_of<T>()` reflects the exact type, including cv-qualifiers and references. `class_of<const Point&>` is not `class_of<Point>`.

| Handle | Role |
|---|---|
| `Class` | Name, size, alignment, kind, bases, enumerators, constructors, and the member lists |
| `Field` | Read, write, and `get_ref` a data member. Also offset, bit-field width, `mutable` |
| `Method` | `invoke` (boxes a copy of the return value) and `invoke_ref` (for reference returns) |
| `Constructor` | `new_instance` on the heap, or `construct_at` into storage you own |
| `Object` / `ObjectRef` | An owned value, and a non-owning view of one. `ObjectRef` carries no reference type |
| `Array` | Index a `T[N]`, or allocate a runtime-length array with `Array::new_instance` |
| `Annotation` | One `[[= expr]]` value attached to a class, member, enumerator, base, or parameter |
| `Modifier` | The access and qualifier bits (`public`, `const`, `virtual`, `noexcept`, …) |

`get_fields()` and `get_methods()` are the public members declared on that class. `get_declared_fields()` and `get_declared_methods()` include private and protected members and still exclude inherited ones. Walk bases with `get_bases()` or `get_superclass()`, or use `get_field_recursive` / `get_method_recursive`.

Overloads are selected three ways, from fastest to slowest:

```cpp
cls.get_method<int>("scale");                          // compile-time parameter types
cls.get_method("scale", { &speculo::class_of<int>() }); // Class pointers
cls.get_method("scale", { "int" });                    // names, via the registry
```

`get_method("scale")` with only a name returns that method when the name is unique. It does not mean "the overload with no parameters".

`Class::for_name("demo::Point")` looks up a type that has already been materialized. The first `class_of<T>()` registers `T`. If nothing in the program ever names the type, register it yourself:

```cpp
SPECULO_REGISTER(demo::Wheel);
```

`SPECULO_IMPL(Type)` adds a non-virtual `get_class()`. `SPECULO_IMPL_VIRTUAL(Type)` is the virtual form, for a polymorphic hierarchy. The injected function is not itself reflected.

## Calls and values

Arguments are passed as an `Args` list: `invoke(obj, { a, b })`. Each argument is matched by exact type. There is no integer promotion, no derived-to-base conversion, and a `const` argument does not bind to `T&`. Convert first, or `cast_to` a base, then pass the result.

A by-value parameter is copied from the object you pass. An rvalue-reference parameter is not selected automatically; call that overload yourself when you mean to move.

`invoke` returns an owning `Object`. `as<T>()` gives a reference to the value inside it when the type matches. `take<T>()` moves the value out. Types that are not copyable cannot be boxed; `get` and `invoke` then return `NotSupported`, and `get_ref` / `invoke_ref` are the way to reach the object.

`cast_to` only walks up public bases. Down-casts and cross-casts need RTTI, which this library does not use.

## Standard-library types

A `std::string` member, or a function that takes `const std::string&`, is a normal reflected member. You can `get`, `set`, `get_ref`, and `invoke` it.

`class_of<std::string>()` itself stays shallow. It has a name, a size, and the operations `Object` needs to copy and destroy a string. It does not list `size`, `empty`, constructors, or bases. The same is true of `std::vector<T>`, `std::unique_ptr<T>`, and other types declared in namespace `std`. Expanding those members pulls a large amount of library code into the binary for every user type that mentions them.

## Annotations

C++26 annotations are constant values written `[[= expr]]`, distinct from ordinary attributes such as `[[nodiscard]]`.

```cpp
struct Label { int rank = 0; };

struct Point
{
    [[= Label{ 3 }]]
    [[= 1, = 2]]
    int x = 0;

    void scale([[= Label{ 1 }]] int factor) const;
};
```

```cpp
speculo::Field x = speculo::class_of<Point>().get_field("x").unwrap();
x.get_annotation<Label>().unwrap().rank;   // 3
x.get_annotation<int>().unwrap();          // 1, the first int; the 2 is later in get_annotations()
```

Annotations are not inherited. An annotation on a base-specifier belongs to that `BaseClass`, not to the derived class and not to the base type. `const Point` reports the same annotations as `Point`. `Point*` and `Point&` report none.

A value whose type contains a pointer or a reference is listed, but `is_readable()` is false and `get<T>()` is empty. On GCC 16.2, `[[= "literal"]]` has type `const char*` and cannot be read back. A `std::array<char, N>` can. Annotations on `= delete` functions are not enumerated: querying them instantiates the function body, and a deleted function has none.

## Limits

- Templates are not reflected, only concrete types. A `using` alias is the type it names; annotations on the alias are not visible.
- Function-local types and types in an anonymous namespace can be reflected, but `for_name` cannot see them.
- Bit-fields can be read and written. `get_ref` returns `NotSupported` because a bit-field has no address.
- Reflecting a class materializes every reflectable member, and the types those members mention. A class whose member function is ill-formed to instantiate cannot be materialized. Reflect your own types; `class_of` on a library class template specialization can fail for that reason.
- The project builds with `-fno-exceptions`. A reflected function that throws has undefined behavior.

## Examples

| Program | What it shows |
|---|---|
| `examples/01_basics.cpp` | Fields, layout, `get` / `set` / `get_ref`, `invoke` |
| `examples/02_construct_and_call.cpp` | Constructors, the three lookup paths, `Object` lifetime |
| `examples/03_inheritance_enum_array.cpp` | Bases, enumerators, arrays |
| `examples/04_registry_and_macros.cpp` | `for_name`, `SPECULO_REGISTER`, `SPECULO_IMPL` |

`speculo::version_string()` returns `"0.1.0"`.
