// Allow 'tagging' of data in stead of having to wrap the POD in a class/struct
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
#ifndef ETDC_TAGGED_H
#define ETDC_TAGGED_H

// Own C++ headers
#include <utilities.h>   // for type2str<...>()

// Standard C++ headers
#include <tuple>         // ...
#include <utility>       // std::forward
#include <ostream>       // std::ostream
#include <stdexcept>     // std::logic_error
#include <functional>    // std::reference_wrapper et.al.
#include <type_traits>   // std::is_same


// Sometimes it's convenient to be able to say 'this int is the port number'
// and 'that int means the option_name from setsockopt(2)'
//
// It's possible to encode the meaning of the int in the name of the
// variable but that is sooooooh 2016. And also it's not type safe.
//
// We'd like the port number and option_name types to be really distinct
// such that you cannot, compile time, assign one to the other.
//
// Possible solutions:
//  * create class/struct type which contains the integer(*) such that
//    the types don't match and you can't create one from the other
//  
//  * this file is an experiment: tag any data type with any number of tags
//    basically creating a new type as per the previous but with more
//    flexibility.
//
//  namespace detail {
//      struct port_tag {};
//      struct option_name_tag {};
//      struct setsockopt_tag {};
//  }
//  using port_number_type = tagged<int, detail::port_tag>;
//  using option_name_type = tagged<int, detail::option_name_tag, detail::setsockopt_tag>; // any number of tags 
//
//  Now you can do:
//
//  int                 an_integer( 42 );
//  port_number_type    port( 443 );  // construct from int
//  option_name_type    option( SO_RCVBUF );
//
//
//  // If you need to get at the contained instance, use "untag(...)"
//  // untag(...) returns a std::reference_wrapper<> to the contained T instance
//  cout << untag(port)+1 << endl; 
//  auto  contained1 = untag( option ).get();
//
//  // but it also works on any non-tagged type so can be used transparently
//  // on tagged / non-tagged types
//  auto  contained2 = untag( an_integer ).get();
//
//  (*) or any old data type, for that matter


namespace etdc {

    // details
    namespace detail {
        template <typename... Ts>
        struct typelist_type {
            template <typename... Us>
            using is_equal = std::is_same<std::tuple<Ts...>, std::tuple<Us...>>;
        };

    }

    // forward declaration
    template <typename T, typename... Tags>
    struct tagged;

    // Is the entry tagged with tag Tag?
    template <typename Tag, typename...>
    struct has_tag: std::false_type {};

    template <typename Tag, typename T, typename... Tags>
    struct has_tag<Tag, tagged<T, Tags...>>: etdc::has_type<Tag, Tags...> {};

    // Does the thingy have a tag satisfying Pred<Tag>?
    template <template <typename...> class Pred, typename T, typename...>
    struct has_tag_p: std::false_type {};

    template <template <typename...> class Pred, typename T, typename... Tags>
    struct has_tag_p<Pred, tagged<T, Tags...>>:
        etdc::has_type<std::true_type, typename etdc::apply<Pred, Tags...>::type>
    {};

    namespace detail {
        template <typename T>
        struct unsatisfied_predicate {};
    }

    //  Check if the tagged value has a tag satisfying predicate Pred
    template <template <typename...> class Pred, typename... Ts>
    struct get_tag_p {
        using type = detail::unsatisfied_predicate<Pred<Ts...>>;
    };
    template <template <typename...> class Pred, typename T, typename... Tags>
    struct get_tag_p<Pred, tagged<T, Tags...>> {
        using result_type  = typename etdc::apply<Pred, Tags...>::type;
        using type = typename std::conditional<
            // Depending on wether true_type appears ...
            etdc::has_type<std::true_type, result_type>::value,
            // extract that tag
            typename std::tuple_element<etdc::index_of<std::true_type, result_type>::value, std::tuple<Tags...>>::type,
            // or not
            detail::unsatisfied_predicate<Pred<tagged<T,Tags...>>>>::type;
    };

    // The main template - tag an instance of T with any number of tags
    template <typename T, typename... Tags>
    struct tagged {
        using type    = T;
        using my_tags = detail::typelist_type<Tags...>;

        explicit constexpr tagged(T const& t): __m_value(t) {}

        // Support any c'tors that T support
        template <typename... Ts>
        explicit tagged(Ts... ts) : __m_value(std::forward<Ts>(ts)...) { }

        // Prevent construction from somethat that does not have the same tags
        template <typename U, typename... UTags>
        tagged(tagged<U, UTags...> const& ut): __m_value(ut.__m_value) {
            static_assert( my_tags::template is_equal<UTags...>::value, "Tagged types don't match" );
        }

        // Support output to any type of ostream
        template <class CharT, class Traits>
        friend std::basic_ostream<CharT, Traits>& 
               operator<<(std::basic_ostream<CharT, Traits>& os, 
                          tagged<T, Tags...> const& w) {
                   return os << w.__m_value;
               }

        // Allow assignments: 3 flavours:
        //    * from a convertable type U
        //    * from own type T
        //    * from tagged<U, UTags...> where all Tags match with own tags
        //      and U can be converted to T
        template <typename U>
        tagged<T, Tags...> const& operator=(U const& u) {
            __m_value = T{ u };
            return *this;
        }
        tagged<T, Tags...> const& operator=(T const& t) {
            __m_value = t;
            return *this;
        }

        // Match anything that is tagged<U, ...>
        // and prevent assignment from something that does not have the same tags
        template <typename U, typename... UTags>
        tagged<T, Tags...> const& operator=(tagged<U, UTags...> const& ut) {
            static_assert( my_tags::template is_equal<UTags...>::value, "Tagged types don't match" );
            __m_value = T{ ut.__m_value };
            return *this;
        }

        template <typename U>
        operator U() const {
            return static_cast<U>( __m_value );
        }
        T   __m_value {};
    };

    // Specialization: tag a function pointer!
    template <typename T, typename... Args, typename... Tags>
    struct tagged<T(*)(Args...), Tags...> {
        using type    = T(*)(Args...);
        using my_tags = detail::typelist_type<Tags...>;

        tagged(): __m_value( nullptr ) {}
        explicit constexpr tagged(type t): __m_value(t) {}

        // Prevent construction from somethat that does not have the same tags
        template <typename U, typename... UArgs, typename... UTags>
        tagged(tagged<U(*)(UArgs...), UTags...> const& ut): __m_value(ut.__m_value) {
            static_assert( std::is_same<type, typename tagged<U(*)(UArgs...), UTags...>::type>::value &&
                           my_tags::template is_equal<UTags...>::value, "Tagged types don't match" );
        }

        // Support output to any type of ostream
        template <class CharT, class Traits>
        friend std::basic_ostream<CharT, Traits>& 
               operator<<(std::basic_ostream<CharT, Traits>& os, 
                          tagged<T(*)(Args...), Tags...> const& w) {
                   return os << etdc::type2str<T(*)(Args...)>() << " @" << (void*)w.__m_value;
               }

        // Allow assignments
        // 1. From raw function pointer with same prototype
        tagged<T(*)(Args...), Tags...> const& operator=(T(*t)(Args...)) {
            __m_value = t;
            return *this;
        }

        // We allow function call operator!
        T operator()(Args... args) const {
            return this->__m_value(std::forward<Args>(args)...);
        }

        T(*__m_value)(Args...);
    };

    // Specialization: tag a std::function<...> object
    template <typename T, typename... Args, typename... Tags>
    struct tagged<std::function<T(Args...)>, Tags...> {
        using self    = tagged<std::function<T(Args...)>, Tags...>;
        using type    = std::function<T(Args...)>;
        using my_tags = detail::typelist_type<Tags...>;

        tagged(): __m_value( nullptr ) {}

        // Can construct from raw pointer-to-function
        //explicit tagged(T(*ptr)(Args...)): __m_value( ptr ) {}
        // or from function object
        //explicit tagged(type const& ptr): __m_value( ptr ) {}

        // Let the compilert figure out if this is OK?
        template <typename U>
        explicit tagged(U u): __m_value( u ) {}

        // Prevent construction from somethat that does not have the same tags
        template <typename U, typename... UTags>
        tagged(tagged<U, UTags...> const& ut): __m_value(ut.__m_value) {
            static_assert( my_tags::template is_equal<UTags...>::value, "Tagged types don't match" );
        }

        // Support output to any type of ostream
        template <class CharT, class Traits>
        friend std::basic_ostream<CharT, Traits>& 
               operator<<(std::basic_ostream<CharT, Traits>& os, self const&) {
                   return os << etdc::type2str<type>();
               }

        // Allow assignments
        // 1. From raw function pointer with same prototype
        // 2. From std::function with same prototype
        self const& operator=(T(*t)(Args...)) {
            __m_value = t;
            return *this;
        }

        self const& operator=(type const& t) {
            __m_value = t;
            return *this;
        }

        // We allow function call operator!
        T operator()(Args... args) const {
            return this->__m_value(std::forward<Args>(args)...);
        }

        type __m_value;
    };


    /////////////////////////////////////////////////////////////////////
    //
    //      free functions to 'extract' the contained value from 
    //      an (optionally) tagged value
    //
    /////////////////////////////////////////////////////////////////////
    template <typename T, typename...>
    T& untag(T& t) {
        return t;
    }

    template <typename T, typename...>
    T& untag(T* t) {
        return *t;
    }

    template <typename T, typename... Ts>
    T& untag(tagged<T, Ts...>& t) {
        return t.__m_value;
    }

    // const versions
    template <typename T, typename...>
    T const& untag(T const& t) {
        return t;
    }

    template <typename T, typename...>
    T const& untag(T const* t) {
        return *t;
    }

    template <typename T, typename... Ts>
    T const& untag(tagged<T, Ts...> const& t) {
        return t.__m_value;
    }
} // namespace etdc


#endif // ETDC_TAGGED_H
