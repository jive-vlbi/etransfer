// Random utilities
// Copyright (C) 2007-2016 Harro Verkouter
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
// 
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE.  See the GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
// 
// Author:  Harro Verkouter - verkouter@jive.eu
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#ifndef ETDC_UTILITIES_H
#define ETDC_UTILITIES_H

// std c++
#include <map>
#include <tuple>
#include <memory>
#include <string>
#include <sstream>
#include <iterator>
#include <stdexcept>
#include <type_traits>

// plain old C
#include <string.h>
#include <stdlib.h>
#include <cxxabi.h>

namespace etdc {

    // Simple wrapper template to zero-initialize C-style structs which
    // makes them still usable as the C-structs that they are:
    //
    // in stead of:
    //      struct glob_t  g;
    //      ::memset(&g, 0, sizeof(glob_t));
    //
    //      ...
    //      for(size_t i = 0; i<g.gl_pathc; ...)
    //
    // do:
    //      Zero<struct glob_t> g;
    //
    //      ...
    //      for(size_t i=0; i<g.gl_pathc; ...)
    //
    template <typename T>
    struct Zero: public T {
        Zero() {
            // GCC 6 does not like this - i.e. overwriting oneself 
            //       using memset. Compiler sais that we're not trivial
            //       thus may have a vtable thus it sais we should
            //       use member assignment or c'tor to zero struct out
            ::memset(static_cast<void*>(const_cast<Zero<T>*>(this)), 0x0, sizeof(T));
        }
        // This creates a zeroed-out region of memory
    	static T* mk( void ) {
            T*  rv = new T;
            ::memset(static_cast<void*>(rv), 0x0, sizeof(T));
            return rv;
        }
    };

    // Sometimes you just *have* to be able to get the (demangled) name from
    // "typeid(T).name()" so as to know what the **** 'T' happens to be.
    namespace detail {
        // http://stackoverflow.com/a/4541470
        template <typename T>
        std::string demangle(void) {
            int              status = -4; // some arbitrary value to eliminate the compiler warning
            char const*const name = typeid(T).name();

            // enable c++11 by passing the flag -std=c++11 to g++
            std::unique_ptr<char, void(*)(void*)> res {
                abi::__cxa_demangle(name, NULL, NULL, &status),
                std::free
            };

            return (status==0) ? res.get() : name ;
        }
    }

    // can use as "type2str<X, Y<Z>, ....>()" and get a comma-separated
    // string of demangled types
    template <typename... Ts>
    std::string type2str( void ) {
        std::string dummy[sizeof...(Ts)] = { detail::demangle<Ts>()... };
        std::ostringstream oss;
        std::copy(&dummy[0], &dummy[sizeof...(Ts)], std::ostream_iterator<std::string>(oss, ";"));
        return oss.str();
    }


    // Adaptor for easy creation of reversed range-based for loop, thanks to:
    // http://stackoverflow.com/a/36928761
    namespace detail {
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
    detail::reverse_wrapper<C> reversed(C & c) {
        return detail::reverse_wrapper<C>(c);
    }


    // Helpers for enabling/disabling certain flavours of contstructors
    namespace detail {
        // From: http://stackoverflow.com/a/9407521
        //       "Determine if a type is an STL container at compile time"
        template<typename T>
        struct has_const_iterator
        {
            private:
                typedef char                      yes;
                typedef struct { char array[2]; } no;

                template<typename C> static yes test(typename C::const_iterator*);
                template<typename C> static no  test(...);
            public:
                static const bool value = sizeof(test<T>(0)) == sizeof(yes);
                typedef T type;
        };

        template <typename T>
        struct has_begin_end {
            template<typename C> static char (&f(typename std::enable_if<
              std::is_same<decltype(static_cast<typename C::const_iterator (C::*)() const>(&C::begin)),
              typename C::const_iterator(C::*)() const>::value, void>::type*))[1];

            template<typename C> static char (&f(...))[2];

            template<typename C> static char (&g(typename std::enable_if<
              std::is_same<decltype(static_cast<typename C::const_iterator (C::*)() const>(&C::end)),
              typename C::const_iterator(C::*)() const>::value, void>::type*))[1];

            template<typename C> static char (&g(...))[2];

            static bool const beg_value = sizeof(f<T>(0)) == 1;
            static bool const end_value = sizeof(g<T>(0)) == 1;
            static bool const value     = beg_value && end_value;
        };

        // mapped types in STL containers have a key_type typedef.
        // this is important for looking up values in any container:
        //  * non-associative containers: use std::find()
        //  * associative containers: use .find(key)
        template <typename T>
        struct has_key_type {
            using yes = char;
            using no  = decltype(nullptr);

            template <typename U> static auto test(typename U::key_type*) -> yes;
            template <typename U> static auto test(U*)                    -> no;

            static const bool value = (sizeof(test<T>(nullptr))==sizeof(yes));
            static_assert(sizeof(yes)!=sizeof(no), "Choose a slightly different type for yes or no");
        };
    } // namespace detail

    // Let's put the useful primitive just in etdc in stead of etdc::detail
    template<typename T> 
    struct is_container :
        std::integral_constant<bool, detail::has_const_iterator<T>::value && detail::has_begin_end<T>::value> 
    {};

    ///////////////////////////////////////////////////////////////////
    //                'function application'
    ///////////////////////////////////////////////////////////////////
    template <template <typename...> class Function, typename... Ts>
    struct apply {
        using type = std::tuple<typename Function<Ts>::type...>;
    };


    /////////////////////////////////////////////////////////////////////////////////////////
    //
    //                (Helper) stuff for getting the index of type T in a
    //                std::tuple<> or just in a parameter pack
    //
    /////////////////////////////////////////////////////////////////////////////////////////

    struct requested_element_not_found {};
    template <typename T>
    struct requested_type_not_found {};

    namespace detail {
        // 'end' case: type T not found in pack or tuple 
        template <typename T, std::size_t I, typename...>
        struct index_of_impl {};

        template <typename T, std::size_t I>
        struct index_of_impl<T, I> {
                using  type = requested_type_not_found<T>;
                static constexpr size_t value = I;
                //static constexpr requested_type_not_found<T> value = requested_type_not_found<T>();
            };

        // specialization: we find type T at position I in the sequence of types
        template <typename T, std::size_t I, typename... Ts>
        struct index_of_impl<T, I, T, Ts...>:
            std::integral_constant<std::size_t, I> {};

        // specialization: type at postion I is not what we're looking for, check the next one along
        template <typename T, std::size_t I, typename U, typename... Ts>
        struct index_of_impl<T, I, U, Ts...>:
            index_of_impl<T, I+1, Ts...> {};

        ///// Check if the set Ts... contains type T:

        // 'end' case: type T not found in pack or tuple 
        template <typename T, std::size_t I, typename... Ts>
        struct has_type_impl: std::false_type {};

        // specialization: we find type T at position I in the sequence of types
        template <typename T, std::size_t I, typename... Ts>
        struct has_type_impl<T, I, T, Ts...>:
            std::true_type {};

        // specialization: type at postion I is not what we're looking for, check the next one along
        template <typename T, std::size_t I, typename U, typename... Ts>
        struct has_type_impl<T, I, U, Ts...>:
            has_type_impl<T, I+1, Ts...> {};

    } // namespace detail!


    // Put the useful template(s) in etdc in stead of etdc::detail
    // 1.) find the (first) occurrence of T in Ts...
    template <typename T, typename... Ts>
    struct index_of:
        std::integral_constant<std::size_t, detail::index_of_impl<T, 0, Ts...>::value> {};

    // 2.) specialization for std::tuple<>, find the (first) occurrence of T in std::tuple<Ts...>
    template <typename T, typename... Ts>
    struct index_of<T, std::tuple<Ts...>>:
        std::integral_constant<std::size_t, detail::index_of_impl<T, 0, Ts...>::value> {};

    // Put the useful template(s) in etdc in stead of etdc::detail
    // 1.) find the (first) occurrence of T in Ts...
    template <typename T, typename... Ts>
    struct has_type: detail::has_type_impl<T, 0, Ts...> {};

    // 2.) specialization for std::tuple<>, find the (first) occurrence of T in std::tuple<Ts...>
    template <typename T, typename... Ts>
    struct has_type<T, std::tuple<Ts...>>: detail::has_type_impl<T, 0, Ts...> {};

    // 1.) find the (first) occurrence of Pred<T> in Ts...
    // Apply Pred to all types and check if the true_type is present
    template <template <typename...> class Pred, typename... Ts>
    struct index_of_p:
        std::conditional<has_type<std::true_type, typename apply<Pred, Ts...>::type>::value,
                         typename index_of<std::true_type, typename apply<Pred, Ts...>::type>::type,
                         requested_element_not_found>
    {};

    // 2.) specialization for std::tuple<>, find the (first) occurrence where Pred<T> in std::tuple<Ts...> is true
    template <template <typename...> class Pred, typename... Ts>
    struct index_of_p<Pred, std::tuple<Ts...>>:
        std::conditional<has_type<std::true_type, typename apply<Pred, Ts...>::type>::value,
                         typename index_of<std::true_type, typename apply<Pred, Ts...>::type>::type,
                         requested_element_not_found>
    {};


    /////////////////////////////////////////////////////////////////////////////////////////
    //
    //     An integral constant for testing if T is a 'real' integer.
    //     Yes, we are most certainly aware that 'char' and 'bool' typically
    //     are integers behind the scenes but we want to be able to enforce 
    //     strict numerical types - e.g. when a port number is expected.
    //
    //     You don't want to support code that reads:
    //          fd->connect( "host.example.com", true ); 
    //      or
    //          fd->connect( "host.example.com", 'a' ); 
    //
    //     The standard C++11 type traits (http://en.cppreference.com/w/cpp/header/type_traits) primitive
    //           std::is_integral<T>::value
    //     evaluates to 'true' for 'char' (including 'wchar_t'), 'bool' &cet ... 
    //
    /////////////////////////////////////////////////////////////////////////////////////////
    template <typename T>
    struct is_integer_number_type :
        std::integral_constant<bool, std::is_same<T, short>::value ||
                                     std::is_same<T, unsigned short>::value ||
                                     std::is_same<T, int>::value ||
                                     std::is_same<T, unsigned int>::value ||
                                     std::is_same<T, long>::value ||
                                     std::is_same<T, unsigned long>::value ||
                                     std::is_same<T, long long>::value ||
                                     std::is_same<T, unsigned long long>::value>
    {};

    /////////////////////////////////////////////////////////////////////////////////////////
    //
    //     for use with stl algorithms that work on a pair of iterators
    //     this pseudo sequence will allow iteration over the sequence 
    //          init, init+inc, init+2*inc, ...  
    //     without actually allocating memory for <nElement> items
    //
    //     http://en.cppreference.com/w/cpp/iterator/iterator
    //     does mention a class template along these lines. 
    //
    //     The implementation in here allows for multiple iterators to
    //     iterate over the same sequence. This is possible because the
    //     iterators do not modify the underlying Sequence object.
    //     Incrementing one iterator does not invalidate an other.
    //
    /////////////////////////////////////////////////////////////////////////////////////////
    template <typename T>
    struct Sequence {
        // delete stuff that we really don't want to enable
        Sequence() = delete;

        // Require at least first, last.
        // Note that if 'inc' == 0 you'll get a divide-by-zero error
        Sequence(T const& first, T const& last, T const& inc = T{1}):
            __m_counter(first), __m_increment(inc), __m_last(&__m_counter + static_cast<unsigned int>(::abs((last-first)/inc)) + 1) {}

        struct iterator_impl {
                // There will be no public c'tor
                friend struct Sequence<T>;
            public:
                // What kind of iterator do we pretend to be?
                // An inputiterator - we can only guarantee single pass validity
                typedef void                    difference_type;
                typedef T                       value_type;
                typedef T*                      pointer;
                typedef T const&                reference;
                typedef std::input_iterator_tag iterator_category;

                iterator_impl() = delete;

                T operator*( void )       { return __m_counter; }
                T operator*( void ) const { return __m_counter; }

                iterator_impl& operator++( void )             { return do_inc(); }
                iterator_impl& operator++( int )              { return do_inc(); }
                iterator_impl const& operator++( void ) const { return do_inc(); }
                iterator_impl const& operator++( int )  const { return do_inc(); }

                bool operator==(iterator_impl const& other) const {
                    return __m_cur==other.__m_cur;
                }
                bool operator!=(iterator_impl const& other) const {
                    return  !(this->operator==(other));
                }

            private:
                iterator_impl(T const& cnt, T* cur, T const& inc):
                    __m_counter( cnt ), __m_increment( inc ), __m_cur( cur )
                {}

                iterator_impl&       do_inc( void )       { __m_counter += __m_increment; __m_cur++; return *this; };
                iterator_impl const& do_inc( void ) const { __m_counter += __m_increment; __m_cur++; return *this; };

                mutable T   __m_counter;
                T           __m_increment;
                mutable T*  __m_cur;
        };


        typedef iterator_impl       iterator;
        typedef const iterator_impl const_iterator;

        iterator       begin( void )       { return iterator_impl(__m_counter, &__m_counter, __m_increment); }
        iterator       end( void )         { return iterator_impl(__m_counter, __m_last, __m_increment); }
        const_iterator begin( void ) const { return iterator_impl(__m_counter, &__m_counter, __m_increment); }
        const_iterator end( void )   const { return iterator_impl(__m_counter, __m_last, __m_increment); }

        private:
            T   __m_counter, __m_increment;
            T*  __m_last;
    };

    template <typename T>
    Sequence<T> mk_sequence(T const& f, T const& l, T const& inc=T{1}) {
        return Sequence<T>(f, l, inc);
    }

    /////////////////////////////////////////////////////////////////////////////////////////
    //
    //    A 'common_type' template that does not actually trigger a
    //    compilation error. Attempting to use std::enable_if<> with
    //    std::common_type<...> does not seem to work because if there
    //    isn't a common type, there is no "::type" typedef
    //
    //    If there is a common type, the ::value is true, otherwise the
    //    ::value is false.
    //
    //    In all cases the member typedef ::type exists. 
    //    When there /IS/ a common type, the ::type holds the type of the 
    //    common type. Otherwise it is detail::no_common_type
    //
    /////////////////////////////////////////////////////////////////////////////////////////
    namespace detail {
        struct no_common_type {};

        // No types => no common type
        template <typename...>
        struct m_common_type: std::false_type {
            using type = no_common_type;
        };

        // One type => common by default :D
        template <typename T>
        struct m_common_type<T>: std::true_type {
            using type = typename std::decay<T>::type;
        };

        // Two types - dat's a simple check
        template <typename T, typename U>
        struct m_common_type<T, U>:
            std::conditional<std::is_same<typename std::decay<T>::type, typename std::decay<U>::type>::value,
                             m_common_type<T>,
                             m_common_type<>>::type
        {};

        template <typename T, typename U, typename... Rest>
        struct m_common_type<T, U, Rest...>:
            std::conditional<std::is_same<no_common_type, typename m_common_type<T, U>::type>::value,
                             m_common_type<>, // T and U were already not same so no point in checking Rest
                             m_common_type<typename m_common_type<T, U>::type, Rest...>>::type
        {};
    }

    // Expose only this into the etdc namespace
    template <typename... Ts>
    using common_type = detail::m_common_type<Ts...>;


    /////////////////////////////////////////////////////////////////////////////////////////
    //
    // Sometimes it is really handy to be able to use first()/second() to
    // extract elements from tuple/pair &cet
    //
    // The following structs can act as functor to extract the n-th element 
    // of any thing that supports "std::get<N>(...)"
    //
    /////////////////////////////////////////////////////////////////////////////////////////
    template <std::size_t N>
    struct nth_type {
        explicit nth_type() {}

        template <typename T>
        auto operator()(T const& t) const -> decltype( std::get<N>(t) ) {
            return std::get<N>(t);
        }
        template <typename T>
        auto operator()(T& t) const -> decltype( std::get<N>(t) ) {
            return std::get<N>(t);
        }
    };
    // shorthands - handy for std::pair
    using fst_type = nth_type<0>;
    using snd_type = nth_type<1>;


    /////////////////////////////////////////////////////////////////////////////////////////
    //
    //  When using an insert iterator into e.g. a map or set, sometimes you'd like
    //  to assert that only unique values are being inserted.
    //  Using "std::insert_iterator(map<K,V>&,...)" will allow multiple inserts
    //  of the same Key but effectively only remembers the first value inserted.
    //  The other insertions are ignored.
    //
    //  Actually there could be three policies on how to deal with duplicate Key insert:
    //      1.) keep the first and ignore the rest
    //      2.) keep the last inserted value
    //      3.) fail on duplicate insert
    //
    /////////////////////////////////////////////////////////////////////////////////////////

    namespace detail {
        // mapped types in STL containers have a key_type typedef.
        // this is important for looking up values in any container:
        //  * non-associative containers: use std::find()
        //  * associative containers: use .find(key)
        template <typename Container, typename Value>
        typename Container::iterator find_value(Container* container, Value v, typename std::enable_if<has_key_type<Container>::value>::type* = nullptr) {
            // this Container has key_type as member typedef thus we assume Value is a <Key, Value> pair
            // so the lookup goes by Key, i.e. "v.first"
            return container->find( v.first );
        }
        template <typename Container, typename Value>
        typename Container::iterator find_value(Container* container, Value v, typename std::enable_if<!has_key_type<Container>::value>::type* = nullptr) {
            // this Container don't have key_type as member so we assume it's just a container of values
            // so the lookup goes by find(value)
            return std::find(std::begin(*container), std::end(*container), v);
        }

        // keep_first policy: do not insert if value already there
        template <typename Container>
        struct keep_first {
            template <typename T>
            typename Container::iterator operator()(Container* container, typename Container::iterator iter, T value) const {
                typename Container::iterator  exist = find_value(container, value);
                if( exist==container->end() )
                    return container->insert(iter, std::move(value));
                return iter;
            }
        };
        // keep_last policy: if value already existed, erase old value and insert new one
        template <typename Container>
        struct keep_last {
            template <typename T>
            typename Container::iterator operator()(Container* container, typename Container::iterator iter, T value) const {
                typename Container::iterator  exist = find_value(container, value);
                if( exist!=container->end() )
                    container->erase(exist);
                return container->insert(iter, std::move(value));
            }
        };

        // the assert-no-duplicates policy: it is an error to attempt to insert multiple identical keys
        template <typename Container>
        struct no_duplicates {
            template <typename T>
            typename Container::iterator operator()(Container* container, typename Container::iterator iter, T value) const {
                // Throw up if value already exists
                typename Container::iterator  exist = find_value(container, value);
                if( exist!=container->end() )
                    throw std::logic_error("Attempt to insert duplicate value");
                return container->insert(iter, std::move(value));
            }
        };

        template <typename Container, template<typename...> class Policy>
        struct policy_insert_iterator:
            public std::iterator< std::output_iterator_tag, void,void,void,void > 
        {
            // Yeah I know. struct is public by default. But we have protected member(s)
            // so to keep alignment 'pretty' we do it like this
            public:
                using Self = policy_insert_iterator<Container, Policy>;

                // no default not copy
                policy_insert_iterator() = delete;
                policy_insert_iterator(policy_insert_iterator<Container, Policy> const&) = delete;
                // but move is ok
                policy_insert_iterator(policy_insert_iterator<Container, Policy>&& other):
                    container( std::move(other.container) ), iter( std::move(other.iter) )
                {}

                policy_insert_iterator(Container& c, typename Container::iterator i):
                    container( &c ), iter( i )
                {}

                // The methods that don't do nothing
                Self& operator*( void )   { return *this; }
                Self& operator++( void )  { return *this; }
                Self& operator++( int )   { return *this; }

                // The method that does everything
                template <typename U>
                Self& operator=(U&& u) {
                    ++ (iter = __m_policy(container, iter, std::forward<U>(u)));
                    return *this;
                }

            protected:
                Container*                   container;
                typename Container::iterator iter;
                Policy<Container>            __m_policy{};
        };
    }

    template <typename Container>
    using keep_first_iterator    = detail::policy_insert_iterator<Container, detail::keep_first>;
    template <typename Container>
    using keep_last_iterator     = detail::policy_insert_iterator<Container, detail::keep_last>;
    template <typename Container>
    using no_duplicates_iterator = detail::policy_insert_iterator<Container, detail::no_duplicates>;

    template <typename Container>
    auto keep_first_inserter(Container& container, typename Container::iterator iter) -> keep_first_iterator<Container> {
        return keep_first_iterator<Container>(container, iter);
    }
    template <typename Container>
    auto keep_last_inserter(Container& container, typename Container::iterator iter) -> keep_last_iterator<Container> {
        return keep_last_iterator<Container>(container, iter);
    }
    template <typename Container>
    auto no_duplicates_inserter(Container& container, typename Container::iterator iter) -> no_duplicates_iterator<Container> {
        return no_duplicates_iterator<Container>(container, iter);
    }


    //////////////////////////////////////////////////////////////////////////////////////////
    //
    //  Pythonic "get(dict, key, default)"
    //  We have two flavours: dict and const dict [aka std::map<Key,Value>]
    //
    //////////////////////////////////////////////////////////////////////////////////////////

    template <typename Dict, typename Default, typename Foo = typename std::remove_reference<Dict>::type, typename Bar = typename std::decay<Dict>::type>
    typename std::enable_if<std::is_const<Foo>::value && std::is_convertible<Default, typename Bar::mapped_type>::value, typename Bar::mapped_type>::type
    get(Dict&& d, const typename Bar::key_type key, Default&& def) {
        auto ptr = d.find(key);
        if( ptr==d.end() )
            return typename Bar::mapped_type{ std::forward<Default>(def) };
        return ptr->second;
    }
    // non-const get
    template <typename Dict, typename Default, typename Foo = typename std::remove_reference<Dict>::type, typename Bar = typename std::decay<Dict>::type>
    typename std::enable_if<!std::is_const<Foo>::value && std::is_convertible<Default, typename Bar::mapped_type>::value, typename Bar::mapped_type>::type
    get(Dict&& d, const typename Bar::key_type key, Default&& def) {
        auto ptr = d.find(key);
        if( ptr==d.end() )
            d[ key ] = typename Bar::mapped_type{ std::forward<Default>(def) };
        return d[ key ];
    }
}

#endif // ETDC_UTILITIES_H
