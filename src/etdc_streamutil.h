// utilities for inserting 'stuff' to std::ostream and whatnots
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
#ifndef ETDC_STREAMUTIL_H
#define ETDC_STREAMUTIL_H

#include <iterator>
#include <string>
#include <iostream>
#include <functional>
#include <sstream>

namespace etdc {

    /////////////////////////////////////////////////////////////////////////////////////////////
    //
    // A really simple functional form to transform anything to std::string
    // that can be output to a std::ostream
    //
    /////////////////////////////////////////////////////////////////////////////////////////////
    template <typename T>
    std::string repr(T const& t) {
        std::ostringstream oss;
        oss << t;
        return oss.str();
    }

    /////////////////////////////////////////////////////////////////////////////////////////////
    //
    // Implements a streamiterator, model of std::outputiterator for
    // outputting formatted data to a std::ostream
    //
    /////////////////////////////////////////////////////////////////////////////////////////////

    // It is a scoped output iterator that, at construction time outputs the
    // "Open" sequence (if given in the c'tor) onto the stream.
    //
    // Any values assigned to the iterator are ouput to the stream with "Separator"
    // inserted if necessary
    //
    // If the iterator goes out of scope, the "Close" sequence is output, if any.
    //
    // One can use the mk_streamiter() function below to conveniently
    // construct an instance w/o bothering about the templates too much:
    //  
    //   std::list<int>   a{1,2,3}
    //   std::copy(std::begin(a), std::end(a),
    //             mk_streamiter(os, ", "));
    //   // outputs: 1, 2, 3
    //   std::copy(std::begin(a), std::end(a),
    //             mk_streamiter(os, '_', 42, 3.14));
    //   // outputs: "421_2_33.14"
    //   It's not very useful but it shows that Separator/Open/Close don't have to be
    //   the same type or even be string ...
    //   Use your imagination to make the best use of this :D
    //
    template <typename Separator, typename Open = std::string, typename Close = std::string>
    struct streamiter {
        public:
            // According to http://en.cppreference.com/w/cpp/concept/OutputIterator
            // "pure outputiterators may typedef (all but 'iterator_category') to be 'void'"
            typedef void                     difference_type;
            typedef void                     value_type;
            typedef void                     pointer;
            typedef void                     reference;
            typedef std::output_iterator_tag iterator_category;

            // Construct from ostream ref and at the very least a separator.
            // Open and Close sequences are optional
            streamiter(std::ostream& os, Separator sep, Open open = Open(), Close close = Close()):
                __need_separator( false ), __need_close( true ), __m_separator( sep ), __m_close( close ), __m_streamref( os )
            { __m_streamref.get() << open; }

            // In the move constructor we must explicitly tell the object
            // that we've been move-constructed from to NOT output the
            // closing sequence
            streamiter(streamiter&& other):
                __need_separator( false ), __need_close( true ),
                __m_separator( std::move(other.__m_separator) ),
                __m_close( std::move(other.__m_close) ),
                __m_streamref( std::move(other.__m_streamref) )
            {
                other.__need_close = false;
            }
            // iterator interface
            template <typename T>
            streamiter& operator=(T const& t) {
                if( __need_separator )
                     __m_streamref.get() << __m_separator;
                __m_streamref.get() << t, __need_separator = true;
                return *this;
            }

            streamiter& operator++()                { return *this; };
            streamiter& operator++(int)             { return *this; };
            streamiter const& operator++() const    { return *this; };
            streamiter const& operator++(int) const { return *this; };

            streamiter&       operator*()       { return *this; };
            streamiter const& operator*() const { return *this; };

            // explicitly forbid default c'tor
            streamiter() = delete;

            ~streamiter() {
                __need_close && (__m_streamref.get() << __m_close);
            }

        private:
            using StreamRef = std::reference_wrapper<std::ostream>;

            bool        __need_separator, __need_close;
            Separator   __m_separator;
            Close       __m_close;
            StreamRef   __m_streamref;
    };

    // Function template to quickly create these things
    template <typename... Args>
    auto mk_streamiter(std::ostream& os, Args&&... args) -> streamiter<Args...> {
        return streamiter<Args...>{os, std::forward<Args>(args)...};
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    //
    //   Print the contents of a tuple
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    namespace detail {
        // helpers for printing the tuple's elements

        // This is the endpoint of the 'recursion'
        template <typename SIter, typename Tuple, size_t I, size_t N>
        void copy_to_streamiter(SIter&, Tuple const&, typename std::enable_if<(I>=N), int>::type = 0) {
            return;
        }

        // generic case - enabled if requested index < number of elements in tuple
        template <typename SIter, typename Tuple, size_t I, size_t N>
        void copy_to_streamiter(SIter& si, Tuple const& tup, typename std::enable_if<(I<N), int>::type = 0) {
            *si++ = std::get<I>(tup);
            copy_to_streamiter<SIter, Tuple, I+1, N>(si, tup);
        }

        template <typename T>
        struct tuple_holder {
            public:

                // We allow construction form reference-to-T as well as the move constructor
                tuple_holder(T const& value): __m_value_ref(value) {}
                tuple_holder(tuple_holder<T>&& other): __m_value_ref( std::move(other.__m_value_ref) ) {}

                // the ostream insert operator better be our friend
                friend std::ostream& operator<<(std::ostream& os, tuple_holder<T> const& h) {
                    auto strmiter = mk_streamiter(os, ',', '(', ')');
                    copy_to_streamiter<decltype(strmiter), T, size_t(0), std::tuple_size<T>::value>(strmiter, h.__m_value_ref.get());
                    return os;
                }

                // your really don't want any of these
                tuple_holder()                                           = delete;
                tuple_holder(tuple_holder<T> const&)                     = delete;
                tuple_holder<T> const& operator=(tuple_holder<T> const&) = delete;
        
            private:
                using ValueRef = std::reference_wrapper<T const>;
                ValueRef    __m_value_ref;
        };
    }
    template <typename T>
    auto fmt_tuple(T const& tref) -> detail::tuple_holder<T> {
        return detail::tuple_holder<T>(tref);
    }
}


#endif // ETDC_STREAMUTIL_H
