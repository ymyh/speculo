// 03 — 继承、枚举与数组
//
// 展示 get_bases / get_superclass / cast_to（含虚派发）、
// 枚举的三向互转，以及静态/动态数组的访问。

import std;
import crab_cpp;
import speculo;

struct Shape
{
    int id = 0;
    virtual ~Shape() = default;
    virtual int area() const { return 0; }
};

struct Square : Shape
{
    int side = 1;
    int area() const override { return side * side; }
};

struct Colored
{
    int rgb = 0;
};

enum class Color
{
    Red = 0,
    Green = 5,
    Blue = 6,
};

struct Ints
{
    int v[3]{ 1, 2, 3 };
    int m[2][2]{ { 1, 2 }, { 3, 4 } };
};

int main()
{
    // ── 继承 ──
    const speculo::Class& shape = speculo::class_of<Shape>();
    const speculo::Class& square = speculo::class_of<Square>();
    std::println("== inheritance ==");
    std::println("  Square bases={} superclass={}",
                 square.get_bases().size(), square.get_superclass().unwrap().get_name());
    std::println("  Shape is_assignable_from Square = {} ; from Colored = {}",
                 shape.is_assignable_from(square),
                 shape.is_assignable_from(speculo::class_of<Colored>()));

    Square sq;
    sq.side = 5;

    // 向上转换（多继承/虚基偏移由 thunk 处理）。接收者必须与声明类一致，故先转换。
    speculo::ObjectRef ref(sq);
    auto as_shape = square.cast_to(ref, shape);

    // 经基类 Method 调用，虚派发得到派生实现。
    speculo::Method area = shape.get_method("area").unwrap();
    std::println("  Shape::area on a Square -> {}", area.invoke(as_shape.unwrap(), {}).unwrap().as<int>().unwrap());
    std::println("  cast_to(Shape) ok={} type={} ; Shape::is_instance(sq)={}",
                 as_shape.is_ok(), as_shape.unwrap().type().get_name(), shape.is_instance(ref));

    // ── 枚举 ──
    std::println("== enum ==");
    const speculo::Class& color = speculo::class_of<Color>();
    std::print("  Color (underlying {}):", color.get_underlying_type().unwrap().get_name());
    for (const speculo::Enumerator& e : color.get_enumerators())
    {
        std::print(" {}={}", e.get_name(), e.to_int64());
    }
    std::println("");

    speculo::Object green = std::move(color.enum_from_name("Green").unwrap());
    std::println("  enum_from_name(Green) == Green? {}",
                 green.as<Color>().unwrap() == Color::Green);
    Color blue = Color::Blue;
    std::println("  enum_name_of(Blue) = {}", color.enum_name_of(speculo::ObjectRef(blue)).unwrap());

    // ── 数组 ──
    std::println("== array ==");
    Ints ints;
    const speculo::Class& icls = speculo::class_of<Ints>();

    speculo::ObjectRef arr = icls.get_field("v").unwrap().get_ref(ints).unwrap();
    std::println("  int[3]: type={} rank={} length={} element={}",
                 arr.type().get_name(), arr.type().get_array_rank(),
                 speculo::Array::get_length(arr).unwrap(),
                 speculo::Array::get_element_type(arr).unwrap().get_name());
    std::println("  arr[2] = {}", speculo::Array::get(arr, 2).unwrap().as<int>().unwrap());

    int nine = 9;
    speculo::Array::set(arr, 2, nine).unwrap();
    std::println("  after set arr[2]={} -> v[2]={}", nine, ints.v[2]);

    // 多维：逐级降维
    speculo::ObjectRef matrix = icls.get_field("m").unwrap().get_ref(ints).unwrap();
    auto row = speculo::Array::get_ref(matrix, 1).unwrap();
    std::println("  matrix rank={} -> row rank={} m[1][0]={}",
                 matrix.type().get_array_rank(), row.type().get_array_rank(),
                 speculo::Array::get(row, 0).unwrap().as<int>().unwrap());

    // 动态数组（运行期长度）
    speculo::Object dyn = std::move(speculo::Array::new_instance(speculo::class_of<int>(), 4).unwrap());
    std::println("  dynamic int[4]: length={} is_array={} element={}",
                 speculo::Array::get_length(dyn).unwrap(), dyn.type().is_array(),
                 speculo::Array::get_element_type(dyn).unwrap().get_name());

    return 0;
}
