// Wanted:
//
//   Flags:
//   -nlmSux            => one or more single-character boolean flags
//                         may be optional or count [cf. "ssh -vvvv" -> more "v" is higher]
//                         [store_true/store_false/count]
//
//   --long-opt         => long name flag, boolean
//                         [store_true/store_false/count]
//
//   Options:
//   -f <value>         => short name option, can not combine multiple short
//                         name options. presence could be 0 or 1, 0 or
//                         more, 1 or more.
//                         Action could be: append or set
//
//   --long-f <value>   => Two syntaxes for long name options. Same as short
//   --long-f=<value>      name version.
//
//   Argument:
//   <value>            => arguments can be gathered. User controls
//                         if 0 or more, 1 or more, 0 or 1 or min,max
//                         applies
//
#include "functools.h"
//#include <utilities.h>
#include <set>
#include <map>
#include <list>
#include <regex>
#include <limits>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <functional>
#include <typeindex>

#include <ctype.h>
#include <cstdlib>   // for std::free, std::exit
#include <cstring>
#include <cxxabi.h>

using ignore_t = typename std::decay<decltype(std::ignore)>::type;

// Tests if "std::ostream<<T" is well-formed
template <typename T>
struct is_streamable {
    using yes = char;
    using no  = long long int;

    template <typename U>
    static auto test(std::ostream& os, U const& u) -> decltype(os << u, yes()) {}
    //                                                ^^^^^^^^^^^^^^^^^^^^^^^^
    //                                 Thanks @ http://stackoverflow.com/a/9154394

    template <typename U>
    static no test(...);

    static constexpr bool value = (sizeof(test<T>(std::declval<std::ostream&>(), std::declval<T const&>()))==sizeof(yes));
};

// Tests if T has "Ret operator()(Args...)" defined
// (yes, also checks the return type)
template <template <typename...> class Match, typename T, typename Ret, typename... Args>
struct has_operator {
    using yes = char;
    using no  = unsigned long;

    // Test if V matches the requested Ret according to Match specification
    template <typename V,
              typename std::enable_if<Match<V, Ret>::value, int>::type = 0>
    static yes test_rv(V*);

    template <typename V>
    static no  test_rv(...);

    // Test if T has "operator()(Args...)" defined in the first place
    // (by testing if calling it is well-formed)
    template <typename U>
    static auto test( decltype( std::declval<U>()(std::declval<Args>()...) )* ) -> 
        // Yes, proceed to test the return type
        decltype( test_rv<decltype(std::declval<U>()(std::declval<Args>()...))>(nullptr) );
    template <typename U>
    static no   test(...);

    static constexpr bool value = sizeof(test<T>(nullptr))==sizeof(yes);
};

template <typename... Ts>
using has_compatible_operator = has_operator<std::is_convertible, Ts...>;
template <typename... Ts>
using has_exact_operator      = has_operator<std::is_same, Ts...>;

namespace detail {
    // Adaptor for easy creation of reversed range-based for loop, thanks to:
    // http://stackoverflow.com/a/36928761
    template <typename C>
    struct reverse_wrapper {
        C& c_;   // could use std::reference_wrapper, maybe?
        reverse_wrapper(C& c) :  c_(c) {}

        // container.rbegin()/container.rend() is fine - these have existed for long
        typename C::reverse_iterator begin() { return c_.rbegin(); }
        typename C::reverse_iterator end()   { return c_.rend();   }
    };

    template <typename C, size_t N>
    struct reverse_wrapper< C[N] >{
        C (&c_)[N];
        reverse_wrapper( C(&c)[N] ) : c_(c) {}

        // std::rbegin()/std::rend() are c++14 :-(
        typename std::reverse_iterator<const C *> begin() { return &c_[N-1];/*std::rbegin(c_);*/ }
        typename std::reverse_iterator<const C *> end()   { return &c_[-1]; /*std::rend(c_);*/   }
    };
}
template <typename C>
detail::reverse_wrapper<C> reversed(C& c) {
    return detail::reverse_wrapper<C>(c);
}

// Tests if tye C has typedef "iterator" , typedef "value_type"
// and a member function ".insert(value_type, iterator)"
template <typename T>
struct can_insert {
    // Only works if sizeof(char)!=sizeof(unsigned long)
    using yes = char;
    using no  = unsigned long;


    template <typename Container, typename Value>
    static auto test2(Container* w) -> decltype(w->insert(w->end(), std::declval<Value>()), yes());

    template <typename Container, typename Value>
    static no   test2(...);

    template <typename Container>
    static auto test(typename Container::value_type* v, typename Container::iterator*) ->
            decltype( test2<Container, typename Container::value_type>(nullptr) );

    template <typename Container>
    static no   test(...);

    static constexpr bool value = sizeof(test<T>(nullptr, nullptr))==sizeof(yes);
};




// Deduce the return type and arguments of a callable
//
// This implementation is a combination of tricks found here:
//      https://github.com/Manu343726/TTL/blob/master/include/overloaded_function.hpp
//      http://stackoverflow.com/a/21665705
//
//  NOTE: due to the nature of how std::bind(...) is implemented this
//        code cannot accept instances of std::bind(...) - the function
//        call signature of a std::bind(...) object can not be inferred:
//        see http://stackoverflow.com/a/21739025
template <typename R, typename... Args>
struct signature {
    using return_type                  = R;
    using argument_type                = std::tuple<Args...>;
    static constexpr std::size_t arity = sizeof...(Args);
};

template <typename T>
struct deduce_signature: deduce_signature< decltype( &T::operator() ) > {
    static_assert( !std::is_bind_expression<T>::value,
                   "The signature of a std::bind(...) expression can not be inferred. Wrap your call in a lambda instead.");
};

template <typename R, typename... Args>
struct deduce_signature<R(*)(Args...)>: signature<R, Args...> {};

template <typename R, typename... Args>
struct deduce_signature<R(&)(Args...)>: signature<R, Args...> {};

template <typename R, typename... Args>
struct deduce_signature<std::function<R(Args...)>> : signature<R, Args...> {};

// these two capture lambda's - they have a scope!
template <typename R, typename C, typename... Args>
struct deduce_signature<R(C::*)(Args...)> : signature<R, Args...> {};

template <typename R, typename C, typename... Args>
struct deduce_signature<R(C::*)(Args...) const> : signature<R, Args...> {};

template <typename R, typename C, typename... Args>
struct deduce_signature<std::function<R(C::*)(Args...)>> : signature<R, Args...> {};

template <typename R, typename C, typename... Args>
struct deduce_signature<std::function<R(C::*)(Args...) const>> : signature<R, Args...> {};

template <typename T>
struct is_unary_fn: std::integral_constant<bool, T::arity==1> {};



// Kwik-n-dirty / 'lightweight type categorization idiom'
// Thanks to http://stackoverflow.com/a/9644512
//
// Use it to get a quick test if a type seems to be std:: container;
// all of them have "::value_type"
template<class T, class R = void>  
struct enable_if_type { typedef R type; };

template<class T, class Enable = void>
struct maybe_container : std::false_type {};

template<class T>
struct maybe_container<T, typename enable_if_type<typename T::value_type>::type> : std::true_type {};

// Should look at http://stackoverflow.com/a/20303333
template <typename T>
std::string demangle_f(void) {
    int              status = -4; // some arbitrary value to eliminate the compiler warning
    char const*const name = typeid(T).name();

    // enable c++11 by passing the flag -std=c++11 to g++
    std::unique_ptr<char, void(*)(void*)> res {
        abi::__cxa_demangle(name, NULL, NULL, &status),
        std::free
    };

    return (status==0) ? res.get() : name ;
}

// For usage we don't want "std::string" printed
template <typename T>
std::string optiontype( void ) {
    return demangle_f<T>();
}
template <>
std::string optiontype<std::string>( void ) {
    return "string";
}

// How to output a type / type's value
struct string_repr {
    // If it's streamable, then we insert its value
    template <typename T,
              typename std::enable_if<is_streamable<T>::value, int>::type = 0>
    std::string operator()(T const& t) const {
        std::ostringstream oss;
        oss << t;
        return oss.str();
    }
    // Otherwise we just output the (demangled) type name
    template <typename T,
              typename std::enable_if<!is_streamable<T>::value, int>::type = 0>
    std::string operator()(T const&) const {
        return demangle_f< typename std::decay<T>::type >();
    }

    // Some specializations that don't have to go through "operator<<"
    std::string operator()(std::string const& s) const {
        return s;
    }
    std::string operator()(char const& c) const {
        return std::string(&c, &c+1);
    }
    std::string operator()(char const*const c) const {
        return std::string(c);
    }
};

// Std operators as readable strings
template <template <typename...> class OP>
std::string op2str( void ) {
    return "<unknown operator>";
}
template <>
std::string op2str<std::less>( void ) {
    return "less than";
}
template <>
std::string op2str<std::less_equal>( void ) {
    return "less than or equal";
}
template <>
std::string op2str<std::greater>( void ) {
    return "greater than";
}
template <>
std::string op2str<std::greater_equal>( void ) {
    return "greater than or equal";
}
template <>
std::string op2str<std::equal_to>( void ) {
    return "equal to";
}


template <typename... Ts>
std::string build_string(Ts&&... ts) {
    return functools::foldl(std::plus<std::string>(),
                            functools::map(std::forward_as_tuple(ts...), string_repr()),
                            std::string());
}



struct CmdLineBase {
    // Print help, short (true) or long (false) format
    virtual void print_help( bool ) const = 0;
    virtual void print_version( void ) const = 0;

    virtual ~CmdLineBase() {}
};


struct constraint_violation: public std::domain_error {
    using std::domain_error::domain_error;
};

template <typename T>
using Constraint  = std::function<bool(typename std::decay<T>::type const&)>;

template <typename T>
using ConstraintS = std::function<std::string(typename std::decay<T>::type const&)>;

struct constraint_impl {
    constraint_impl() = delete;

    template <typename F,
          typename std::enable_if<!std::is_bind_expression<F>::value, int>::type = 0,
          typename std::enable_if<is_unary_fn< deduce_signature<F> >::value, int>::type = 0,
          typename Arg = typename std::decay<typename std::tuple_element<0, typename deduce_signature<F>::argument_type>::type>::type,
          typename Ret = typename std::decay<typename deduce_signature<F>::return_type>::type,
          typename std::enable_if<std::is_same<Ret, bool>::value, int>::type = 0>
    constraint_impl(std::string const& descr, F f):
        __m_docstr( descr ),
        __m_index( std::type_index(typeid(typename std::decay<F>::type)) ),
        __m_function( new typename std::decay<F>::type(f) )
    {}
    template <typename T,
              typename RealType = typename std::decay<T>::type>
    std::string constrain(T const& t) const {
        string_repr  stringer{};
        auto         ti = std::type_index(typeid(Constraint<RealType>));

        if( ti!=__m_index )
            throw std::runtime_error(std::string("Constraint types did not match: expect=")+__m_index.name()+" got="+ti.name());
        if( (*reinterpret_cast<Constraint<RealType>*>(__m_function.get()))( t ) )
            return std::string();
        // Form error message
        std::ostringstream oss;
        oss << "constraint \"" << __m_docstr << "\" violated by value '" << stringer(t) << "'";
        return oss.str();
    }

    std::string docstr( void ) const {
        return __m_docstr;
    }

    virtual ~constraint_impl() {}

    const std::string           __m_docstr;
    const std::type_index       __m_index;
    const std::shared_ptr<void> __m_function;
};




////////////////////////////////////////////////
// Stuff that gets passed in by the usert can be
// any one of:

struct action_t              {};
struct name_t                {};
struct default_t             {};
struct docstring_t           {};
struct conversion_t          {};
struct constraint_template_t {};

struct constraint: public constraint_impl {
    using constraint_impl::constraint_impl;
};
struct precondition: public constraint_impl {
    using constraint_impl::constraint_impl;
};
struct postcondition: public constraint_impl {
    using constraint_impl::constraint_impl;
};



template <typename T>
struct provides {
    using real_type = typename std::decay<T>::type;
    template <typename U>
    struct test: std::is_base_of<real_type, typename std::decay<U>::type> {};
};
// Get all values from a tuple that provide Desired
template <typename Desired, typename T>
auto get_all(T&& t) ->
    decltype( functools::filter_t<provides<Desired>::template test>(std::forward<T>(t)) ) {
        return functools::filter_t<provides<Desired>::template test>(std::forward<T>(t));
}

struct docstr_getter_t {
    docstr_getter_t() {}
    docstr_getter_t(std::string const& pfx):
        __m_prefix(pfx)
    {}
    template <typename U>
    std::string operator()(U const& u) const {
        return __m_prefix+u.docstr();
    }
    template <typename U>
    std::string operator()(U const* u) const {
        return __m_prefix+u->docstr();
    }
    const std::string __m_prefix {};
};



template <typename T>
struct Default: default_t {
    using type = typename std::decay<T>::type;

    Default(type const& t):
        __m_default(t)
    {}

    std::string docstr( void ) const {
        return string_repr()( __m_default );
    }

    const type __m_default {};

    protected:
        Default() {}
};

template <typename T>
auto set_default(T&& t) -> Default<typename std::decay<T>::type> {
    return Default<typename std::decay<T>::type>(std::forward<T>(t));
}

auto set_default(char const*const t) -> Default<std::string> {
    return Default<std::string>( std::string(t) );
}

struct is_actual_default {
    template <typename T>
    struct test: std::integral_constant<bool, !std::is_same<typename std::decay<decltype(std::declval<T>().__m_default)>::type, ignore_t>::value> {};
};


template <typename T>
struct is_ok_default {
    // A default object may store its __m_default as const so we test 
    // on the equality of the decayed types - we're only interested
    // that the underlying types actually match
    template <typename U>
    struct test: std::integral_constant<bool, std::is_same<typename std::decay<decltype(std::declval<U>().__m_default)>::type,
                                                           typename std::decay<T>::type>::value> {};
};

// We can now apply "setting the default" over all defaults - it'll be 0
// or 1 defaults being set ...
struct default_setter_t {
    template <typename Defaulter, typename Option>
    void operator()(const Defaulter& def, Option& value) const {
        value->__m_value = def.__m_default;
        value->__m_count++; // indicate that the value was initialized
    }
};

struct default_constrainer_t {
    template <typename Defaulter, typename Constraints>
    void operator()(const Defaulter& def, Constraints&& constraints) const {
        try {
            std::forward<Constraints>(constraints)( def.__m_default );
        }
        catch( std::exception const& e ) {
            throw constraint_violation(std::string("The default violated a constraint: ")+e.what());
        }
    }
};


/////////////////////// Conversion from string to ... ///////////////
#define std_convert(tref, s, fn) \
    do { std::size_t  n; std::string es;\
         try { tref = fn(s, &n); } \
         catch ( std::exception const& e ) { es = e.what(); n = (std::size_t)-1; } \
         if( s.size() && n!=s.size() ) \
            throw std::runtime_error(std::string("Failed to completely convert the value '") + s + "' "+es); \
    } while( false );

// Converters from std::string => T
struct std_conversion_t: conversion_t {
    void operator()(int& t, std::string const& s) const {
        std_convert(t, s, std::stoi);
    }
    void operator()(unsigned int& t, std::string const& s) const {
        unsigned long tmp{};
        std_convert(tmp, s, std::stoul);
        if( tmp>std::numeric_limits<unsigned int>::max() ) {
            std::ostringstream oss;
            oss << "converted value '" << tmp << "' > unsigned int max";
            throw std::runtime_error(oss.str());
        }
        t = (unsigned int)tmp;
    }
    void operator()(long& t, std::string const& s) const {
        std_convert(t, s, std::stol);
    }
    void operator()(unsigned long& t, std::string const& s) const {
        std_convert(t, s, std::stoul);
    }
    void operator()(long long& t, std::string const& s) const {
        std_convert(t, s, std::stoll);
    }
    void operator()(unsigned long long& t, std::string const& s) const {
        std_convert(t, s, std::stoull);
    }
    void operator()(float& t, std::string const& s) const {
        std_convert(t, s, std::stof);
    }
    void operator()(double& t, std::string const& s) const {
        std_convert(t, s, std::stod);
    }
    void operator()(std::string& t, std::string const& s) const {
        t = s;
    }
    // Things that we ignore we ignore
    void operator()(ignore_t& , std::string const&) const { }
};



template <bool Value>
struct StoreFlag: action_t, Default<bool> {

    // Not all command lines have the stored type equal to the element type 
    using type         = bool;
    using element_type = ignore_t;

    StoreFlag(): Default<bool>(!Value), __m_value(Value) {}

    template <typename U>
    void operator()(type& value, U const&) const {
        value = __m_value;
    }
    const bool __m_value;
};

using store_true  = StoreFlag<true>;
using store_false = StoreFlag<false>;

template <typename T>
struct StoreConst: action_t {
    // We convert values to type T and also store those types
    using type         = typename std::decay<T>::type;
    using element_type = ignore_t;

    StoreConst() = delete;
    StoreConst(type const& t): __m_value(t) {}

    template <typename U>
    void operator()(type& value, U const&) const {
        value = __m_value;
    }
    const T __m_value;
};
template <typename T>
auto store_const(T const& t) -> StoreConst<T> {
    return StoreConst<T>(t);
}
auto store_const(char const *const t) -> StoreConst<std::string> {
    return StoreConst<std::string>( std::string(t) );
}

// count ignores the elements but we set a default of 0
// such that if nothing was counted we still get back the correct number :D
struct count: action_t, Default<unsigned int> {
    using type         = unsigned int;
    using element_type = ignore_t;

    count() : Default<unsigned int>(0u), __m_value(0) {}
    template <typename U>
    void operator()(type& value, U const&) const {
        __m_value = ++value;
    }
    mutable type __m_value;
};


// One can set a default for these buggers
template <typename T, template <typename...> class Container = std::list, typename... Details>
struct collect: action_t {
    // We store a container-of-T's but the command line arguments
    // get converted to element-of-what-we-store 
    using type         = Container<typename std::decay<T>::type, Details...>;
    using element_type = typename type::value_type;

    // We can only support containers where the value_type == T!
    static_assert(std::is_same<element_type, T>::value,
                  "Can only collect into containers that store elements of the requested type");

    template <typename U>
    void operator()(type& value, U const& u) const {
        value.insert(value.end(), u);
    }
};

// collect_into -> 
// T is an output iterator we can store converted values into
// We don't allow defaults to be set for this one
template <typename T>
struct Collector: action_t, Default<ignore_t> {
    using my_type      = std::reference_wrapper<T>;
    using type         = ignore_t;
    using element_type = typename std::decay<typename T::container_type::value_type>::type;

    Collector() = delete;
    Collector(my_type t): __m_ref(t) {}

    template <typename U>
    void operator()(type& , U const& u) const {
        *__m_ref.get()++ = u;
    }

    my_type __m_ref;
};

template <typename T,
          typename std::enable_if<functools::detail::is_output_iterator<T>::value, int>::type = 0>
auto collect_into(T& t) -> Collector<T> {
    return Collector<T>(std::ref(t));
}

template <typename T,
          typename std::enable_if<functools::detail::is_output_iterator<T>::value, int>::type = 0>
auto collect_into(std::reference_wrapper<T> t) -> Collector<T> {
    return Collector<T>(t);
}

// Support collecting into a container directly?
template <typename T>
struct ContainerCollector: action_t, Default<ignore_t> {
    using my_type      = std::reference_wrapper<T>;
    using type         = ignore_t;
    using element_type = typename std::decay<typename T::value_type>::type;

    ContainerCollector() = delete;
    ContainerCollector(my_type t): __m_ref(t) {}

    template <typename U>
    void operator()(type& , U const& u) const {
        __m_ref.get().insert(__m_ref.get().end(), u);
    }

    my_type __m_ref;

};
template <typename T,
          typename std::enable_if<can_insert<T>::value, int>::type = 0>
auto collect_into(T& t) -> ContainerCollector<T> {
    return ContainerCollector<T>(std::ref(t));
}
template <typename T,
          typename std::enable_if<can_insert<T>::value, int>::type = 0>
auto collect_into(std::reference_wrapper<T> t) -> ContainerCollector<T> {
    return ContainerCollector<T>(t);
}


template <typename T>
struct store_value: action_t {
    // We store the converted value
    using type         = typename std::decay<T>::type;
    using element_type = type;

    template <typename U>
    void operator()(type& value, U const& u) const {
        value = u;
    }
};

template <typename T>
struct StoreInto: action_t, Default<ignore_t> {
    using my_type      = std::reference_wrapper<T>;
    using type         = ignore_t;
    using element_type = typename std::decay<T>::type;

    StoreInto() = delete;
    StoreInto(my_type t): __m_ref(t) {}

    template <typename U>
    void operator()(type& , U const& u) const {
        __m_ref.get() = u;
    }

    my_type __m_ref;
};

template <typename T>
auto store_into(T& t) -> StoreInto<T> {
    return StoreInto<T>(std::ref(t));
}

template <typename T>
auto store_into(std::reference_wrapper<T> t) -> StoreInto<T> {
    return StoreInto<T>(t);
}

template <typename Function>
struct methodcaller_t: action_t {
    using type         = ignore_t;
    using element_type = ignore_t;

    methodcaller_t(Function const& f): __m_value( f ) {}

    template <typename T, typename U>
    void operator()(T t, U const&) const {
        (const_cast<methodcaller_t<Function>*>(this))->__m_value(t);
    }

    Function __m_value;
};

template <typename F>
auto mk_methodcaller(F&& f) -> methodcaller_t< typename std::decay<F>::type > {
    return methodcaller_t< typename std::decay<F>::type >(std::forward<F>(f));
}

auto print_usage( void ) ->
    decltype( mk_methodcaller( std::bind(std::mem_fn(&CmdLineBase::print_help), std::placeholders::_1, true)) ) {
    return mk_methodcaller( std::bind(std::mem_fn(&CmdLineBase::print_help), std::placeholders::_1, true));
}

auto print_help( void ) ->
    decltype( mk_methodcaller( std::bind(std::mem_fn(&CmdLineBase::print_help), std::placeholders::_1, false)) ) {
    return mk_methodcaller( std::bind(std::mem_fn(&CmdLineBase::print_help), std::placeholders::_1, false));
}



// implement docstring
struct docstring: std::string, docstring_t {
    using std::string::string;
    std::string const& docstr( void ) const {
        return *this;
    }
};


template <typename L, typename Category>
struct wrap_category: Category {
    using Left = typename std::decay<L>::type;
    using Category::Category;
};

// Constraint factory - can separate left/right types as long
// as "OP<Left>(left, Right())" is defined ...
template <typename L, typename Category, template <typename...> class OP,
          typename Left = typename std::decay<L>::type>
struct constraint_op: Category {
    using Result = wrap_category<Left, Category>;

    template <typename Right>
    static Result mk(Right const& right, std::string const& descr = std::string()) {
        return Result(build_string(descr, " ", op2str<OP>(), " ", right),
                      Constraint<Left>([=](Left const& left) { return OP<Left>()(left, right); }));
    }
};

template <typename L, typename Category>
struct constraint_fn: Category {
    using Left = typename std::decay<L>::type;
    using Self = constraint_fn<L, Category>;

    template <typename F>
    constraint_fn(F&& f, std::string const& descr):
        Category(descr, Constraint<Left>([=](Left const& left) { return f(left); }))
    {}

    template <typename F>
    static Self mk(F&& f, std::string const& descr) {
        return Self(Constraint<Left>([=](Left const& left) { return (bool)(f(left)); }), descr);
    }
};

template <typename T>
auto minimum_value(T const& t) -> typename constraint_op<T, constraint, std::greater_equal>::Result {
    return constraint_op<T, constraint, std::greater_equal>::mk(t, "minimum value");
}
template <typename T>
auto maximum_value(T const& t) -> typename constraint_op<T, constraint, std::greater_equal>::Result {
    return constraint_op<T, constraint, std::less_equal>::mk(t, "maximum value");
}

template <typename Container>
struct member_of_t {
    template <typename U>
    struct member_of_t_impl {
        bool operator()(U const& u, Container const& s) const {
            return s.find(u)!=s.end();
        }
    };
};
template <typename T>
auto is_member_of(std::initializer_list<T> il) ->
    typename constraint_op<T, constraint, member_of_t<std::set<T>>::template member_of_t_impl>::Result {
  return constraint_op<T, constraint, member_of_t<std::set<T>>::template member_of_t_impl>::mk(std::set<T>(il), "member of");
}

template <typename Category, template <typename...> class OP>
struct size_constrain: public constraint_template_t {
    using category = Category;

    template <typename U>
    struct size_constrain_t: Category {
        size_constrain_t(std::size_t limit, std::string const& msg):
            Category(msg, Constraint<U>([=](U const& u){ return OP<std::size_t>()(u.size(), limit); }))
        {};
    };

    template <typename U>
    size_constrain_t<U> mk( void ) const {
        return size_constrain_t<U>(__m_size, __m_docstr);
    }

    size_constrain(std::size_t n, std::string const& descr="size"):
        __m_size(n), __m_docstr( build_string(descr, " ", op2str<OP>(), " ", __m_size) )
    {}
    const std::size_t __m_size;
    const std::string __m_docstr;
};

using minimum_size = size_constrain<constraint, std::greater_equal>;
using maximum_size = size_constrain<constraint, std::less_equal>;
using exact_size   = size_constrain<constraint, std::equal_to>;


// Pre- and/or post conditons on the amount of times the command line
// option/argument may be present. The most usual constraints are:
//    0 or more   ("optional")
//    1 or more   ("required")
//    0 or 1      ("optional")
//
// which can easily implemented through the two primitives
// "at_most/at_least":
//
//    1. the default, with no constraints given, implies 0 or more
//    2. at least N implies N or more, thus "1 or more" is "at_least(1)"
//    3. at most N implies, well, 0 to N, thus "0 or 1" is "at_most(1)"
//    4. a range N->M can easily be formed by combining "at_least(N), at_most(M)"
//
auto at_least(unsigned int n) -> typename constraint_op<unsigned int, postcondition, std::greater_equal>::Result {
    return constraint_op<unsigned int, postcondition, std::greater_equal>::mk(n, "argument count");
}

// Note: at_most(n) means that the precondtion has to be "< n" because,
//       in fact, this could have been implemented as a post condition but 
//       then the action has already executed. The point of "at_most()"
//       should be to ensure that the action is /never/ executed more than
//       n times. So we turn it into a precondition on (n-1) such that the
//       action may proceed to exactly satisfy the post condition but no more.
auto at_most(unsigned int n) -> typename constraint_op<unsigned int, precondition, std::less>::Result {
    if( n<1 )
        throw std::logic_error("at_most() with requirement < 1 makes no sense at all.");
    return constraint_op<unsigned int, precondition, std::less>::mk(n, "argument count");
}

// For potential convenience there is also the exactly N, which could be 
// made out of the combination "at_least(N), at_most(N)" but that is 
// less efficient and less intuitive
template <typename T, template<typename...> class OP>
auto mk_op(std::string const& descr, T const& r) -> std::pair<std::string, Constraint<T>> {
    return std::make_pair(build_string(descr, " ", op2str<OP>(), " ", r), [=](T const& l) { return OP<T>()(l, r);});
}

template <typename L, typename... Category>
struct wrap_many: Category... {
    using Left = L;
    using Self = wrap_many<L, Category...>;

    // There's 4 stages to building this object
    // they're here in reverse order because the next one calls the previous
    // one.
    //
    // In execution order they are:
    //   1. "mk<Operators>(std::string const& s, Left const& right)"
    //      construct a tuple of pairs of <string, unary-function-on-Left>
    //      the string is the readable description of the operator,
    //      the unary function is an instantiation of Operator<Left> with
    //      right-hand-side fixed to `right`
    //   2. The tuple of pairs is 'zipped' into a tuple<Category...>
    //      by using parameter pack expansion; each Category is
    //      constructed as Category(get<Idx>().first, get<Idx>().second)
    //   3. The tuple of constructed Category objects is fed to the next
    //      maker, which unpacks that tuple again and feeds the unpacked
    //      Category... instances to the actual constructor of Self,
    //      which in turn calls the base-class constructors with those
    //      arguments, in order :-)
    //   I think it has to be done like this because AFAIK the parameter
    //   pack expansion (eg. on "Idx...") can only happen once. So for each
    //   level of unpacking you need to pass tuple+Idx...
    template <typename... Fs,
              typename std::enable_if<sizeof...(Fs)==sizeof...(Category), int>::type = 0>
    wrap_many(Fs&&... fs):
        Category(std::forward<Fs>(fs))...
    {}

    template <typename Cats, std::size_t... Idx>
    static auto mk3(Cats&& cats, functools::detail::index_sequence<Idx...>) -> Self {
        return Self( std::get<Idx>(std::forward<Cats>(cats))... );
    }
    template <typename Ops, size_t... Idx>
    static auto mk2(Ops&& ops, functools::detail::index_sequence<Idx...> idx) -> Self {
        return Self::mk3(std::tuple<Category...>( {std::get<Idx>(std::forward<Ops>(ops)).first,
                                                  std::get<Idx>(std::forward<Ops>(ops)).second}... ), idx);
    }
    template <template <typename...> class... OP>
    static auto mk(std::string const& descr, Left const& r) -> Self { 
        return Self::mk2(std::make_tuple(mk_op<Left, OP>(descr, r)...), typename functools::mk_sequence<0, sizeof...(OP)>::type());
    }
};


using exactly_t = wrap_many<unsigned int, precondition, postcondition>;
auto exactly(unsigned int n) -> exactly_t {
    // Exactly "0" makes as much sense as something that doesn't make sense at all
    if( n<1 )
        throw std::logic_error("exactly() with requirement < 1 makes no sense at all.");
    return exactly_t::mk<std::less, std::equal_to>("argument count", n);
}


// Be able to constrain on just about anything ...
template <typename T,
          typename std::enable_if<!std::is_bind_expression<T>::value, int>::type = 0,
          typename std::enable_if<is_unary_fn< deduce_signature<T> >::value, int>::type = 0,
          typename Arg = typename std::decay<typename std::tuple_element<0, typename deduce_signature<T>::argument_type>::type>::type,
          typename Ret = typename std::decay< typename deduce_signature<T>::return_type >::type>
auto constrain(T&& t, std::string const& descr = "lazy user did not supply a description") -> constraint_fn<Arg, constraint>  {
    // Whatever the constraint returns, we must be able to convert it to bool
    static_assert( std::is_convertible<Ret, bool>::value,
                   "A constraint's return value must be convertible to bool" );
    return constraint_fn<Arg, constraint>::mk(std::forward<T>(t), descr);
}

// And a cooked regex match constraint for string values
template <typename T, typename... Ts>
auto match(T&& t, Ts&&... ts) -> constraint_fn<std::string, constraint> {
    std::regex    rx( std::forward<T>(t), std::forward<Ts>(ts)... );
    return constraint_fn<std::string, constraint>(
            [=](std::string const& s) { return std::regex_match(s, rx); },
            std::string("match <<")+t+">>");
}




template <typename Ret>
struct user_conversion_t: conversion_t {
    using type = std::function<Ret(std::string const&)>;

    user_conversion_t() = delete;
    user_conversion_t(type const& t): __m_cvt(t) {}

    void operator()(Ret& r, std::string const& s) const {
        r = __m_cvt(s);
    }
    type __m_cvt;
};

template <typename T,
          typename std::enable_if<!std::is_bind_expression<T>::value, int>::type = 0,
          typename std::enable_if<is_unary_fn< deduce_signature<T> >::value, int>::type = 0,
          typename Arg = typename std::decay<typename std::tuple_element<0, typename deduce_signature<T>::argument_type>::type>::type,
          typename Ret = typename std::decay< typename deduce_signature<T>::return_type >::type>
auto convert(T&& t) -> user_conversion_t< Ret > {
    static_assert( std::is_same<Arg, std::string>::value,
                   "A converter's argument type must be std::string" );
    return user_conversion_t<Ret>( std::forward<T>(t) );
}

struct value_constraint_t: public constraint_impl {
    using constraint_impl::constraint_impl;
};

struct acceptable_short_name: constraint_fn<char, value_constraint_t> {
    acceptable_short_name():
        constraint_fn<char, value_constraint_t>([](char const& c) { return ::isalnum(c); }, "short name character ::isalnum(...)")
    {}
};

using minimum_size_v = size_constrain<value_constraint_t, std::greater_equal>;
struct acceptable_long_name: minimum_size_v {
    acceptable_long_name():
        minimum_size_v(2, "long name length")
    {}
};

struct CollectConstraints {
    template <typename T>
    static auto handle_result(T const& t) -> T {
        return t;
    }
};
struct ExecuteConstraints {
    template <typename T>
    static auto handle_result(T const& t) -> T {
        if( !t.empty() )
            throw constraint_violation(t);
        return t;
    }
};


template <typename Category, typename Policy>
struct constrainer {
    template <typename T, typename U>
    auto operator()(T const& t, U const& u) const -> decltype( Policy::handle_result(dynamic_cast<Category const&>(t).constrain(u)) ){
        return Policy::handle_result( dynamic_cast<Category const&>(t).constrain(u) );
    }
};

template <typename Category>
struct is_category {
    template <typename T>
    struct test: std::is_same<Category, typename T::category> {};
};

template <typename Requested>
struct instantiator {
    // Assume T is derived from constraint_template_t
    // then ask it to instantiate a real constraint for type Requested
    template <typename T>
    auto operator()(T const& t) const -> decltype( t.template mk<Requested>() ) {
        return t.template mk<Requested>();
    }
};

struct type_printer {
    template <typename T>
    void operator()(T&&) const {
        std::cout << "type: " << demangle_f<typename std::decay<T>::type>() << std::endl;
    }
};
struct type_printer2 {
    template <typename T>
    void operator()(T&& t) const {
        std::cout << "type: " << demangle_f<typename std::decay<T>::type>() << std::endl
                  << "      " << t << std::endl;
    }
};



struct case_insensitive_char_cmp {
    bool operator()(char l, char r) const {
        return ::toupper(l)==::toupper(r);
    }
};

// Sort names in descending length and then alphabetically
template <typename T>
struct name_sort {
    bool operator()(T const& l, T const& r) const {
        if( l.size()==r.size() )
            return std::lexicographical_compare(std::begin(l), std::end(l),
                                                std::begin(r), std::end(r), case_insensitive_char_cmp());
        return l.size() > r.size();
    }
};

// The collection of command line option names
using namecollection_t = std::set<std::string, name_sort<std::string>>;
using docstringlist_t  = std::list<std::string>;




template <typename Category, typename Element, typename Policy>
struct constraint_maker {
    using element_type = typename std::decay<Element>::type;
    using Self         = constraint_maker<Category, Element, Policy>;

    template <typename U>
    struct incompatible_element: std::integral_constant<bool, !std::is_same<element_type, typename U::Left>::value> {};
#if 0
    template <typename U>
    struct incompatible_element_t:
        std::integral_constant<bool,
                               !std::is_same<element_type,
                                             typename decltype( (Category&)std::declval<U>())::Left
                                             >::value> {}; 
#endif
    struct typecast {
        template <typename T>
        Category const* operator()(T const& t) const {
            return dynamic_cast<Category const*>(&t);
        }
    };
    template <typename Props>
    static ConstraintS<element_type> mk(docstringlist_t& docs, Props&& props) {
        // For all direct constraints, assert that the left type of the
        // operand is, in fact, Element
        auto   direct       = get_all<Category>( std::forward<Props>(props) );
        //auto  incompatible  = functools::filter_t<Self::incompatible_element>(direct);
        using  incompatible = typename functools::filter_p<Self::incompatible_element, decltype(direct)>::type;

#if 0
        std::cout << std::boolalpha << "CONSTRAINT MAKER" << std::endl
                  << "Category:     " << etdc::type2str<Category>() << std::endl
                  << "Element:      " << etdc::type2str<Element>() << std::endl
                  << "element_type: " << etdc::type2str<element_type>() << std::endl
                  << "Policy:       " << etdc::type2str<Policy>() << std::endl
                  << std::endl;
#endif
        static_assert( std::tuple_size<incompatible>::value==0,
                       "There is a type mismatch between given constraint(s) and the target type to constrain" );
        // Check if there are any constraint templates - for those we don't
        // have to check wether the Left type matches; we'll /instantiate/
        // them for the correct type :-)
        auto   templates    = get_all<constraint_template_t>( std::forward<Props>(props) );

        // Out of the templates, filter the ones that have the correct category
        auto   remain       = functools::filter_t<is_category<Category>::template test>(templates);
        // Transform those into actual constraints
        auto   instances    = functools::map(remain, instantiator<element_type/*Element*/>());
#if 0      
        //std::cout << "Constraint maker<" << etdc::type2str<Category>() << ", " << etdc::type2str<Element>() << ">" << std::endl;
        std::cout << "#direct:   " << std::tuple_size< decltype(direct) >::value << std::endl;
        functools::map(direct, type_printer());
        //std::cout << "#incompat: " << std::tuple_size< incompatible >::value << std::endl;
        std::cout << "#incompat: " << std::tuple_size< decltype(incompatible) >::value << std::endl;
        functools::map(incompatible, type_printer());
        std::cout << "#templates:" << std::tuple_size< decltype(templates) >::value << std::endl;
        functools::map(templates, type_printer());
        std::cout << "#remain:   " << std::tuple_size< decltype(remain) >::value << std::endl;
        functools::map(remain, type_printer());
        std::cout << "#instances:" << std::tuple_size< decltype(instances) >::value << std::endl;
        functools::map(instances, type_printer());
#if 0
        auto   allConstraints = std::tuple_cat(direct, instances);
        std::cout << "---> total:" << std::tuple_size< decltype(allConstraints) >::value << std::endl;
        std::list<std::string>  ls;
        functools::filter_v([](std::string const& s) {return !s.empty();},
                            functools::map(allConstraints, constrainer<Category, Policy>(), typename std::decay<Element>::type()),
                            std::back_inserter(ls));
        for(auto const& s: ls)
            std::cout << "Filtered string: " << s << std::endl;
#endif
#endif
        // Now that we have all constraints, we can build a function that
        // asserts all of them
        //return [=](typename std::decay<Element>::type const&) { return std::string(); };
        auto   allConstraints = std::tuple_cat(direct, instances);

        // Capture the docstrings, filtering out the ones that are empty
        //auto docbuilder = std::inserter(docs, docs.end());
        //functools::copy(functools::map(functools::map(allConstraints, typecast()), docstr_getter_t()), docbuilder);
        functools::filter_v(
                    [](std::string const& s){ return !s.empty(); },
                    functools::map(functools::map(allConstraints, typecast()), docstr_getter_t(demangle_f<Category>()+":")),
                    std::inserter(docs, docs.end()) );
#if 0
        std::cout << "---> total:" << std::tuple_size< decltype(allConstraints) >::value << std::endl;
#endif
        return ConstraintS<element_type>(
                [=](element_type const& value) {
                    std::ostringstream oss;
                    // Apply all constraints to the value and collect the violations
                    functools::filter_v([](std::string const& s){ return !s.empty(); },
                                        functools::map(allConstraints, constrainer<Category, Policy>(), value),
                                        std::ostream_iterator<std::string>(oss, ", "));
                    return oss.str();
                });
    }
};
// Specialize for ignore_t 
template <typename Category, typename Policy>
struct constraint_maker<Category, ignore_t, Policy> {
    template <typename Props>
    static ConstraintS<ignore_t> mk(docstringlist_t&, Props&&) {
        return [](ignore_t const&) { return std::string(); };
    }
};



template <typename T, typename... Props>
struct value_holder {
    // default c'tor is present but does not actually initialize
    value_holder() {}
    value_holder(T const& t): __m_value( t ) {
        // Apply the __m_value to all compile-time constraints
        docstringlist_t  tmp;
        auto constraintf = constraint_maker<value_constraint_t, T, ExecuteConstraints>::mk(tmp, std::tuple<Props...>() );
        constraintf( __m_value );
    }
    T  __m_value;
};

struct long_name: name_t, value_holder<std::string, acceptable_long_name> {
    // we cannot have default c'tor!
    long_name() = delete;
    using value_holder::value_holder;
};
struct short_name: name_t, value_holder<char, acceptable_short_name> {
    short_name() = delete;
    using value_holder::value_holder;
};





///////////////////////////////////////////////////////////////////////////////////////////////
//                            the command line objects
///////////////////////////////////////////////////////////////////////////////////////////////


// forward-declarartions so's we can have pointers-to
struct CmdLineOptionIF;
template <typename T> struct CmdLineOptionStorage;
using CmdLineOptionPtr = std::shared_ptr<CmdLineOptionIF>;

using ConstCmdLineOptionPtr = std::shared_ptr<CmdLineOptionIF const>;


// Non-templated baseclass
struct CmdLineOptionIF {
    using condition_f = std::function<void(unsigned int)>;

    friend class CmdLine;

    template <typename... Props>
    friend CmdLineOptionPtr mk_argument(CmdLineBase*, Props&&...);

    CmdLineOptionIF(): 
        __m_requires_argument( false ),
        __m_count( 0 ),
        __m_precondition_f( nullptr ), __m_postcondition_f( nullptr )
    {}

    virtual std::string docstring( void ) const {
        return std::string();
    }

    // Make sure we get a non-const lvalue reference
    // we don't want r-value refs
    template <typename T>
    bool get(T& t) const {
        // If we didn't parse anything yet then this is an error
        //if( !__m_parsed )
        //    throw std::logic_error("get() called before command line arguments have been parsed");

        // See if we can fulfill the request
        using theType                               = typename std::decay<T>::type;
        CmdLineOptionStorage<theType> const* upcast = dynamic_cast<CmdLineOptionStorage<theType> const*>(this);

        if( !upcast )
            throw std::runtime_error("Bad cast - requested option type is not actual type");
        return upcast->get(t);
    }

    // Process an actual command line argument
    virtual void processArgument(std::string const&) = 0;
    
    virtual ~CmdLineOptionIF() {}


    // We could protect these members but that wouldn't be a lot of use
    bool             __m_requires_argument;
    bool             __m_required;
    // pre-format the option's name(s), required/optional and, if applicable, type of argument:
    std::string      __m_usage; 
    unsigned int     __m_count;
    namecollection_t __m_names;
    docstringlist_t  __m_docstring;
    docstringlist_t  __m_defaults;
    docstringlist_t  __m_constraints;
    docstringlist_t  __m_requirements;

    protected:
        // If an option has a default then it can override
        // this one to set it
        virtual void set_default( void ) { }

        condition_f __m_precondition_f;
        condition_f __m_postcondition_f;
    private:
};


// We have to discriminate between two stages:
//   1. storing what was collected from the command line
//   2. processing the individual bits from the command line
// and they don't need to be equal ...
//
// To wit, the "collect" action collectes elements of type X 
// and stores them in type Y.
// Some actions ignore the value on the command line (Y==ignore)
// but still store some value of type X.
// Sometimes X == Y, for the simple store_value<Y> actions
//
// Default should apply to X, constraints to Y?
// (But suppose action = store const and user set default and constraints,
//  then Y==ignore then still need to apply constraint to default ...)
template <typename StoredType>
struct CmdLineOptionStorage: CmdLineOptionIF, value_holder<typename std::decay<StoredType>::type> {
    using type          = typename std::decay<StoredType>::type;
    using holder_type   = value_holder<type>;
    using default_f     = std::function<void(void)>;

    CmdLineOptionStorage():
        __m_default_f( nullptr )
    {}

    bool get(type& t) const {
        //std::cout << "CmdLineOption<T>::get()" << std::endl;
        if( __m_count==0 ) {
            //std::cout << "CmdLineOption<T>/ not present on command line, checking default" << std::endl;
            if( __m_default_f )
                __m_default_f();
        }
        //std::cout << "CmdLineOption<T>::get()/mcount=" << this->__m_count << std::endl;
        if( this->__m_count )
            t = this->holder_type::__m_value;
        return this->__m_count>0;
    }

    default_f   __m_default_f;
};

template <typename ElementType, typename StoredType>
struct CmdLineOption: CmdLineOptionStorage<typename std::decay<StoredType>::type> {

    using type          = typename std::decay<ElementType>::type;
    using holder_type   = CmdLineOptionStorage<typename std::decay<StoredType>::type>;
    using held_type     = typename holder_type::type;
    using constraint_f  = Constraint<type>;
    using process_arg_f = std::function<void(held_type&, std::string const&)>;

    CmdLineOption():
        __m_constraint_f( nullptr ),
        __m_process_arg_f( nullptr )
    {}

    void processArgument(std::string const& v) {
        //std::cout << "processing argument '" << v << "'" << std::endl;
        // Assert no preconditions are violated
        this->CmdLineOptionIF::__m_precondition_f( this->CmdLineOptionIF::__m_count );
        // Request the action to do it's thing
        __m_process_arg_f(this->holder_type::__m_value, v);
        // Chalk up another entry of this command line option
        this->CmdLineOptionIF::__m_count++;
        //std::cout << "__m_count = " << this->CmdLineOptionIF::__m_count << std::endl;
    }

    constraint_f   __m_constraint_f;
    process_arg_f  __m_process_arg_f;
};






///////////////////////////////////////////
// Depending on wether action ignores its
// argument or not return the correct
// processing function
//////////////////////////////////////////

template <typename U, typename V>
struct ignore_both:
    std::integral_constant<bool, std::is_same<typename std::decay<U>::type, typename std::decay<V>::type>::value &&
                                 std::is_same<typename std::decay<U>::type, ignore_t>::value>
{};
                                   

template <typename Value>
struct ignore_both_t {
    using type = std::function<void(Value&, std::string const&)>;

    template <typename Object, typename Action, typename Convert, typename Constrain>
    static type mk(const Object* o, Action const& action, Convert const&, Constrain const&) {
        return [=](Value&, std::string const&) {
                    // these actions ignore everything
                    action(o, std::cref(std::ignore));
                };
    }
};

template <typename Value>
struct ignore_argument_t {
    using type = std::function<void(Value&, std::string const&)>;

    template <typename Object, typename Action, typename Convert, typename Constrain>
    static type mk(const Object*, Action const& action, Convert const&, Constrain const& constrain) {
        return [=](Value& v, std::string const&) {
                    // these actions ingnore the value passed in but rather,
                    // they'll do something with the value stored in them
                    // so we better apply our constraints to that value
                    // before using it
                    constrain(action.__m_value);
                    // passed constraint, now apply
                    action(v, std::cref(std::ignore));
                };
    }
};

template <typename Value, typename Element>
struct use_argument_t {
    using type = std::function<void(Value&, std::string const&)>;

    template <typename Object, typename Action, typename Convert, typename Constrain>
    static type mk(const Object*, Action const& action, Convert const& convert, Constrain const& constrain) {
        return [=](Value& v, std::string const& s) {
                    Element tmp;
                    convert(tmp, s);
                    constrain(tmp);
                    action(v, tmp);
                };
    }
};

struct name_getter_t {
    template <typename U>
    std::string operator()(U const& u) const {
        return name_getter_t::to_str(u.__m_value);
    }

    static std::string to_str(std::string const& s) {
        return s;
    }
    static std::string to_str(char const& c) {
        return std::string(&c, &c+1);
    }
};

template <typename... Ts>
void fatal_error(std::ostream& os, std::string const& e, Ts&&... ts) {
    char  dummy[] = {(os << e << " ", 'a'), (os << std::forward<Ts>(ts), 'a')..., (os << std::endl, 'a')};
    (void)dummy;
    std::exit( EXIT_FAILURE );
}


template <typename... Props>
CmdLineOptionPtr mk_argument(CmdLineBase* cmdline, Props&&... props) {
    // get the action!
    auto allAction = get_all<action_t>( std::forward_as_tuple(props...) );
    static_assert( std::tuple_size<decltype(allAction)>::value==1, "You must specify exactly one Action" );

    // Once we know what the action is, we know:
    //    1. the type of the element(argument) it expects
    //    2. the type where to store the element(s)
    auto theAction                  = std::get<0>(allAction);
    using actionType                = decltype(theAction);
    using actionValue               = typename std::decay<typename actionType::type>::type;
    using actionElement             = typename std::decay<typename actionType::element_type>::type;

    // From the amount of names (short and/or long ones) we can infer wether
    // this is a command line option or an argument
    auto allNames                   = get_all<name_t>( std::forward_as_tuple(props...) );
    constexpr bool isArgument       = (std::tuple_size< decltype(allNames) >::value == 0);

    // Remember if the option takes an argument or not (element type == ignore_t)
    // De Morgan: !a && !b == !(a || b)
    constexpr bool requiresArgument = !(isArgument || std::is_same<actionElement, ignore_t>::value);

    // Depending on /that/ we can infer to which type to apply any
    // constraints; it's either applying a constraint o the action's stored
    // type (actionValue) or to the argument to the option (actionArgument)
    // Use the actionElement as type if an argument is required, otherwise
    // use the actionValue /UNLESS/ that is ignore_t in which case we fall
    // back to using the actionElement again
    using actionArgument =
        typename std::conditional<isArgument || requiresArgument,
                                  actionElement,
                                  typename std::conditional<std::is_same<actionValue, ignore_t>::value,
                                                            actionElement,
                                                            actionValue>::type
                                 >::type;
#if 0
    std::cout << std::boolalpha
              << "actionValue:      " << etdc::type2str<actionValue>() << std::endl
              << "actionElement:    " << etdc::type2str<actionElement>() << std::endl
              << "actionArg:        " << etdc::type2str<actionArgument>() << std::endl
              << "ignore_t:         " << etdc::type2str<ignore_t>() << std::endl
              << "ignore_t(decayed):" << etdc::type2str<typename std::decay<ignore_t>::type>() << std::endl
              << "ignore_both:      " << ignore_both<actionValue, actionElement>::value << std::endl
              << "isArgument:       " << isArgument << std::endl
              << "requiresArgument: " << requiresArgument << std::endl
              << std::endl;
#endif
    // Once we know what the action wants to store we can already
    // construct the appropriate command line option:
    auto optionPtr         = std::make_shared< CmdLineOption<actionArgument, actionValue> >();
    auto optionStore       = reinterpret_cast< CmdLineOptionStorage<actionValue>* >(optionPtr.get());
    auto optionIF          = reinterpret_cast< CmdLineOptionIF* >(optionPtr.get());

    // Collect all names and verify they don't collide
    // if no names were given this should be a command line argument
    // i.e. the option /is/ the value
    functools::copy(functools::map(allNames, name_getter_t()),
                    std::inserter(optionIF->__m_names, optionIF->__m_names.end()));

    /////////////////// Constraint handling /////////////////////////////
    //
    // Allow for any number of constraints to apply to an element's value
    // We must do this such that we can verify if e.g. a specified 
    // default violates these constraints
    //optionPtr->__m_constraint_f = mk_constrainer<constraint_t, actionArgument>( std::forward<Props>(props)... );
    auto DoConstrain =
        constraint_maker<constraint, actionArgument, ExecuteConstraints>::mk( optionIF->__m_constraints,
                                                                                std::forward_as_tuple(props...) );
    optionPtr->__m_constraint_f = Constraint<actionArgument>(
            [=](actionArgument const& value) {
                DoConstrain(value);
                return true;
            });

    /////////////////// Argument count constraints //////////////////////
    //
    // Allow for setting pre- and/or post conditions on the amount of
    // times the option is allowed
#if 1
    using argcount_t = decltype(optionIF->__m_count);
    auto DoPreCond  = constraint_maker<precondition,  argcount_t, ExecuteConstraints>::mk( optionIF->__m_requirements,
                                                                                             std::forward_as_tuple(props...) );
    auto DoPostCond = constraint_maker<postcondition, argcount_t, ExecuteConstraints>::mk( optionIF->__m_requirements,
                                                                                             std::forward_as_tuple(props...) );

    optionIF->__m_precondition_f  = [=](argcount_t v) { DoPreCond(v);  };
    optionIF->__m_postcondition_f = [=](argcount_t v) { DoPostCond(v); };
#endif
#if 0
    auto allPreConds  = get_all<precondition>( std::forward_as_tuple(props...) );
    auto allPostConds = get_all<postcondition>( std::forward_as_tuple(props...) );

    // Set the pre- and postcondition functions
    optionIF->__m_precondition_f =
        [=](unsigned int v) {
            functools::map(allPreConds, constrainer<precondition, ExecuteConstraints>(), v);
        };
    optionIF->__m_postcondition_f =
        [=](unsigned int v) {
            functools::map(allPostConds, constrainer<postcondition, ExecuteConstraints>(), v);
        };
#endif
    // a commandline option/argument is required if there is (at least one)
    // postcondition that fails with a count of 0
    try {
        optionIF->__m_postcondition_f(0);
        optionIF->__m_required = false;
    }
    catch( std::exception const& ) {
        optionIF->__m_required = true;
    }
    // Wether to include ellipsis; out of [0 or 1, 0 or more, 1 or more] 
    // the latter two need to have "..." to indicate "or more"
    // Test wether there is (a) precondition that fails on '1'
    // because if it does then the option is not allowed to be present more
    // than once
    std::string ellipsis;
    try {
        optionIF->__m_precondition_f(1);
        ellipsis = "...";
    }
    catch( std::exception const& ) { }

    /////////////////// Default handling /////////////////////////////
    //
    // Generate a function to set the default value, if one was supplied.
    // Make sure that at most one was given ...
    // Note that the default applies to the action's stored type not the 
    // action's argument type
    //
    auto allDefaults     = get_all<default_t>( std::forward_as_tuple(props...) );
    static_assert( std::tuple_size<decltype(allDefaults)>::value<=1, "You may specify one default at most" );

    // Now filter the ones whose actual default type is not ignore_t, which
    // just indicate that the user wasn't supposed to set a default ...
    auto actualDefaults  = functools::filter_t<is_actual_default::template test>( allDefaults );
    constexpr std::size_t nDefault = std::tuple_size< decltype(actualDefaults) >::value;

    // Allow only defaults to be set where the stored type == argument type?
    static_assert( nDefault==0 || std::is_same<actionArgument, actionValue>::value,
                   "You can only set defaults for options that store values, not collect them" );

    // Verify that any remaining defaults have the correct type
    //auto okDefaults      = functools::filter_t<is_ok_default<actionValue>::template test>( actualDefaults );
    auto okDefaults      = functools::filter_t<is_ok_default<actionArgument>::template test>( actualDefaults );

    static_assert( std::tuple_size<decltype(okDefaults)>::value==std::tuple_size<decltype(actualDefaults)>::value,
                   "The type of the default is incompatible with the type of the option" );

    // Verify that any defaults that are set do not violate any constraints
    functools::map(okDefaults, default_constrainer_t(), optionPtr->__m_constraint_f);

    // Now we can blindly 'map' the default setter over ALL defaults (well,
    // there's at most one ...). This function can be void(void) because
    // it requires no further inputs than everything we already
    // have available here ... and they don't violate any of the constraints
    optionStore->__m_default_f =
        [=](void) -> void {
            functools::map(okDefaults, default_setter_t(), optionPtr);
        };

    // Capture the defaults
    auto defbuilder = std::inserter(optionIF->__m_defaults, optionIF->__m_defaults.end());
    functools::copy(functools::map(okDefaults, docstr_getter_t()), defbuilder);


    /////////////////// Conversion handling /////////////////////////////
    //
    // Allow users to specify their own converter for string -> element
    // We append the built-in default conversion and take the first
    // element from the tuple, which is guaranteed to exists
    auto allConverters   = std::tuple_cat(get_all<conversion_t>( std::forward_as_tuple(props...) ),
                                          std::make_tuple(std_conversion_t()));

    // The user may specify up to one converter so the total number
    // of converters that we find must be >=1 && <=2
    constexpr std::size_t nConvert = std::tuple_size< decltype(allConverters) >::value;
    static_assert( nConvert>=1 && nConvert<=2,
                   "You may specify at most one user-defined converter");

    // Verify that the converter has an operator of the correct type?
    static_assert( !(isArgument || requiresArgument) ||
                   has_exact_operator<typename std::tuple_element<0, decltype(allConverters)>::type const,
                                      void, actionArgument&, std::string const&>::value,
                   "The converter can not convert to the requested value of the action" );

    // We need to build a function that takes a string and executes the
    // action with the converted string
    optionPtr->__m_requires_argument = requiresArgument;

    // Deal with documentation - we may add to this later
    auto allDocstr     = get_all<docstring_t>( std::forward_as_tuple(props...) );
    auto docstrbuilder = std::inserter(optionIF->__m_docstring, optionIF->__m_docstring.end());

    functools::copy(functools::map(allDocstr, docstr_getter_t()), docstrbuilder);
    // we may add docstr()'d constraints, defaults and whatnots.

    // Now we can pre-format the option's usage
    //  [...]   if not required
    //   -<short name> --long-name [<type>]  
    //      (type only appended if argument required)
    //   + ellipsis to indicate "or more"
    //
    unsigned int       n( 0 );
    std::ostringstream usage;

    // Names are sorted by reverse size
    for(auto const& nm: reversed(optionIF->__m_names))
        usage << (n++ ? " " : "") << (nm.size()==1 ? "-" : "--") << nm;
    if( optionIF->__m_requires_argument || isArgument )
        usage << (n ? " " : "") << "<" << optiontype<actionElement>() << ">" << ellipsis;

    optionIF->__m_usage = usage.str();
    if( !optionIF->__m_required )
        optionIF->__m_usage = "[" + optionIF->__m_usage + "]";

    /////////////////// The actual action builder /////////////////////////////
    //
    // Depending on wether we actually need to look at the (converted)
    // value we choose the correct action maker
    using actionMaker = typename 
        std::conditional<ignore_both<actionValue, actionElement>::value,
                         ignore_both_t<actionValue>, 
                         typename std::conditional<isArgument || requiresArgument,
                                                   use_argument_t<actionValue, actionArgument>,
                                                   ignore_argument_t<actionValue>
                                                  >::type
                        >::type;

    // Pass the zeroth element of the converters into the processing
    // function - it's either the user's one or our default one
    optionPtr->__m_process_arg_f = actionMaker::mk(cmdline, theAction, std::get<0>(allConverters), optionPtr->__m_constraint_f);
    return optionPtr;
}

// support stuff
struct lt_cmdlineoption {
    // The command line option's __m_names have already been sorted for us
    // we sort the command line options by longest name, alphabetically
    bool operator()(CmdLineOptionPtr const& l, CmdLineOptionPtr const r) const {
        if( l->__m_names.empty() || r->__m_names.empty() )
            ::fatal_error(std::cerr, "no names found whilst comparing cmdlineoptions for ",
                                     (l->__m_names.empty() ? "left " : ""), (r->__m_names.empty() ? "right " : ""),
                                     "side of the comparison");
        // We /only/ want lexicographical compare on the names
        auto const&  left  = *std::begin(l->__m_names);
        auto const&  right = *std::begin(r->__m_names);
        return std::lexicographical_compare(std::begin(left), std::end(left),
                                            std::begin(right), std::end(right), case_insensitive_char_cmp());
    }
};


template <typename C, typename Iter>
void maybe_print(std::string const& topic, C const& c, Iter& iter) {
    if( !c.empty() )
        std::copy(std::begin(c), std::end(c), *iter++ = topic);
}





/////////////////////////////////////////////////////////////////////////////////////
//
//      _Finally_ we get to the actual command line class ...
//
/////////////////////////////////////////////////////////////////////////////////////

class CmdLine: public CmdLineBase {

    public:
        // Collect version + docstr(explanation of program)
        CmdLine():
            __m_parsed( false )
        {}

        template <typename T>
        bool get(std::string const& opt, T& t) const {
            if( !__m_parsed )
                ::fatal_error(std::cerr, "Cannot request value if no command line options have been parsed yet.");

            auto option = __m_option_idx_by_name.find(opt);
            if( option==__m_option_idx_by_name.end() )
                ::fatal_error(std::cerr, std::string("No option by the name of '")+opt+"' defined.");
            return option->second->get(t);
        }

        void parse(int, char const*const*const argv) {
            if( __m_parsed )
                ::fatal_error(std::cerr, "Cannot double parse a command line");
            __m_parsed = true;

            // Step 1. Transform into list of strings 
            char const*const*       option( argv ? argv+1 : nullptr);
            std::list<std::string>  options;
            auto                    argptr = std::back_inserter(options);
       
            // NOTE:
            //  could support "<prog> ... options ... -- <verbatim>"
            //  such that everything after a literal '--' gets passed
            //  through verbatim and is made available as "arguments()"
            while( option && *option ) {
                // starts with "--"?
                if( ::strncmp(*option, "--", 2)==0 && ::strlen(*option)>2 ) {
                    // assume it is either:
                    //    --<long>
                    //    --<long>=<value>
                    char const*const equal = ::strchr((*option)+2, '=');
                    if( equal ) {
                        *argptr++ = std::string(*option, equal);
                        *argptr++ = std::string(equal+1);
                    } else {
                        *argptr++ = std::string(*option);
                    }
                // starts with "-"?
                } else if( **option=='-' && ::strlen(*option)>1 ) {
                    // Assume it's a short-name (single character) option.
                    // (Or a collection thereof). Expand a single "-XYZ"
                    // into "-X -Y -Z"
                    char const*  flags = (*option)+1;
                    while( *flags )
                        *argptr++ = std::string("-")+*flags++;
#if 0
                    while( *flags ) {
                        *argptr++ = std::string(flags, flags+1);
                        flags++;
                    }
#endif
                } else {
                    // Just add verbatim
                    *argptr++ = std::string(*option);
                }
                option++;
            }

            // Now go through all the expanded thingamabobs
            CmdLineOptionPtr   previous = nullptr;
            for(auto const& opt: options) {
                try {
                    // If previous is non-null it means it was waiting for
                    // an argument, now we have it
                    if( previous ) {
                        previous->processArgument( opt );
                        previous = nullptr;
                        continue;
                    }
                    // If the thing starts with '-' look for the option "-(-)<stuff",
                    // otherwise for the thing with the empty name
                    auto curOpt = (opt[0]=='-' ? __m_option_idx_by_name.find(opt.substr(opt.find_first_not_of('-'))) :
                                                 __m_option_idx_by_name.find(std::string()));
                    //std::cout << "Looking for '" << opt << "'" << std::endl;
                    if( curOpt==__m_option_idx_by_name.end() ) {
                        this->print_help(true);
                        ::fatal_error(std::cerr, "\nUnrecognized command line option ", opt);
                    }
                    // Check wether the current option requires an argument
                    if( curOpt->second->__m_requires_argument )
                        previous = curOpt->second;
                    else
                        curOpt->second->processArgument( opt );
                }
                catch( std::exception& e ) {
                    this->print_help(true);
                    //::fatal_error(std::cerr, "whilst processing", opt, std::endl, e.what(), std::endl);
                    ::fatal_error(std::cerr, e.what(), opt);
                }
                catch( ... ) {
                    this->print_help(true);
                    //::fatal_error(std::cerr, "unknown exception whilst processing ", opt, std::endl);
                    ::fatal_error(std::cerr, "unknown exception whilst processing ", opt);
                }
            }
            // If we end up here with previous non-null there's a missing
            // argument!
            if( previous ) {
                this->print_help(true);
                //::fatal_error(std::cerr, "whilst processing", opt, std::endl, e.what(), std::endl);
                ::fatal_error(std::cerr, "Missing argument to option ", *previous->__m_names.begin());
            }
            // And finally, test all post conditions!
            for(auto const& opt: __m_option_by_alphabet ) {
                try {
                    opt->__m_postcondition_f( opt->__m_count );
                }
                catch( std::exception& e ) {
                    this->print_help(true);
                    //::fatal_error(std::cerr, "whilst processing", opt, std::endl, e.what(), std::endl);
                    ::fatal_error(std::cerr, e.what(), *opt->__m_names.begin());
                }
                catch( ... ) {
                    this->print_help(true);
                    //::fatal_error(std::cerr, "unknown exception whilst processing ", opt, std::endl);
                    ::fatal_error(std::cerr, "unknown exception whilst verifying post condition for ", *opt->__m_names.begin());
                }
            }
        }

        virtual void print_help( bool usage ) const {
            std::ostream_iterator<std::string> printer(std::cout, " ");
            std::ostream_iterator<std::string> lineprinter(std::cout, "\n\t\t");
            std::cout << "Print " << (usage ? "usage" : "help") << std::endl;
            ConstCmdLineOptionPtr   argument = nullptr;

            for(auto const& opt: __m_option_by_alphabet) {
                if( opt->__m_names.begin()->empty() ) {
                    argument = opt;
                    continue;
                }
                *printer++ = opt->__m_usage;
                if( usage )
                    continue;

                // Print details!
                std::cout << std::endl;
                maybe_print("\r\tDescription:",  opt->__m_docstring, lineprinter);
                maybe_print("\r\tDefaults:",     opt->__m_defaults, lineprinter);
                maybe_print("\r\tConstraints:",  opt->__m_constraints, lineprinter);
                maybe_print("\r\tRequirements:", opt->__m_requirements, lineprinter);
                std::cout << "\r";
            }
            if( argument ) {
                *printer++ = argument->__m_usage;

                if( !usage ) {
                    std::cout << std::endl;
                    maybe_print("\r\tDescription:",  argument->__m_docstring, lineprinter);
                    maybe_print("\r\tDefaults:",     argument->__m_defaults, lineprinter);
                    maybe_print("\r\tConstraints:",  argument->__m_constraints, lineprinter);
                    maybe_print("\r\tRequirements:", argument->__m_requirements, lineprinter);
                }
            }
            std::cout << std::endl;
        }
        virtual void print_version( void ) const {
            std::cout << "Version: " << 0 << std::endl;
        }

        template <typename... Props>
        void add(Props&&... props) {
            if( __m_parsed )
                ::fatal_error(std::cerr, "Cannot add command line arguments after having already parsed one.");

            auto new_arg = ::mk_argument(this, std::forward<Props>(props)...);

            // Verify that none of the names of the new option conflict with
            // already defined names. If the option has no names at all it
            // is an argument. 
            // Note that we are NOT nice here. Any error here doesn't throw
            // an exception but terminates the program.
            if( new_arg->__m_names.empty() )
                if( !new_arg->__m_names.insert(std::string()).second )
                    ::fatal_error(std::cerr, "Failed to insert empty string in names for command line argument description");

            for(auto const& nm: new_arg->__m_names )
                if( __m_option_idx_by_name.find(nm)!=__m_option_idx_by_name.end() )
                    ::fatal_error(std::cerr, "Duplicate command line", (nm.empty() ? "argument" : ("option '"+nm+"'")));
            // Now that we know that none of the names will clash we can add
            // this option to the set of options, alphabetically sorted by
            // longest name ...
            if( !__m_option_by_alphabet.insert(new_arg).second )
                ::fatal_error(std::cerr, "Failed to insert new elemen into alphabetic set", *new_arg->__m_names.begin());

            // OK register the option under all its names - we've
            // verified that it doesn't clash
            for(auto const& nm: new_arg->__m_names )
                if( !__m_option_idx_by_name.emplace(nm, new_arg).second )
                    ::fatal_error(std::cerr, "Failed to insert new element into index by name", *new_arg->__m_names.begin());
        }

    private:
        // Keep the command line options in a number of data structures.
        // Depending on use case - listing the options or finding the
        // correct option - some data structures are better than others.
        // 1. In a simple set<options> sorted alphabetically on the longest
        //    name so printing usage/help is easy
        // 2. in an associative array mapping name -> option such that
        //    irrespective of under how many names an option was registered
        //    we can quickly find it
        using option_idx_by_name = std::map<std::string, CmdLineOptionPtr>;
        using option_by_alphabet = std::set<CmdLineOptionPtr, lt_cmdlineoption>;

        bool                  __m_parsed;
        option_idx_by_name    __m_option_idx_by_name;
        option_by_alphabet    __m_option_by_alphabet;
};



template <typename T>
void print_docstr(T const& t) {
    std::cout << "<<< docstringlist: " << std::endl;
    for(auto const& s: t->__m_docstring)
        if( !s.empty() )
            std::cout << s << std::endl;
    std::cout << ">>>" << std::endl;
}


struct my_converter: conversion_t {
    const std::regex __m_rxsep{","};
    // split string at "," and produce list-of-strings
    void operator()(std::list<std::string>& t, std::string const& s) const {
        t.clear();
        std::copy( std::sregex_token_iterator(std::begin(s), std::end(s), __m_rxsep, -1),
                   std::sregex_token_iterator(), std::back_inserter(t) );
    }
};

std::size_t stringsize(std::string const& s) {
    return s.size();
}

int main(int argc, char*const*const argv) {
    CmdLine                cmd;
    std::list<std::string> experiments;
    auto                   experimentor = std::back_inserter(experiments);

    cmd.add(long_name("help"), short_name('h'), print_help(),
            docstring("Prints help and exits succesfully"));

    cmd.add(short_name('f'), store_value<std::string>(), exactly(1));

    cmd.add(set_default(3.14f), long_name("threshold"),
            maximum_value(7.f), store_value<float>(), at_least(2));

    cmd.add(long_name("exp"), collect_into(experimentor),
            minimum_size(4), match("[a-zA-Z]{2}[0-9]{3}[a-zA-Z]?"));

    cmd.add(collect_into(experiments));

    cmd.parse(argc, argv);

    std::cout << ">>>>>>>>>>>>>>>" << std::endl;
    float         threshold;
    std::string   f;

    cmd.get("f", f);
    cmd.get("threshold", threshold);

    std::cout << "got '-f' = " << f << std::endl;
    std::cout << "got '--threshold' = " << threshold << std::endl;

    for(auto const& e: experiments)
        std::cout << "Experiment: " << e << std::endl;
#if 0
    std::cout << "Manually printing usage: " << std::endl;
    cmd.print_help(true);
    std::cout << std::endl << "Manually printing help: " << std::endl;
    cmd.print_help(false);
#endif
    return 0;
}

#if 0
int main( void ) {
    CmdLine   cmd;
    std::cout << std::boolalpha;
#if 1
    std::cout << "main/print_usage" << std::endl;
    auto o00 = mk_argument(&cmd, short_name('h'), print_usage());
    std::cout << "main/print_help" << std::endl;
    auto o01 = mk_argument(&cmd, long_name("help"), print_help(), exactly(1));
    std::cout << "main: start processing cmd line arguments" << std::endl;
    o00->processArgument("x");
    o01->processArgument("x");
    o01->processArgument("x");
    o01->processArgument("x");
#endif

#if 0
    std::cout << "main/option store_true" << std::endl;
    auto o1 = mk_argument(nullptr, short_name('x'), long_name("xyz"), store_true() );
    std::cout << "  needs argument: " << o1->__m_requires_argument << std::endl;
    print_docstr(o1);
    o1->processArgument("x");
    bool b;
    o1->get(b);
    std::cout << "b = " << b << std::endl;
#endif

#if 0
    std::cout << "main/option count/w name" << std::endl;
    auto o2 = mk_argument(nullptr, short_name('R'), count()/*, set_default(42)*/, long_name("rpath") );
    std::cout << "  needs argument: " << o2->__m_requires_argument << std::endl;
    print_docstr(o2);
    unsigned int n;
    o2->processArgument("x");
    o2->processArgument("y");
    o2->get(n);
    std::cout << "n = " << n << std::endl;
#endif

#if 0
    std::cout << "main/option store_const" << std::endl;
    auto o3 = mk_argument(nullptr, long_name("version"), store_const(666)/*, short_name('x')*/, set_default(3), is_member_of({1,3,666}));
    print_docstr(o3);
    std::cout << "  needs argument: " << o3->__m_requires_argument << std::endl;
    int  v;
    o3->processArgument("z");
    o3->get(v);
    std::cout << "version=" << v << std::endl;
#endif

#if 0
    std::cout << "main/option store_const / string" << std::endl;
    auto o4 = mk_argument(nullptr, long_name("version_s"), store_const("aap"), set_default("noot"), maximum_size(6));
    std::cout << "  needs argument: " << o4->__m_requires_argument << std::endl;
    print_docstr(o4);
    std::string sv;
    o4->processArgument("foobar");
    o4->get(sv);
    std::cout << "sv = " << sv << std::endl;
#endif

#if 0
    std::cout << "main/option named store_value (convert-to-float)" << std::endl;
    auto o5 = mk_argument(nullptr, store_value<float>(), set_default(-2.0f), maximum_value(3.14f), long_name("float"),
                           docstring("Set weight threshold") );
    std::cout << "  needs argument: " << o5->__m_requires_argument << std::endl;
    print_docstr(o5);
    float  fv;
    o5->processArgument("-3");
    //o5->processArgument("3.15");
    o5->get(fv);
    std::cout << "fv = " << fv << std::endl;
#endif

#if 0
    std::cout << "main/option named store_value with user-defined-conversion (convert-to-list)" << std::endl;
    std::list<std::string>  mls;

    auto o5a = mk_argument(nullptr, store_into(mls), short_name('q'), long_name("qq"),
                           //my_converter(),
                           //constrain(stringsize),
                           constrain([](std::list<std::string> const& l) { return l.size(); }),
                           convert([](std::string const& s) {
                                    std::regex             rxSep{","};
                                    std::list<std::string> tmp;
                                    std::copy( std::sregex_token_iterator(std::begin(s), std::end(s), rxSep, -1),
                                               std::sregex_token_iterator(), std::back_inserter(tmp) );
                                    return tmp; }),
                           minimum_size(3), docstring("Set list (csv)") );
    std::cout << "  needs argument: " << o5a->__m_requires_argument << std::endl;
    print_docstr(o5a);

    //o5a->processArgument("noot");
    o5a->processArgument("-3,aap,noot");
    for(auto const& sel: mls)
        std::cout << "Element: " << sel << std::endl;
#endif
    //std::cout << "main/option anonymous count" << std::endl;
    //mk_argument(nullptr, count() /*, set_default(42)*/ );

#if 0
    std::cout << "main/option anonymous store_value (convert-to-int)" << std::endl;
    auto o6 = mk_argument(nullptr, store_value<unsigned int>()/*, set_default(2)*/, maximum_value(-1u) );
    std::cout << "  needs argument: " << o6->__m_requires_argument << std::endl;
    print_docstr(o6);
    unsigned int iv;
    o6->processArgument("2");
    o6->get(iv);
    std::cout << "iv = " << iv << std::endl;
#endif

#if 0
    std::cout << "main/option anonymous store_into (convert-to-int)" << std::endl;

    int   vlag;
    auto o6a = mk_argument(nullptr, store_into(vlag)/*, set_default(2)*/, maximum_value(-1), docstring("set vlag") );
    std::cout << "  needs argument: " << o6a->__m_requires_argument << std::endl;
    print_docstr(o6a);
    o6a->processArgument("-2");
    std::cout << "vlag = " << vlag << std::endl;
#endif

#if 0
    std::cout << "main/option named collect_into (convert-to-int)" << std::endl;
    std::list<int>  iList;
    auto            inserter = std::back_inserter(iList);
    auto o7 = mk_argument(nullptr, short_name('v'), collect_into(std::ref(inserter))/*, set_default(2)*/, minimum_value(1) );
    std::cout << "  needs argument: " << o7->__m_requires_argument << std::endl;
    print_docstr(o7);
    o7->processArgument("3");
    o7->processArgument("3");
    o7->processArgument("7");
    for(auto const& i: iList)
        std::cout << "List element: " << i << std::endl;
#endif

#if 0
    std::set<std::string>  iSet;
    auto            sinserter = std::inserter(iSet, iSet.end());
    std::cout << "main/option anonymous collect_into (convert-to-string)" << std::endl;
    auto o8 = mk_argument(nullptr, collect_into(std::ref(sinserter))/*, set_default(2)*/, maximum_size(4) );
    std::cout << "  needs argument: " << o8->__m_requires_argument << std::endl;
    print_docstr(o8);
    o8->processArgument("3");
    o8->processArgument("3308");
    o8->processArgument("3308");
    for(auto const& i: iSet)
        std::cout << "Set element: " << i << std::endl;
#endif

#if 0
    std::cout << "main/option named collect (convert-to-string)" << std::endl;
    auto o9 = mk_argument(nullptr, short_name('f'), collect<std::string, std::set>(), maximum_size(4) );
    std::cout << "  needs argument: " << o9->__m_requires_argument << std::endl;
    print_docstr(o9);
    o9->processArgument("3");
    o9->processArgument("3308");
    o9->processArgument("3308");
    std::set<std::string>  ls;
    o9->get(ls);
    for(auto const& s: ls)
        std::cout << "element: " << s << std::endl;
#endif

#if 0
    std::cout << "main/option anonymous collect (convert-to-string)" << std::endl;
    auto o10 = mk_argument(nullptr, collect<std::string, std::set>(),
                           constrain([](std::string const& s) { return s.size(); }), //minimum_size(1),
                           constrain([](std::string const& s) { return std::count(std::begin(s), std::end(s), '3')<=2; },
                                     "at most 2 threes in string") );
                                            // clang bug? capture nothing
                                            // but still warn about shadowed
                                            // variable 'n'!
                                            //const std::size_t n = std::count(std::begin(s), std::end(s), '3');
    std::cout << "  needs argument: " << o10->__m_requires_argument << std::endl;
    print_docstr(o10);
    o10->processArgument("3");
    //o10->processArgument("");
    o10->processArgument("3308");
    o10->processArgument("33083");
    std::set<std::string>  ls2;
    o10->get(ls2);
    for(auto const& s: ls2)
        std::cout << "element: " << s << std::endl;
    auto o11 = mk_argument(nullptr, collect<int, std::vector>() );
    print_docstr(o11);
#endif

#if 0
    std::cout << "main/option named store map" << std::endl;
    auto o12 = mk_argument(nullptr, store_value<std::map<int, std::string>>(),
                           minimum_size(2),
                           convert([](std::string const& s) {
                                int                        i = 0;
                                std::regex                 rxSep{","};
                                std::list<std::string>     tmp;
                                std::map<int, std::string> ms;
                                std::copy( std::sregex_token_iterator(std::begin(s), std::end(s), rxSep, -1),
                                           std::sregex_token_iterator(), std::back_inserter(tmp) );
                                for(auto const& el: tmp)
                                    ms.insert( std::make_pair(i++, el) );
                                return ms;
                            }));
    o12->processArgument("3308,12");
    o12->processArgument("33083");
    std::map<int, std::string> ms2;
    o12->get(ms2);
    for(auto const& mel: ms2)
        std::cout << "Map element: " << mel.first << " => " << mel.second << std::endl;
#endif
    return 0;
}
#endif
