// S8.1 模板：has_template_arguments / template_arguments_of / template_of / substitute
//   关注点：
//   - 类型实参、NTTP、template-template 实参分别是什么 kind（is_type / is_value / is_template）
//   - 参数包在实参表里如何呈现（是否被展平、有无 Pack 项）
//   - template_of 能否 display_string_of 出 std::vector（以及 identifier_of 是否更可靠）
//   - substitute(primary, args) 能否回到原类型
#include <meta>

#include <array>
#include <print>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "spike_util.hpp"

namespace m = std::meta;

template <class T, template <class> class TT, int N>
struct Pack3
{
};

// ---- 编译期结论（断言即结论）----

// 1) 类型实参：std::vector<int> 有 2 个实参（int + 默认 allocator），不是 1 个
constexpr m::info kVec = ^^std::vector<int>;
static_assert(m::has_template_arguments(kVec));
static_assert(m::template_arguments_of(kVec).size() == 2);
static_assert(m::is_same_type(m::template_arguments_of(kVec)[0], ^^int));

// 2) 主模板：template_of 本身没有 template_arguments；identifier_of 给出 "vector"
constexpr m::info kVecTpl = m::template_of(kVec);
static_assert(!m::has_template_arguments(kVecTpl));
static_assert(m::is_class_template(kVecTpl));
static_assert(m::identifier_of(kVecTpl) == "vector");

// 3) substitute 往返（类型实参 / NTTP / 包 三种都成立）
static_assert(m::is_same_type(m::substitute(kVecTpl, m::template_arguments_of(kVec)), kVec));
constexpr m::info kArr = ^^std::array<int, 3>;
constexpr m::info kTup = ^^std::tuple<int, char>;
static_assert(m::is_same_type(m::substitute(m::template_of(kArr), m::template_arguments_of(kArr)), kArr));
static_assert(m::is_same_type(m::substitute(m::template_of(kTup), m::template_arguments_of(kTup)), kTup));

// 4) NTTP：第三个实参是 value，constant_of 后可 extract 出 3（类型必须精确匹配：size_t）
constexpr m::info kArrN = m::template_arguments_of(kArr)[1];
static_assert(m::is_value(kArrN));
static_assert(!m::is_type(kArrN));
static_assert(m::extract<std::size_t>(m::constant_of(kArrN)) == 3);

// 5) 参数包：std::tuple<int, char> 的包在实参表中被展平成 2 项，没有单独的 Pack 项
static_assert(m::template_arguments_of(kTup).size() == 2);
static_assert(m::is_same_type(m::template_arguments_of(kTup)[0], ^^int));
static_assert(m::is_same_type(m::template_arguments_of(kTup)[1], ^^char));

// 6) template-template 实参：is_template 为真，is_type / is_value 均为假
constexpr m::info kP3 = ^^Pack3<int, std::vector, 7>;
constexpr m::info kP3TT = m::template_arguments_of(kP3)[1];
static_assert(m::is_template(kP3TT));
static_assert(!m::is_type(kP3TT));
static_assert(!m::is_value(kP3TT));
static_assert(m::extract<int>(m::constant_of(m::template_arguments_of(kP3)[2])) == 7);

consteval std::string dump(std::string& s, const char* tag, m::info T)
{
    s += "--- ";
    s += tag;
    s += "\n  has_template_arguments = ";
    s += m::has_template_arguments(T) ? "true" : "false";
    s += "\n  template_of.display_string_of = [";
    s += m::display_string_of(m::template_of(T));
    s += "]\n  template_of.identifier_of    = [";
    s += m::identifier_of(m::template_of(T));
    s += "]\n  primary.has_template_arguments = ";
    s += m::has_template_arguments(m::template_of(T)) ? "true" : "false";
    s += "\n";
    auto args = m::template_arguments_of(T);
    s += "  args (" + spike::to_dec(args.size()) + "):\n";
    for (auto a : args)
    {
        s += "    type=";
        s += m::is_type(a) ? "1" : "0";
        s += " value=";
        s += m::is_value(a) ? "1" : "0";
        s += " template=";
        s += m::is_template(a) ? "1" : "0";
        s += "  display=[";
        s += m::display_string_of(a);
        s += "]";
        if (m::is_value(a))
        {
            s += "  value_type=[";
            s += m::display_string_of(m::type_of(a));
            s += "]";
        }
        s += "\n";
    }
    s += "  substitute round-trip: is_same_type = ";
    s += m::is_same_type(m::substitute(m::template_of(T), args), T) ? "true" : "false";
    s += "\n";
    return s;
}

consteval std::string dump_all()
{
    std::string s;
    dump(s, "std::vector<int>", kVec);
    dump(s, "std::array<int, 3>", kArr);
    dump(s, "std::tuple<int, char>", kTup);
    dump(s, "Pack3<int, std::vector, 7>", kP3);
    return s;
}

int main()
{
    static constexpr const char* d = std::define_static_string(dump_all());
    std::print("{}", d);
    std::println("S8.1 ok");
}
