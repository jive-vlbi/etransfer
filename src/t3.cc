// own headers
#include <version.h>

// std c++ headers
#include <map>
#include <tuple>
#include <string>
#include <iostream>
#include <typeindex>
#include <type_traits>
#include <stdexcept>

// old-style C headers
#include <stdlib.h>

using namespace std;

struct     SomeStruct { SomeStruct(int i=0): a(i) {}; int a; };
ostream& operator<<(ostream& os, SomeStruct const& ss) {
    return os << "SomeStruct{ a=" << ss.a << " }";
}

enum class SomeEnum : int  { aap=1, noot=2, mies=4, invalid=0 };

#define KEES(a) case a: os << #a; break
ostream& operator<<(ostream& os, SomeEnum const& en) {

    os << "[" << static_cast<int>(en) << ":";
    switch( en ) {
        KEES(SomeEnum::aap);
        KEES(SomeEnum::noot);
        KEES(SomeEnum::mies);
        KEES(SomeEnum::invalid);
        default: os << "<unhandled?!>";
    }
    return os << "]";
}
#undef KEES

//   Construct<SomeStruct>(&SomeStruct::a, &SomeStruct::b) => fn(Args...)
//
#if 0
namespace construct {
    template <typename Class>
    struct Construct {
        template <typename T, typename... Rest>
        construct(T (Class::*Member)
    };
}

#endif

void process_kwargs( void ) {
    cout << "process_kwargs: terminating recursion" << endl;
}

//void process_kwargs(std::map<std::string, T> const& kwarg, Ts... ts) {
template <typename T, typename... Ts>
void process_kwargs(std::initializer_list<T> kwarg, Ts... ts) {
    for(auto&& kv: kwarg)
        cout << "kwarg[" << typeid(T).name() << "]: " << kv.first << "=" << kv.second << endl;
    process_kwargs(ts...);
}

template <typename T, typename... Ts>
void process_kwargs(std::initializer_list<std::pair<char const*, T> > kwarg, Ts... ts) {
    for(auto&& kv: kwarg)
        cout << "kwarg[" << typeid(T).name() << "]: " << kv.first << "=" << kv.second << endl;
    process_kwargs(ts...);
}

template <typename T, typename... Ts>
void process_kwargs(std::initializer_list<std::pair<std::string, T> > kwarg, Ts... ts) {
    for(auto&& kv: kwarg)
        cout << "kwarg[" << typeid(T).name() << "]: " << kv.first << "=" << kv.second << endl;
    process_kwargs(ts...);
}

void test(std::initializer_list<std::pair<std::string, int> > il) {
    for(auto&& kv: il)
        cout << "test: " << kv.first << "=" << kv.second;
    return;
}

template <typename T1, typename T2>
void test2(std::initializer_list<T1> l1, std::initializer_list<T2> l2) {
    cout << "test2!" << endl;
    for(auto&& l1el: l1)
       cout << "  l1: " << l1el << endl;
    for(auto&& l2el: l2) 
       cout << "  l2: " << l2el << endl;
}

void test3( void ) {
    cout << "test3! recursion stops" << endl;
}

template <typename T1, typename... Ts>
void test3(std::initializer_list<T1> l1, Ts... ts) {
    cout << "test3!" << endl;
    for(auto&& l1el: l1)
       cout << "  l1: " << l1el << endl;
    test3(ts...);
}

void test4_aux(void) {
    cout << "test4aux/w no arguments (i.e. void)" << endl;
}
template <typename... Ts>
void test4_aux(std::tuple<Ts...> const& /*tup*/) {
    cout << "test4aux/called with one tuple, " << sizeof...(Ts) << " elements" << endl;
}

template <typename... Tuples>
void test4_aux(Tuples... tuples) {
    cout << "test4aux/got " << sizeof...(tuples) << " arguments" << endl;
}


void test4(void) {
    cout << "test4/w no arguments (i.e. void)" << endl;
}

template <typename T, typename... Ts>
void test4(std::initializer_list<T> const& il, Ts&&... ts) {
    cout << "test4/w initializer_list of " << typeid(T).name() << " nel=" << il.size() << endl;
    test4_aux(std::forward_as_tuple( std::forward<Ts>(ts)... ));
}


template <typename... Args>
void test4(Args&&... args) {
    cout << "test4/w Args&&... " << sizeof...(args) << " refs" << endl;
    test4_aux(std::forward_as_tuple(std::forward<Args>(args)...));
}


template <typename Class, typename Value>
struct MemberPointer {
    typedef Value Class::*Member;
    MemberPointer(Member ptr):
        memberPointer(ptr)
    {}

    void operator()(Class& object, Value const&& value) const {
        (object.*memberPointer) = value;
    }
    Member  memberPointer;
};

template <typename Class>
struct Construct {
    typedef std::map<std::type_index, void*> typemap_type;

    template <typename... Args>
    Construct(Args&&... args) {
        this->build_map(std::forward<Args>(args)...);
    }

    // single argument stops recursion
    template <typename T>
    unsigned int operator()(T&&) const {
        cout << "operator(): terminating recursion" << endl;
        return 0;
    }

    template <typename T, typename... Args>
    unsigned int operator()(Class& object, T const&& value, Args... args) const {
        auto         entry = typemap.find( type_index(typeid(T)) );
        unsigned int n = 0;

        if( entry!=typemap.end() )
            (object.*((static_cast<MemberPointer<Class, T>*>(entry->second))->memberPointer)) = value, n=1;
        cout << "operator(): assigning value " << value << " yields " << ((n==0) ? "No" : "Yes") << endl;
        return n + this->operator()(object, std::forward<Args>(args)...);
    }
            

    private:
        typemap_type typemap;

        void build_map(void) {
            cout << "Construct::build_map/recursion ends" << endl;
        }

        template <typename T, typename... Args>
        void build_map(T (Class::*MemberPtr), Args... args) {
            auto entry = typemap.insert( make_pair(type_index(typeid(T)), static_cast<void*>(new MemberPointer<Class, T>(MemberPtr))) ) ;
            cout << "Construct::build_map/" << typeid(T).name() << endl;
            if( !entry.second )
                throw std::logic_error( std::string("Double insert of type ")+typeid(T).name() );
            this->build_map(std::forward<Args>(args)...);
        }
};

//is_member_object_pointer<T>
namespace detail {
    template <typename T>
    struct object_type {
        typedef std::false_type type;
    };
    template <typename T, typename Class>
    struct object_type<T Class::*> {
        typedef Class type;
    };

}

template <typename A, typename B,
            typename std::enable_if<
                 std::is_same<typename detail::object_type<A>::type, typename detail::object_type<B>::type>::value &&
                 !std::is_same<typename detail::object_type<A>::type, std::false_type>::value,
                 int>::value = 0
         >
struct same_object_type {};

/*
    std::conditional<
        // condition
        std::is_same<typename detail::object_type<A>::type, typename detail::object_type<B>::type > &&
            !std::is_same<typename detail::object_type<A>::type, std::false_type>,
        // if true, evaluate to non-false type
        typename detail::object_type<A>::type,
        // if false, propagate false type
        std::false_type
    >
{
}
*/

/*
struct Konstruct {
    template <typename T>
    unsigned int operator()(T&) const {
        cout << "Konstruct: terminating recursion" << endl;
    }
};
template <typename T>
struct Konstruct {
    typedef typename detail::object_type<T>::type my_type;
};
*/

/*
template <typename T U::*, typename... Args>
struct Konstruct {
    typedef typename Konstruct<Args...>::type  Next;
    //typedef detail::object_type<T>::type my_type;

    //typedef same_object_type<my_type, Next::my_type>::type  type;
    //typedef typename same_object_type<T, Next>::type  type;
    typedef std::is_same<U, Next>::type     type;
};
*/
//template <typename T>
//struct Maak {
//    typedef std::false_type type;
//};


#if 0
template <typename Class, typename Class::*Member...>
void  foobar(Class*, Class::*Member&&...) {
    cout << "Foobar!" << endl;
}
#endif

struct Test {
    SomeStruct   myStruct;
    SomeEnum     myEnum;
};


ostream& operator<<(ostream& os, Test const& t) {
    return os << "Test{ myStruct.a=" << t.myStruct.a << ", myEnum=" << t.myEnum << "}";
}


int main( void ) {
    test4({1,2, 3}, 1, 2);
    test4({"aap", "noot"});
    //test4({1,2}, {"foo", "bar"});
    //test4({{1,2}}, {{"foo", "bar"}});
    //test4(1, "foo", 3.0);
    //process_kwargs({1,2}, {"foo", "bar"});
    //process_kwargs({{"aap", 1}}, {{"foo", "bar"}});
    //test({{"aap", 1}});
    return 0;
}

int main2( void ) {
    Test   t1;
    Test   t2{ SomeStruct{::atoi( ::version_constant("SEQUENCE_NUMBER").c_str() )}, SomeEnum{SomeEnum::noot} };
    Test   t3{ SomeStruct{-42}, SomeEnum{SomeEnum::mies} };
//    Test   t2{ SomeEnum{SomeEnum::noot}, SomeStruct{::atoi( ::version_constant("SEQUENCE_NUMBER").c_str() )} };
//    Test   t3{ SomeStruct{-42}, SomeEnum{SomeEnum::mies} };

    cout << "version_constant(SEQUENCE_NUMBER)=" << ::version_constant("SEQUENCE_NUMBER") << endl;
    cout << "t1: " << t1 << endl
         << "t2: " << t2 << endl
         << "t3: " << t3 << endl;

    Construct<Test>  kreator{&Test::myEnum, &Test::myStruct};
    unsigned int     r;

    //r = kreator(t1, SomeStruct{33}, SomeEnum{SomeEnum::aap});
    r = kreator(t1, SomeEnum{SomeEnum::aap}, SomeStruct{33});
    r = kreator(t3, SomeStruct{88});
    cout << "r = " << r << endl;
    cout << "t1: " << t1 << endl
         << "t2: " << t2 << endl
         << "t3: " << t3 << endl;
    return 0;
}
