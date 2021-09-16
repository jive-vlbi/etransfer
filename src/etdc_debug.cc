// some of the variables need to be defined only once in the whole program
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
#include <etdc_debug.h>

namespace etdc { namespace detail {
    std::mutex       __m_iolock{};
    std::atomic<int> __m_dbglev{1};
    std::atomic<int> __m_fnthres{5};

    std::string timestamp( std::string const& fmt ) {
        // First things first - sample the time as soon as we enter here
        // and convert to gmtime
        struct tm               raw_tm;
        struct timeval          raw_t1m3;

        ::gettimeofday(&raw_t1m3, NULL);
        ::gmtime_r(&raw_t1m3.tv_sec, &raw_tm);

        // Now we can alloc / string format at leisure
        std::string::size_type   nAlloc{ std::max( std::string::size_type{64}, fmt.size() * 2) };
        std::unique_ptr<char[]>  buff{ new char[nAlloc] };
        static std::string const default_fmt{ "%Y-%m-%d %H:%M:%S" };

        // The allocated buffer /may/ not be large
        // enough to hold the output string
        // We know that the format passed to strftime(3) isn't empty so
        // a ::strftime(3) return value of 0
        // indicates not enough space (and also we know that nAlloc > 0)
        // https://pubs.opengroup.org/onlinepubs/000095399/functions/strftime.html
        while( ::strftime(buff.get(), nAlloc, (fmt.size() ? fmt : default_fmt).c_str() , &raw_tm)==0 ) {
            nAlloc *= 2;
            buff    = std::unique_ptr<char[]>{ new char[nAlloc] };
        }

        // Default format adds some extra bits
        // nAlloc is guaranteed to support the default format (minimum size
        // of buff = 64 characters)
        if( fmt.empty() ) {
            // Default log timestamp format adds subseconds + ": " suffix to
            // make useful prefix for log entries
            ::snprintf( buff.get() + 19, nAlloc-19, ".%02ld: ", (long int)(raw_t1m3.tv_usec / 10000) );
        }
        return std::string( buff.get() );
    }

    } // namespace detail 
} // namespace etdc
