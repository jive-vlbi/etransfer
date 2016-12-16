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
#include <iterator>

// plain old C
#include <stdlib.h>

namespace etdc {

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
    }

    // Let's put the useful primitive just in etdc in stead of etdc::detail
    template<typename T> 
    struct is_container :
        std::integral_constant<bool, detail::has_const_iterator<T>::value && detail::has_begin_end<T>::value> 
    {};



    // for use with stl algorithms that work on a pair of iterators
    // this pseudo sequence will allow iteration over the sequence 
    //    init, init+inc, init+2*inc, ...  
    // without actually allocating memory for <nElement> items
    //
    // http://en.cppreference.com/w/cpp/iterator/iterator
    // does mention a class template along these lines. This implementation
    // allows for multiple iterators to iterate over the same sequence
    // because the iterators do not modify the underlying Sequence object.
    // Incrementing one iterator does not invalidate an other.
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

        iterator       begin( void ) { return iterator_impl(__m_counter, &__m_counter, __m_increment); }
        iterator       end( void )   { return iterator_impl(__m_counter, __m_last, __m_increment); }
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

}

#endif // ETDC_UTILITIES_H
