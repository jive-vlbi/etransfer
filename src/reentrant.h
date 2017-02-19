// Provide thread-safe versions or wrappers of some libc functions
// Copyright (C) 2007-2017 Harro Verkouter
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
//
// Example: 
//    yes there is "strerror_r". But everywhere one uses it, it requires
//    some buffer management around it.
//    In the etdc namespace we provide simple wrappers that either:
//       1.) do the buffer management for you
//       2.) provide locked access to the resource in case there isn't a 
//           POSIX "_r" variant
//
//    We found that Linux / glibc does implement a lot of "_r" functions but
//    that most of them are not POSIX, i.e. not portable. 
//    Thus we take our losses and make sure the portable systemcalls/libc
//    calls are made MT-Safe
//
#ifndef ETDC_REENTRANT_H
#define ETDC_REENTRANT_H

#include <string>
#include <netdb.h>


namespace etdc {
    namespace detail {
        struct protocol_entry {
            int         p_proto;
            std::string p_name;

            protocol_entry();
            protocol_entry(struct protoent* pptr);
        };
    }
    // calls strerror_r(3) behind the scenes so we can replace "::strerror()" with
    // "etdc::strerror()" everywhere
    std::string strerror(int errnum);

    // Will do srandom_r() first time random() is called inside a thread
    long int random( void );

    // Will do srand48_r() first time lrand48() is called inside a thread
    long int lrand48( void );

    // getprotobyname is not marked MT-Safe.
    // getprotobyname_r() is not POSIX, apparently. This wrapper returns
    // the protocol number, doing it thread-safe.
    detail::protocol_entry getprotobyname(char const* name);
}

#endif

