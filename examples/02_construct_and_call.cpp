// 02 — 构造与调用：new_instance、三级重载解析、静态成员、Object 生命周期
//
// 展示 Constructor / new_instance、Method 的三种查找路径
//（模板 / Class* / 类型名），以及 Object 的装箱、克隆与移出。

import std;
import crab_cpp;
import speculo;

struct Vec
{
    int x = 0;
    int y = 0;

    Vec() = default;
    explicit Vec(int a) : x(a) {}
    Vec(int a, int b) : x(a), y(b) {}

    int dot(const Vec& other) const { return x * other.x + y * other.y; }
    static Vec origin() { return Vec{}; }
};

int main()
{
    const speculo::Class& cls = speculo::class_of<Vec>();
    (void)speculo::class_of<int>();          // 注册 "int"，供类型名查找使用
    (void)speculo::class_of<const Vec&>();   // 注册 "const Vec&"

    std::println("== {} : {} constructors ==", cls.get_qualified_name(), cls.get_constructors().size());

    // ── 构造 ──
    int a = 3;
    int b = 4;
    speculo::Object v = std::move(cls.new_instance({ a, b }).unwrap());
    std::println("  new_instance(3,4)        -> x={} y={}", v.as<Vec>().unwrap().x, v.as<Vec>().unwrap().y);

    int seven = 7;
    speculo::Constructor one = cls.get_constructor<int>().unwrap();
    speculo::Object u = std::move(one.new_instance({ seven }).unwrap());
    std::println("  ctor<int>.new_instance(7)-> x={} (explicit={})",
                 u.as<Vec>().unwrap().x, one.is_explicit());

    // ── Method 的三级查找路径，结果等价 ──
    speculo::Method m1 = cls.get_method<"dot", const Vec&>().unwrap();               // 模板：编译期
    speculo::Method m2 = cls.get_method("dot", { &speculo::class_of<const Vec&>() }).unwrap(); // Class*
    speculo::Method m3 = cls.get_method("dot", { "const Vec&" }).unwrap();           // 类型名：注册表
    std::println("  three lookup paths agree : {} {} {}", m1 == m2, m2 == m3, m1.get_signature());

    Vec target{ 3, 4 };
    Vec other{ 7, 0 };
    speculo::Object dot = std::move(m1.invoke(target, { other }).unwrap());
    std::println("  dot((3,4),(7,0))         -> {}", dot.as<int>().unwrap());

    // ── 静态成员函数（obj 传空）──
    speculo::Method origin = cls.get_method("origin").unwrap();
    speculo::Object o = std::move(origin.invoke(speculo::ObjectRef{}, {}).unwrap());
    std::println("  static origin()          -> ({},{})", o.as<Vec>().unwrap().x, o.as<Vec>().unwrap().y);

    // ── Object 生命周期 ──
    auto cloned = v.clone();
    speculo::Object copy = std::move(cloned).unwrap();
    std::println("  clone                    -> ({} , {})",
                 copy.as<Vec>().unwrap().x, copy.as<Vec>().unwrap().y);

    Vec taken = std::move(v).take<Vec>().unwrap();
    std::println("  take()                   -> ({},{}) ; original null={}", taken.x, taken.y, v.is_null());

    return 0;
}
