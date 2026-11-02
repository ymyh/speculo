// S10 probe (focused): conversion operators, default-argument annotations.
#include <meta>

#include <cstddef>
#include <print>
#include <string>
#include <type_traits>

namespace m = std::meta;

struct RefLike
{
    const void* data = nullptr;
    const void* desc = nullptr;
    bool        is_const = false;
    bool        is_volatile = false;
};

struct RefLikePacked
{
    const void* data = nullptr;
    const void* desc = nullptr;
    bool        is_const = false;
    bool        is_volatile = false;
    bool        is_rvalue = false;
};

struct WithStatics
{
    static int plain;
    static thread_local int per_thread;
};

int WithStatics::plain = 1;
thread_local int WithStatics::per_thread = 2;

struct Conv
{
    int n = 0;
    operator int() const { return n; }
    explicit operator std::string() const { return "x"; }
};

template<typename T>
struct default_argument
{
    T value;
};

struct DefArgs2
{
    void f(int a, [[= default_argument<int>{ 7 }]] int b = 7) {}
};

template<m::info M>
consteval m::info find_member(std::string_view name)
{
    for (auto mm : m::members_of(M, m::access_context::unchecked()))
    {
        if (m::is_function(mm) && m::has_identifier(mm) && m::identifier_of(mm) == name)
        {
            return mm;
        }
    }
    return m::info{};
}

template<m::info M>
consteval m::info find_var(std::string_view name)
{
    for (auto mm : m::members_of(M, m::access_context::unchecked()))
    {
        if (m::is_variable(mm) && m::has_identifier(mm) && m::identifier_of(mm) == name)
        {
            return mm;
        }
    }
    return m::info{};
}

constexpr m::info kIntConv = find_member<^^Conv>("operator int");
constexpr m::info kStringConv = find_member<^^Conv>("operator std::string");
constexpr m::info kF = find_member<^^DefArgs2>("f");
constexpr m::info kParam1 = m::parameters_of(kF)[1];
constexpr m::info kAnno = m::annotations_of(kParam1)[0];
constexpr const char* kIntConvDisplay = std::define_static_string(m::display_string_of(kIntConv));
constexpr const char* kStringConvDisplay = std::define_static_string(m::display_string_of(kStringConv));
constexpr const char* kAnnoDisplay = std::define_static_string(m::display_string_of(m::remove_const(m::type_of(kAnno))));

using AnnoType = typename[: m::remove_const(m::type_of(kAnno)) :];
constexpr AnnoType kAnnoValue = m::extract<AnnoType>(kAnno);

int main()
{
    std::println("sizeof(RefLike)={} sizeof(RefLikePacked)={}", sizeof(RefLike), sizeof(RefLikePacked));

    constexpr m::info plain = find_var<^^WithStatics>("plain");
    constexpr m::info per_thread = find_var<^^WithStatics>("per_thread");
    std::println("plain: static={} thread={}", m::has_static_storage_duration(plain),
                 m::has_thread_storage_duration(plain));
    std::println("per_thread: static={} thread={}", m::has_static_storage_duration(per_thread),
                 m::has_thread_storage_duration(per_thread));

    std::println("int conv: is_function={} is_conversion={} display=[{}]", m::is_function(kIntConv),
                 m::is_conversion_function(kIntConv), kIntConvDisplay);
    std::println("string conv: is_function={} is_conversion={} display=[{}]", m::is_function(kStringConv),
                 m::is_conversion_function(kStringConv), kStringConvDisplay);

    constexpr std::size_t kParam1AnnotationCount = m::annotations_of(kParam1).size();
    std::println("param1 annotations={} anno type=[{}]", kParam1AnnotationCount, kAnnoDisplay);

    static_assert(std::is_same_v<AnnoType, default_argument<int>>);
    static_assert(kAnnoValue.value == 7);
    return 0;
}
