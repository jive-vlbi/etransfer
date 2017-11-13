// function prototypes for the automatically generated version info functions
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
#ifndef BUILDINFO_VERSION_H
#define BUILDINFO_VERSION_H

#include <string>

// Returns a string with colon separated fields:
//   <program> : <program version> : <bit size> : <release> : <build> : <datetime>
std::string buildinfo( void );

// Return a specific field of the version string
//    "PROG"
//    "PROG_VERSION"
//    "B2B"
//    "RELEASE"
//    "BUILD"     
//    "BUILDINFO"  (date/time)
std::string version_constant( std::string constant );

#endif
