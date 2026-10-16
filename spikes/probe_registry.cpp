// probe —— 注册表名口径：判定哪些反射类型有「可寻址的规范限定名」。
// 候选谓词：has_identifier / has_internal_linkage / is_namespace_member / has_parent / parent_of。
#include <meta>

#include <cstddef>
#include <print>
#include <string>
#include <string_view>

namespace m = std::meta;

struct NsType
{
};

namespace named
{
struct InNamed
{
};
}

namespace
{
struct AnonType
{
};
}

template<typename T>
struct Tmpl
{
};

struct Outer
{
    struct Inner
    {
    };
};

template<m::info R>
void probe(std::string_view label)
{
    std::string name = "<no id>";
    if constexpr (m::has_identifier(R))
    {
        name = std::string(m::identifier_of(R));
    }

    std::string parent = "<none>";
    if constexpr (m::has_parent(R))
    {
        parent = std::string(m::display_string_of(m::parent_of(R)));
    }

    std::println("{:<16} display={:<20} id={:<5} internal={:<5} ns_member={:<5} has_parent={:<5} parent={}",
                 label, std::string(m::display_string_of(R)),
                 m::has_identifier(R), m::has_internal_linkage(R),
                 m::is_namespace_member(R), m::has_parent(R), parent);
}

int main()
{
    struct LocalType
    {
    };

    probe<^^int>("int");
    probe<^^double>("double");
    probe<^^int*>("int*");
    probe<^^const int&>("const int&");
    probe<^^NsType>("NsType");
    probe<^^named::InNamed>("named::InNamed");
    probe<^^AnonType>("AnonType");
    probe<^^LocalType>("LocalType");
    probe<^^Tmpl<int>>("Tmpl<int>");
    probe<^^Outer::Inner>("Outer::Inner");
    probe<^^void>("void");
    probe<^^std::string_view>("std::string_view");
}
