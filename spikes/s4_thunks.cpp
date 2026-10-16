// S4 —— thunk 合成验证：
//   a) 字段 getter/setter（拼接 obj.[:M:]），含 private / 引用成员 / const 只读
//   b) 位域读写的拼接路径
//   c) 静态成员的拼接读写
//   d) 成员函数 invoker（extract 精确成员函数指针 + 调用），含 const 与 void
//   e) 静态成员函数 invoker
//   f) 构造函数：堆分配 new / placement new
//   g) 析构
//   h) 基类指针转换（含虚基调整）
#include <meta>

#include <cstddef>
#include <cstdint>
#include <new>
#include <print>
#include <string>
#include <type_traits>
#include <utility>

#include "spike_util.hpp"

namespace m = std::meta;

// ===========================================================================
// 测试用类型
// ===========================================================================
struct Base1
{
    int b1 = 10;
};
struct Base2
{
    int b2 = 20;
};
struct Derived : Base1, Base2
{
    int d = 30;
};

struct VB
{
    int v = 7;
};
struct VD : virtual VB
{
    int x = 8;
};

struct Holder
{
    int ival = 5;
    int& rref;
    const int cval = 9;
    unsigned bf : 3;
    unsigned bf2 : 5;
    static int sval;

private:
    int hidden = 88;

public:
    Holder(int& r) : rref(r) {}
    int add(int a, int b) const { return a + b + ival; }
    void bump(int by) { ival += by; }
    static int smul(int a, int b) { return a * b; }
};

int Holder::sval = 123;

// ===========================================================================
// (a)(b)(c) 字段访问器：全部走成员访问拼接
// ===========================================================================

// 普通（含 private / 引用）字段 getter：按值返回
template <m::info M>
consteval auto make_getter()
{
    using C = typename[: m::parent_of(M) :];
    using F = std::remove_cvref_t<typename[: m::type_of(M) :]>;
    return +[](const void* p) -> F
    { return static_cast<const C*>(p)->[:M:]; };
}

// 非 const 字段 setter
template <m::info M>
consteval auto make_setter()
{
    using C = typename[: m::parent_of(M) :];
    using F = std::remove_cvref_t<typename[: m::type_of(M) :]>;
    return +[](void* p, F v)
    { static_cast<C*>(p)->[:M:] = v; };
}

// 静态成员 getter / setter（对象拼接 [:M:]）
template <m::info M>
consteval auto make_static_getter()
{
    using F = std::remove_cvref_t<typename[: m::type_of(M) :]>;
    return +[]() -> F
    { return [:M:]; };
}
template <m::info M>
consteval auto make_static_setter()
{
    using F = std::remove_cvref_t<typename[: m::type_of(M) :]>;
    return +[](F v)
    { [:M:] = v; };
}

// ===========================================================================
// (d)(e) 方法 invoker
// ===========================================================================
template <m::info M>
consteval auto make_add_invoker() // int (C::*)(int,int) const
{
    using C = typename[: m::parent_of(M) :];
    return +[](const void* p, int a, int b) -> int
    {
        constexpr int (C::*fp)(int, int) const = m::extract<int (C::*)(int, int) const>(M);
        return (static_cast<const C*>(p)->*fp)(a, b);
    };
}

template <m::info M>
consteval auto make_void_invoker() // void (C::*)(int)
{
    using C = typename[: m::parent_of(M) :];
    return +[](void* p, int a)
    {
        constexpr void (C::*fp)(int) = m::extract<void (C::*)(int)>(M);
        (static_cast<C*>(p)->*fp)(a);
    };
}

template <m::info M>
consteval auto make_static_invoker() // int (*)(int,int)
{
    return +[](int a, int b) -> int
    {
        constexpr int (*fp)(int, int) = m::extract<int (*)(int, int)>(M);
        return fp(a, b);
    };
}

// ===========================================================================
// (f)(g) 构造 / 析构
// ===========================================================================
template <typename Self, typename... Args>
void* heap_construct(Args... args)
{
    return new Self(args...);
}
template <typename Self, typename... Args>
void placement_construct(void* storage, Args... args)
{
    ::new (storage) Self(args...);
}
template <typename Self>
void destroy_self(void* p)
{
    static_cast<Self*>(p)->~Self();
}

// ===========================================================================
// (h) 基类指针转换（含虚基）
// ===========================================================================
template <m::info Base, m::info DerivedInfo>
consteval auto make_upcast()
{
    using B = typename[: m::type_of(Base) :];
    using D = typename[: DerivedInfo :];
    return +[](void* p) -> void*
    { return static_cast<B*>(static_cast<D*>(p)); };
}

template <std::size_t I>
consteval m::info base_of(m::info D)
{
    return m::bases_of(D, m::access_context::unchecked())[I];
}

consteval m::info find_named(m::info R, std::string_view nm)
{
    for (auto mm : m::members_of(R, m::access_context::unchecked()))
    {
        if (m::has_identifier(mm) && m::identifier_of(mm) == nm)
        {
            return mm;
        }
    }
    return m::info{};
}

int main()
{
    // 取成员反射
    constexpr auto U = m::access_context::unchecked();
    constexpr m::info ival   = m::nonstatic_data_members_of(^^Holder, U)[0];
    constexpr m::info rref   = m::nonstatic_data_members_of(^^Holder, U)[1];
    constexpr m::info cval   = m::nonstatic_data_members_of(^^Holder, U)[2];
    constexpr m::info bf     = m::nonstatic_data_members_of(^^Holder, U)[3];
    constexpr m::info bf2    = m::nonstatic_data_members_of(^^Holder, U)[4];
    constexpr m::info hidden = m::nonstatic_data_members_of(^^Holder, U)[5];
    static_assert(m::identifier_of(hidden) == "hidden");
    static_assert(m::is_private(hidden));

    {
        int ext = 100;
        Holder h(ext);
        auto get_ival = make_getter<ival>();
        auto set_ival = make_setter<ival>();
        if (get_ival(&h) != 5)
        {
            std::println("FAIL ival read");
            return 1;
        }
        set_ival(&h, 55);
        if (h.ival != 55)
        {
            std::println("FAIL ival write");
            return 1;
        }

        // 引用成员：getter 读被引用对象，setter 写到被引用对象（不重绑）
        auto get_rref = make_getter<rref>();
        auto set_rref = make_setter<rref>();
        if (get_rref(&h) != 100 || &h.rref != &ext)
        {
            std::println("FAIL rref binding");
            return 1;
        }
        set_rref(&h, 111);
        if (ext != 111)
        {
            std::println("FAIL rref write");
            return 1;
        }

        // const 成员：只读
        auto get_cval = make_getter<cval>();
        if (get_cval(&h) != 9)
        {
            std::println("FAIL cval");
            return 1;
        }

        // private 字段：拼接读取/写入
        auto get_hidden = make_getter<hidden>();
        auto set_hidden = make_setter<hidden>();
        if (get_hidden(&h) != 88)
        {
            std::println("FAIL hidden read");
            return 1;
        }
        set_hidden(&h, 99);
        if (get_hidden(&h) != 99)
        {
            std::println("FAIL hidden write");
            return 1;
        }
        std::println("(a) 字段拼接: ival={} rref-obj={} cval={} hidden={}", get_ival(&h), ext, get_cval(&h), get_hidden(&h));
    }

    {
        // (b) 位域读写
        int ext = 0;
        Holder h(ext);
        auto get_bf = make_getter<bf>();
        auto set_bf = make_setter<bf>();
        auto get_bf2 = make_getter<bf2>();
        auto set_bf2 = make_setter<bf2>();

        h.bf = 5;
        h.bf2 = 17;
        if (get_bf(&h) != 5 || get_bf2(&h) != 17)
        {
            std::println("FAIL bitfield read {} {}", get_bf(&h), get_bf2(&h));
            return 1;
        }
        set_bf(&h, 3);
        set_bf2(&h, 30);
        if (h.bf != 3 || h.bf2 != 30)
        {
            std::println("FAIL bitfield write {} {}", unsigned(h.bf), unsigned(h.bf2));
            return 1;
        }
        std::println("(b) 位域拼接: bf={} bf2={}", unsigned(h.bf), unsigned(h.bf2));
    }

    {
        // (c) 静态成员
        constexpr m::info sval = m::static_data_members_of(^^Holder, U)[0];
        auto g = make_static_getter<sval>();
        auto s = make_static_setter<sval>();
        if (g() != 123)
        {
            std::println("FAIL static read");
            return 1;
        }
        s(456);
        if (Holder::sval != 456)
        {
            std::println("FAIL static write");
            return 1;
        }
        std::println("(c) 静态成员拼接: sval={}", Holder::sval);
    }

    {
        // (d)(e) 方法调用
        int ext = 0;
        Holder h(ext);
        constexpr m::info add  = find_named(^^Holder, "add");
        constexpr m::info bump = find_named(^^Holder, "bump");
        constexpr m::info smul = find_named(^^Holder, "smul");
        static_assert(add != m::info{} && bump != m::info{} && smul != m::info{});

        auto inv_add  = make_add_invoker<add>();
        auto inv_bump = make_void_invoker<bump>();
        auto inv_smul = make_static_invoker<smul>();

        inv_bump(&h, 5);
        if (h.ival != 10)
        {
            std::println("FAIL bump");
            return 1;
        }
        if (inv_add(&h, 2, 3) != 15)
        {
            std::println("FAIL add");
            return 1;
        }
        if (inv_smul(6, 7) != 42)
        {
            std::println("FAIL smul");
            return 1;
        }
        std::println("(d)(e) 方法: add(2,3)={} bump->{} smul(6,7)={}", inv_add(&h, 2, 3), h.ival, inv_smul(6, 7));
    }

    {
        // (f)(g) 构造 / 析构
        int ext = 0;
        void* heap = heap_construct<Holder>(ext);
        auto g = make_getter<ival>();
        if (g(heap) != 5)
        {
            std::println("FAIL heap ctor");
            return 1;
        }
        destroy_self<Holder>(heap);
        ::operator delete(heap);

        alignas(Holder) unsigned char buf[sizeof(Holder)];
        placement_construct<Holder>(buf, ext);
        if (g(buf) != 5)
        {
            std::println("FAIL placement ctor");
            return 1;
        }
        destroy_self<Holder>(buf);
        std::println("(f)(g) 构造/析构: heap + placement ok");
    }

    {
        // (h) 基类指针转换
        Derived d;
        auto up_b1 = make_upcast<base_of<0>(^^Derived), ^^Derived>();
        auto up_b2 = make_upcast<base_of<1>(^^Derived), ^^Derived>();
        void* pb1 = up_b1(&d);
        void* pb2 = up_b2(&d);
        if (static_cast<Base1*>(pb1)->b1 != 10 || static_cast<Base2*>(pb2)->b2 != 20)
        {
            std::println("FAIL base cast");
            return 1;
        }
        const auto off1 = static_cast<char*>(pb1) - reinterpret_cast<char*>(&d);
        const auto off2 = static_cast<char*>(pb2) - reinterpret_cast<char*>(&d);

        VD vd;
        auto up_vb = make_upcast<base_of<0>(^^VD), ^^VD>();
        void* pvb = up_vb(&vd);
        if (static_cast<VB*>(pvb)->v != 7)
        {
            std::println("FAIL virtual base cast");
            return 1;
        }
        const auto offv = static_cast<char*>(pvb) - reinterpret_cast<char*>(&vd);
        std::println("(h) 基类转换: B1 offset={} B2 offset={} VB(virtual) offset={}", off1, off2, offv);
    }

    std::println("S4 ok");
}
