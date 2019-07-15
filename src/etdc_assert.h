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

namespace etdc {
    // Simple wrapper that looks like std::ostringstream which allows for 
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
    struct stream {
        // Yaaah. Struct is public by default. But we also have privates. So thus.
        public:
            // Construct from any number of arguments - they'll be inserted into self
            template <typename... Ts>
            stream(Ts&&... ts) { insert( std::forward<Ts>(ts)... ); }

            // Provide operator<< for all types
            template <typename T>
            stream& operator<<(T&& t) {
                return insert( std::forward<T>(t) );
            }

            inline std::string str( void ) const {
                return __m_stream.str();
            }

            // Support output to any type of ostream
            template <class CharT, class Traits>
            friend std::basic_ostream<CharT, Traits>& 
                   operator<<(std::basic_ostream<CharT, Traits>& os, 
                              stream const& s) {
                       return os << s.str();
                   }

        private:
            std::ostringstream __m_stream;

            // No need to expose these to der Publik
            // This is the only way to do it in c++11 where we don't have 'auto' parameters in lambdas
            // base-case - stop the iteration
            inline stream& insert( void ) { return *this; }

            // Strip off one parameter, insert to self and move on to nxt
            template <typename T, typename... Ts>
            stream& insert(T&& t, Ts... ts) {
                __m_stream << std::forward<T>(t);
                return insert( std::forward<Ts>(ts)... );
            }
    };

    ////////////////////////////////////////////////////////////////////////////////
    //
    //     Next up: assertion_error and syscall_error exception classes,
    //     derived from std::runtime_error
    //
    ////////////////////////////////////////////////////////////////////////////////
    struct assertion_error:
        public std::runtime_error
    {
        assertion_error(std::string const& s):
            std::runtime_error(std::string("assertion error: ")+s)
        {}

        using std::runtime_error::what;
    };


    struct syscall_error:
        public std::runtime_error
    {
        syscall_error(std::string const& s):
            std::runtime_error(std::string("system call failed: ")+s)
        {}

        using std::runtime_error::what;
    };
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
    if( !(cond) ) { throw etdc::assertion_error(etdc::stream(__FILE__, ":", __LINE__, " [", #cond, "] fails").str()); }

// Almost the same but now we only take one extra argument the (stream)
// formatted message, e.g.: ETDCASSERT(fd>0, "fd=" << fd << " is NOT > 0!");
#define ETDCASSERT(cond, msg)  \
    if( !(cond) ) { throw etdc::assertion_error((etdc::stream(__FILE__, ":", __LINE__, " [", #cond, "] ") << msg).str()); }

// When calling systemcalls and asserting their return values, most often
// you don't need (or want) the whole function call in the error, e.g. in a 
// situation like this:
//
// ETDCASSERT(::bind(pSok->__m_fd, reinterpret_cast<struct sockaddr const*>(&sa), socklen_t(...))==0, 
//            "failing to bind to " << sa << " - " << etdc::strerror());
//
// You'd get the "::bind(...........)" string verbatim in the assertion
// error which might be a bit too verbose. So the SYSCALL macros
// now do two things:
//  1. do not add the assertion itself to the message, unless there
//     is no error message then we DO add the violating code
//  2. throw a different error
#define ETDCSYSCALLX(cond)  \
    if( !(cond) ) { throw etdc::syscall_error(etdc::stream(__FILE__, ":", __LINE__, " [", #cond, "] fails").str()); }

// This version assumes that the msg will explain what failed so we don't
// have to include the violating code verbatim
#define ETDCSYSCALL(cond, msg) \
    if( !(cond) ) { throw etdc::syscall_error((etdc::stream(__FILE__, ":", __LINE__, " ") << msg).str()); }

#endif // ETDC_ASSERT_H
