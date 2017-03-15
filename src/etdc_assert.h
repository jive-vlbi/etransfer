// Personally I think there are not nearly enough assertions around
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
#ifndef ETDC_ASSERT_H
#define ETDC_ASSERT_H

#include <iostream>
#include <sstream>
#include <stdexcept>

#if 0
#include <utilities.h>
#endif

namespace etdc {

    // Simple wrapper around std::ostringstream which allows for 
    // easy construction of temporary stringstream objects, e.g. for
    // creating readable error messages:
    //
    // if( ::open(f.c_str())==-1 )
    //    throw std::runtime_error( stream("Failed to open file '", f, "'") << ::strerror(errno) );
    //
    // The nice thing is that after construction you can keep on 'streaming'
    // into the temporary
    //
    // (Or, as we'll see below, in generating assertion error messages ...
    struct stream:
        public std::ostringstream
    {
        public:
#if 0
            // Short-circuit for when constructing from just a string
            stream(std::string const& s):
                std::ostringstream(s)
            {}
            template <typename... Ts,
                      typename std::enable_if<std::is_same<std::string, typename etdc::common_type<Ts...>::type>::value, int>:type = 0>
            stream(Ts const&... ts):
                std::ostringstream( std::accumulate
#endif
            // Construct from any number of arguments - they'll be inserted into self
            template <typename... Ts>
            stream(Ts... ts) { insert( std::forward<Ts>(ts)... ); }

            // Support output to any type of ostream
            template <class CharT, class Traits>
            friend std::basic_ostream<CharT, Traits>& 
                   operator<<(std::basic_ostream<CharT, Traits>& os, 
                              stream const& s) {
                       return os << s.str();
                   }
#if 0
            inline operator char const*( void ) const {
                return this->std::ostringstream::str().c_str();
            }
#endif
        private:
            // No need to expose these to der Publik
            // This is the only way to do it in c++11 where we don't have 'auto' parameters in lambdas
            // base-case - stop the iteration
            inline void insert( void ) {}

            // Strip off one parameter, insert to self and move on to nxt
            template <typename T, typename... Ts>
            void insert(T const& t, Ts... ts) {
                (*this) << t;
                insert( std::forward<Ts>(ts)... );
            }
    };

    ////////////////////////////////////////////////////////////////////////////////
    //
    //     Next up: an assertion_error exception class,
    //     derived from std::runtime_error
    //
    ////////////////////////////////////////////////////////////////////////////////
    struct assertion_error:
        public std::runtime_error
    {
        // Allow construction from an arbitrary amount of arguments
        //template <typename... Ts>
        //assertion_error(Ts... ts):
        //    std::runtime_error( stream("assertion error: ", std::forward<Ts>(ts)...).str() )
        //{}
        assertion_error(std::string const& s):
            std::runtime_error(std::string("assertion error: ")+s)
        {}

        using std::runtime_error::what;
    };

        
    ////////////////////////////////////////////////////////////////////////////////
    //
    //     Next up: an actual variable-argument function that throws 
    //     a specifiyable exception if the condition does not hold.
    //     It accepts a boolean and a variable number of arguments
    //     which will be transformed into the error message
    //
    ////////////////////////////////////////////////////////////////////////////////
    namespace detail {
        template <typename Exception, typename... Ts>
        void assert(bool b, Ts... ts) {
            if( !b )
                throw Exception( stream(std::forward<Ts>(ts)...).str() );
        }
    }

    ////////////////////////////////////////////////////////////////////////////////
    //
    //     Nearly there. For /useful/ assertion errors, you want the
    //     actual source file + line number included.
    //     So what we'll do is to create an object which can hold
    //     this information and define an overloaded function call
    //     operator - which will trigger the *actual* assertion.
    //     Of course it will be templated on the actual error type such
    //     that it can be propagated to the assertion proper.
    //
    ////////////////////////////////////////////////////////////////////////////////
    namespace detail {
        template <typename Error>
        struct location {
            // Take any number of arguments that make up the location
            template <typename... Ts>
            location(Ts... ts):
                __m_location( stream(std::forward<Ts>(ts)...).str() ) {}

            // And the functioncall operator takes at least the boolean
            // condition to test
            template <typename... Ts>
            void operator()(bool cond, Ts... ts) const {
                detail::assert<Error>(cond, __m_location, " ", std::forward<Ts>(ts)...);
            }
            const std::string __m_location;
        };
    }
} // namespace etdc

////////////////////////////////////////////////////////////////////////////////
//
//  At this point I cannot think up another way - but AFAIK 
//  we MUST use a macro at some point :-(
//
//  The upshot is that it:
//  1. captures the location
//  2. captures the actual assertion condition (in string representation)
//  3. takes a variable number of arguments.
//  4. Yes, apparently this is C++11 standardized, the __VA_ARGS__
//     It came in through C99, apparently:
//     https://en.wikipedia.org/wiki/Variadic_macro
//  5. The __VA_ARGS__ is actually only useful if it can do zero or more
//     arguments but that is only through a GNU extension. 
//     The only standardized __VA_ARGS__ only works correctly for 
//     one or more arguments. So we might as well do away with __VA_ARGS__
//     for the moment.
//
////////////////////////////////////////////////////////////////////////////////
#define ETDCASSERTX(cond)  \
    etdc::detail::location<etdc::assertion_error>(__FILE__, ":", __LINE__, " [", #cond, "]")(cond)

// Almost the same but now we only take one extra argument the (stream)
// formatted message, e.g.: ETDCASSERT(fd>0, "fd=" << fd << " is NOT > 0!");
#define ETDCASSERT(cond, msg)  \
    etdc::detail::location<etdc::assertion_error>(__FILE__, ":", __LINE__, " [", #cond, "]")(cond, (std::ostringstream() << msg).str())

#endif // ETDC_ASSERT_H
