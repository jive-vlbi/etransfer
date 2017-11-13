// Some oft-used keys for setting parameter values
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
#ifndef ETDC_STDKEYS_H
#define ETDC_STDKEYS_H
// For code that uses:
//
//   namespace keys = etdc::stdkeys;
//   foo() {
//       ...
//       settings.update( keys::mtu=9000, keys::timeout=0.5 );
//   }
#include <keywordargs.h>


namespace etdc { namespace stdkeys {
    const auto mtu        = key("mtu");
    const auto timeout    = key("timeout");
    const auto rcvbufSize = key("rcvbufSize");
    const auto sndbufSize = key("sndbufSize");
}}

#endif
