// S8.9 基线：与 s8_9_traversal.cpp 相同的头文件集合，但不含 128 字段 define_aggregate，
//   用于测出「大结构体编译期遍历」的净开销（见 review.md）。
#include <meta>

#include <array>
#include <print>
#include <string>
#include <vector>

#include "spike_util.hpp"

namespace m = std::meta;

struct Point
{
    int x;
    double y;
    char z;
};

consteval std::size_t total_bits()
{
    std::size_t n = 0;
    for (auto mem : m::nonstatic_data_members_of(^^Point, m::access_context::unchecked()))
    {
        n += m::bit_size_of(m::type_of(mem));
    }
    return n;
}

int main()
{
    std::println("baseline total_bits={}", total_bits());
    std::println("S8.9b ok");
}
