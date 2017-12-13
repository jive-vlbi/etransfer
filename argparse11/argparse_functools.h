// functional programming tools on std::tuple/std::array: head(), tail(), fold[lr](), filter_[ptv], map(), copy()
// Copyright (C) 2017  Harro Verkouter, verkouter@jive.eu
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
#ifndef ARGPARSE_FUNCTOOLS_H
#define ARGPARSE_FUNCTOOLS_H

#include <tuple>
#include <iterator>
#include <algorithm>

namespace argparse { namespace functools {

    namespace detail {
        // This particular trick found here:
        // http://stackoverflow.com/a/8572595
        template <size_t... Idx>
        struct index_sequence {
            template <size_t N>
            struct push_back {
                using type = index_sequence<Idx..., N>;
            };
        };

        // This, however, is my rendition of building a sequence.
        // Note that at some point we could template the "test" operation
        // such that we don't have to literally repeat "First+1<Last"
        // but e.g. also make it count backwards ...

        // Anyway, this is the primary template:
        //  <first, last, built-so-far, continue-yes-no>
        template <size_t, size_t, typename, bool>
        struct mk_sequence_impl{};

        // This specialization pushes First onto the sequence of Indices built
        // so far and recurses to the next, including a test wether the
        // next index should be added (i.e. as long as the next index < last)
        template <size_t First, size_t Last, typename Indices>
        struct mk_sequence_impl<First, Last, Indices, true> {
            using type = typename mk_sequence_impl<First+1, Last,
                                                   typename Indices::template push_back<First>::type, First+1<Last>::type;
        };
        // Stop when First >= Last and 'return' the sequence of Indices built so far
        template <size_t First, size_t Last, typename Indices>
        struct mk_sequence_impl<First, Last, Indices, false> {
            using type = Indices;
        };

        // Some code deals with void + non-void returning calls equally well
        // For functioncalls that return void we replace them with
        // std::ignore (that's why it was invented, wasn't it ...)
        struct void_call {
            using type = decltype(std::ignore);

            // Perform the function call and lose the result
            template <typename F, typename... Args>
            type& operator()(F&& f, Args&&... args) const {
                f(std::forward<Args>(args)...);
                return __m_void;
            }
            //static constexpr type __m_void{};
            static  type __m_void;
        };
        void_call::type  void_call::__m_void{};

        template <typename T>
        struct nonvoid_call {
            using type = T;

            template <typename F, typename... Args>
            type operator()(F&& f, Args&&... args) const {
                return f(std::forward<Args>(args)...);
            }
        };

        // Shorthand for choosing the correct caller proxy
        template <typename T>
        using callertype = typename std::conditional<std::is_void<T>::value, void_call, nonvoid_call<T>>::type;
    }

    // Entry point - start building a sequence of indices from [first, last>
    // (i.e. last is non-inclusive!)
    template <size_t First, size_t Last>
    struct mk_sequence {
        using type = typename detail::mk_sequence_impl<First, Last, detail::index_sequence<>, First<Last>::type;
    };

    /////////////////////////////////////////////////////////////////////////////////
    //
    //    Get a tuple with the requested elements out of the given tuple
    //
    /////////////////////////////////////////////////////////////////////////////////
    template <typename T, size_t... Idx>
    auto get_idx(T&& t, detail::index_sequence<Idx...>) ->
        decltype( std::make_tuple( std::get<Idx>(std::forward<T>(t))... ) ) {
            return std::make_tuple( std::get<Idx>(std::forward<T>(t))... );
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    //                          head(...) is /very/ easy ...
    //
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    template <typename T>
    auto head(T&& t) -> decltype( std::get<0>(std::forward<T>(t)) ) {
        return std::get<0>(std::forward<T>(t) );
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    //                          tail(...) is 
    //             just a specialization of "get some indices" where
    //         the "some indices" happen to be "1 .. sizeof(tuple)-1"  ...
    //
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    template <typename T>
    auto tail(T&& t) ->
        decltype(get_idx(std::forward<T>(t),
                         typename mk_sequence<1, std::tuple_size<typename std::decay<T>::type>::value>::type())) {
            return get_idx(std::forward<T>(t),
                           typename mk_sequence<1, std::tuple_size<typename std::decay<T>::type>::value>::type());
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // foldl(OP, (x,y,z), u) =>
    //      u OP (x, y, z) -> OP( OP( OP(u,x), y), z )
    //
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    template <typename BinOp, typename T, typename U,
              typename std::enable_if<std::tuple_size<T>::value==0, int>::type = 0>
    auto foldl(BinOp&&, T&&, U&& u) -> typename std::decay<U>::type {
        return u;
    }

    template <typename BinOp, typename T, typename U,
              typename std::enable_if<std::tuple_size<T>::value!=0, int>::type = 0>
    auto foldl(BinOp&& binop, T&& t, U&& u) -> typename std::decay<U>::type {
            return foldl(std::forward<BinOp>(binop),
                         tail(std::forward<T>(t)),
                         binop(u, head(std::forward<T>(t))));
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // foldr(OP, (x,y,z), u) =>
    //      (x, y, z) OP u -> OP(x, OP(y, OP(z, u)))
    //
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    template <typename BinOp, typename T, typename U,
              typename std::enable_if<std::tuple_size<typename std::decay<T>::type>::value==0, int>::type = 0>
    auto foldr(BinOp&&, T&&, U&& u) -> typename std::decay<U>::type {
        return u;
    }

    template <typename BinOp, typename T, typename U,
              typename std::enable_if<std::tuple_size<typename std::decay<T>::type>::value!=0, int>::type = 0>
    auto foldr(BinOp&& binop, T&& t, U&& u) -> typename std::decay<U>::type {
        return std::forward<BinOp>(binop)(head(std::forward<T>(t)),
                                          foldr(std::forward<BinOp>(binop), 
                                                tail( std::forward<T>(t) ),
                                                 std::forward<U>(u)
                                               ));
    }

    ///////////////////////////////////////////////////////////////////////////
    //
    //   map(std::tuple<T1, T2,...>(t1, t2, ...), F&& f, Args... args) =>
    //      std::tuple{ f(t1, args...), f(t2, args...), ... }
    //
    //   So "f" must be a functor instance that has operator() defined for:
    //          (T1, Args...), (T2, Args...), ...
    //
    //   "f" can also be std::mem_fn(...) if T1, T2, ... all have the same
    //   base class and mem_fn = Base::*f (pointer-to-baseclass-member-function)
    //   or a lambda taking reference-to-baseclass.
    //
    //   Easier in C++14 when lambda's can take "auto ..." parameters ;-)
    //
    //   Notes:
    //      The functor + arguments go last in order for "Args..." to
    //      capture any number of arguments (including none at all)
    //
    //      The functor may return void - the library detects this
    //      and ignores the return value in such case. The tuple
    //      element of the returned tuple will be "std::ignore" 
    //
    //
    //   example:
    //   "Print all values in a tuple" (A classic)
    //
    //   // Create a functor that will print anything
    //   // to std::cout
    //   struct printfunctor {
    //      template <typename T>
    //      void operator()(T const& t) const {
    //          std::cout << t << std::endl;
    //      }
    //   };
    //
    //   // Hoopla!
    //   functools::map( std::make_tuple(1, 'a', 3.14), printfunctor() );
    //
    //   // Output:
    //   1
    //   'a'
    //   3.14
    //
    //
    //   Another example:
    //
    //   struct Base {
    //      virtual int foo(int i) const {
    //          return i+1;
    //      }
    //   };
    //   struct Derived: struct Base {
    //      virtual int foo(int j) const {
    //          return j+2;
    //      }
    //   };
    //
    //   auto r = functools::map(std::make_tuple(Base(), Derived()), std::mem_fn(&Base::foo), 42);
    //   // Result:
    //   // r == std::tuple<int, int>(43, 44);
    //
    ////////////////////////////////////////////////////

    // Because of limitiations we have to write an implementation which 
    // iterates over the tuple's elements by using the integer-sequence
    // index (expanding parameter pack)
    // In C++14 we could use std::integer_sequence but we don't have that
    // here in 2011 (even though it's 2017, I know ...)
    namespace detail {
        template <typename Tuple, typename F, std::size_t... Idx, typename... Args>
        auto map_impl(Tuple&& t, F&& f, detail::index_sequence<Idx...>, Args&&... args) -> 
            // We're returning a tuple with call results
            std::tuple<
                typename detail::callertype<decltype(f(std::get<Idx>(std::forward<Tuple>(t)), std::forward<Args>(args)...))>::type...
                >
        {
            // Here's where we actually forward everything to the proxy that's doing
            // the real calling
            return std::make_tuple( 
                detail::callertype<decltype(f(std::get<Idx>(std::forward<Tuple>(t)), std::forward<Args>(args)...))>()
                    (std::forward<F>(f), std::get<Idx>(std::forward<Tuple>(t)), std::forward<Args>(args)...)...
            );
        }
    }

    /////////////////////////////////////////////////////////////////////////////
    //
    //           This is the primary template
    //
    // It's only function is to determine the sequence of tuple indices in order
    // to pass them on to the implementation such that /it/ can apply "f(...)"
    // to each tuple element in turn
    //
    ////////////////////////////////////////////////////////////////////////////
    template <typename Tuple, typename F, typename... Args>
    auto map(Tuple&& t, F&& f, Args&&... args) -> 
        // If we need to repeat ourselves and the only difference is
        // wrapping the same code in "decltype(...)" then maybe
        // there's room for optimization. You'd say that the compiler
        // could do "decltype(...)" by itself to figure out what the F**K we're
        // returning.
        // I think C++14 or C++17 fixes this. (Yay.)
        decltype(
            detail::map_impl(
                std::forward<Tuple>(t),
                std::forward<F>(f),
                typename mk_sequence<0, std::tuple_size<typename std::decay<Tuple>::type>::value>::type{},
                std::forward<Args>(args)...
            )
        )
    {
        return detail::map_impl(
                    std::forward<Tuple>(t),
                    std::forward<F>(f),
                    typename mk_sequence<0, std::tuple_size<typename std::decay<Tuple>::type>::value>::type{},
                    std::forward<Args>(args)...
                );
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // For a list of of types, we can do
    //      filtering by predicate on the type(s)
    //     
    // For a tuple there can be two types of filtering elements:
    //      filtering by predicate on the element's value
    //      filtering by predicate on the element's type
    //
    // filter_p<PRED, Ts...> ->
    //      std::tuple<Us...> for all T's in Ts... for which PRED<T>::value == true
    //
    // filter_v(PRED, (x,y,z), OutputIterator) ->
    //      copy elements from tuple for which PRED(value) is true to output iterator
    //      this can be done at runtime [can't be done at compile time]
    //
    // filter_t<PRED>( (x,y,z) ) ->
    //      return tuple with elements for which PRED<type>::value is true
    //      (PRED should be a template)
    //
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    namespace detail {
        template <typename T>
        struct is_output_iterator {  
            template <typename U>
            static char test_category(std::output_iterator_tag);

            template <typename U>
            static unsigned int test_category(U);

            template <typename U>
            static auto test(typename std::iterator_traits<U>::pointer*) ->
                decltype( test_category<U>(std::declval<typename std::iterator_traits<U>::iterator_category>()) );

            template <typename U>
            static unsigned int test(U* x);

            static const bool value = sizeof(test<T>(nullptr)) == sizeof(char);
        };

        struct predicate_insert {
            template <typename T, typename P, typename I>
            void operator()(T&& t, P&& p, I&& i) const {
                if( p(t) )
                    *i++ = t;
            }
        };
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // filter_v(PRED, (x,y,z), OutputIterator) ->
    //      copy elements from tuple for which PRED(value) is true to output iterator
    //
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    template <typename Pred, typename T, typename Iter,
              typename std::enable_if<detail::is_output_iterator<typename std::decay<Iter>::type>::value,int>::type = 0>
    void filter_v(Pred&& pred, T&& t, Iter&& iter) {
        functools::map(std::forward<T>(t), detail::predicate_insert(), std::forward<Pred>(pred), std::forward<Iter>(iter));
    }


    namespace detail {
        template <template <typename...> class Predicate, std::size_t N, typename Indices, typename...>
        struct filter_t {
            using type = Indices;
        };
        template <template <typename...> class Predicate, std::size_t N, typename Indices, typename T, typename... Ts>
        struct filter_t<Predicate, N, Indices, T, Ts...> {
            using type = typename filter_t<Predicate, N+1, 
                                           typename std::conditional<Predicate<T>::value,
                                                                     typename Indices::template push_back<N>::type,
                                                                     Indices>::type,
                                           Ts...>::type;
        };
        template <template <typename...> class Predicate, std::size_t N, typename Indices, typename... Ts>
        struct filter_t<Predicate, N, Indices, std::tuple<Ts...>> {
            using type = typename filter_t<Predicate, N, Indices, Ts...>::type;
        };
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // filter_t<PRED>( (x,y,z) ) ->
    //      return tuple with elements for which PRED<type>::value is true
    //      (PRED should be a template)
    //
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    template <template <typename...> class Predicate, typename T>
    auto filter_t(T&& t) ->
        decltype( get_idx(std::forward<T>(t),
                          typename detail::filter_t<Predicate, 0, detail::index_sequence<>, typename std::decay<T>::type>::type{}) ) {
            return get_idx(std::forward<T>(t),
                           typename detail::filter_t<Predicate, 0, detail::index_sequence<>, typename std::decay<T>::type>::type{});
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // filter_p<PRED, Ts...> ->
    //      std::tuple<Us...> for all T's in Ts... for which PRED<T>::value == true
    //      (PRED should be a template)
    //
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    namespace detail {
        template <typename... Ts>
        struct typelist_t {};

        template <template <typename...> class Predicate, typename...>
        struct filter_p {};

        template <template <typename...> class Predicate, typename... Us>
        struct filter_p<Predicate, typelist_t<Us...>> {
            using type = std::tuple<Us...>;
        };
        template <template <typename...> class Predicate, typename... Us, typename T, typename... Ts>
        struct filter_p<Predicate, typelist_t<Us...>, T, Ts...> {
            using type = typename filter_p<Predicate,
                                           typename std::conditional<Predicate<T>::value,
                                                                     typelist_t<Us..., T>,
                                                                     typelist_t<Us...>>::type,
                                           Ts...>::type;
        };
    }
    template <template <typename...> class Predicate, typename... Ts>
    struct filter_p {
        using type = typename detail::filter_p<Predicate, detail::typelist_t<>, Ts...>::type;
    };
    template <template <typename...> class Predicate, typename... Ts>
    struct filter_p<Predicate, std::tuple<Ts...>> {
        using type = typename detail::filter_p<Predicate, detail::typelist_t<>, Ts...>::type;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // copy((x,y,z), OutputIterator) ->
    //      copy elements from tuple to output iterator
    //      basically filter_v with a filter that does return true always
    //
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    namespace detail {
        struct always_true {
            template <typename T>
            bool operator()(T const &) const {
                return true;
            }
        };
    }
    template <typename T, typename Iter,
              typename std::enable_if<detail::is_output_iterator<typename std::decay<Iter>::type>::value,int>::type = 0>
    void copy(T&& t, Iter&& iter) {
        functools::filter_v(detail::always_true(), std::forward<T>(t), std::forward<Iter>(iter));
    }

} }  // namespace argparse { namespace functools {

#endif // include guard
