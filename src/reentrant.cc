// implementation
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
#include <reentrant.h>

#include <thread>
#include <iostream>
#include <stdexcept>

#include <stdlib.h>   // for ::free(), ::srandom_r, ::initstate_r
#include <string.h>   // for ::strerror_r()
#include <time.h>
#include <errno.h>
//#include <signal.h>
//#include <dosyscall.h>


namespace etdc {

    namespace detail {
        protocol_entry::protocol_entry():
            p_proto( -1 )
        {}

        protocol_entry::protocol_entry(struct protoent* pptr):
            p_proto( pptr->p_proto ), p_name( pptr->p_name )
        {}

        // Simplest is just to use mutexes to make sure not more than one thread
        // calls "strerror(3)", "getprotobyname(3)" or "random(3)" at the same time
        using mutex       = std::mutex;
        using scoped_lock = std::lock_guard<mutex>;

        static mutex  strerror_lock{};
        static mutex  random_lock{};
#ifndef __APPLE__
        static mutex  protoent_lock{};
#endif
    }

    std::string strerror(int errnum) {
        detail::scoped_lock     scopedLock( detail::strerror_lock );
        return std::string( ::strerror(errnum) );
    }

    long int random( void ) {
        detail::scoped_lock     scopedLock( detail::random_lock );
        return ::random();
    }

    long int lrand48( void ) {
        detail::scoped_lock     scopedLock( detail::random_lock );
        return ::lrand48();
    }

    detail::protocol_entry getprotobyname(char const* name) {
        // Quoth getprotoent(3) on Mac OSX:
        // "These functions use a thread-specific data space; if the data is
        // needed for future use, it should be copied before any subsequent calls overwrite it."
        //
        // Well, we copy the values anyway so MacOSX is MT-Safe
        #ifndef __APPLE__
        detail::scoped_lock     scopedLock( detail::protoent_lock );
        #endif
        struct protoent* pptr;

        if( (pptr=::getprotobyname(name))==0 )
            throw std::runtime_error(std::string("getprotent(")+name+") fails - no such protocol found");
        return detail::protocol_entry(pptr);
    }
} // namespace etdc
