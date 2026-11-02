#include <meta>
#include <print>

template<typename T>
struct default_argument
{
    T value;
};

struct S
{
    void f(int a, [[= default_argument{ 7 }]] int b = 7) {}
};

int main()
{
    return 0;
}
