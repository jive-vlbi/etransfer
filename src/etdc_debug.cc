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

    std::string timestamp( void ) {
        char           buff[64];
        struct tm      raw_tm;
        struct timeval raw_t1m3;

        ::gettimeofday(&raw_t1m3, NULL);
        ::gmtime_r(&raw_t1m3.tv_sec, &raw_tm);
        ::strftime( buff, sizeof(buff), "%Y-%m-%d %H:%M:%S", &raw_tm );
        ::snprintf( buff + 19, sizeof(buff)-19, ".%02ld: ", (long int)(raw_t1m3.tv_usec / 10000) );
        return buff;
    }

    } // namespace detail 
} // namespace etdc
