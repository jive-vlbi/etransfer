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
#include <etdc_thread_local.h>

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
        // "getprotobyname(3)" at the same time. For random+strerror we now
        // use proper thread-local storage
#ifndef __APPLE__
        using mutex       = std::mutex;
        using scoped_lock = std::lock_guard<mutex>;
        static mutex  protoent_lock{};
#endif

        struct random_state_type {
            bool            sown;
            unsigned short  xsubi[3];

            random_state_type():
                sown( false )
            {}
        };

        // See - here is the thread-local stuff
        using strerr_buf_type = etdc::tls_object_type<char[128]>;
        strerr_buf_type                          strerr_buf;
        etdc::tls_object_type<random_state_type> random_state;
    }

    // Get thread-local storage where strerror_r(3) can write into then we
    // copy it out back to the user
    std::string strerror(int errnum) {
        ::strerror_r(errnum, &detail::strerr_buf[0], detail::strerr_buf_type::size);
        return std::string( detail::strerr_buf.begin() );
    }

    void srand( void ) {
        time_t         now( ::time(0) ), tmp;
        pthread_t      self = ::pthread_self();

        ::memcpy(&tmp, &self, std::min(sizeof(time_t), sizeof(pthread_t)));
        now += tmp;
        ::memcpy(&detail::random_state->xsubi[0], &now, std::min(sizeof(time_t), sizeof(detail::random_state_type::xsubi)));
        detail::random_state->sown = true;
    }


    // Deal with the random stuff - we just deal with the 48-bit versions

    // 0 .. (2**32)-1
    long int random( void ) {
        if( !detail::random_state->sown )
            etdc::srand();
        return ::nrand48(detail::random_state->xsubi);
    }
    long int lrand48( void ) {
        if( !detail::random_state->sown )
            etdc::srand();
        return ::nrand48(detail::random_state->xsubi);
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
