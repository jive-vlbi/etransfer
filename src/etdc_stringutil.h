// sometimes there's stuff not in std C++ library :-(
// Copyright (C) 2017-* Harro Verkouter
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

#include <regex>
#include <string>
#include <algorithm>

namespace etdc {
    ////////////////////////////////////////////////////////////////////////////
    //
    // Split string "s" into substrings delimited by the character "sep"
    // skip_empty indicates what to do with multiple consecutive separation
    // characters:
    //
    // Given s="aap,,noot,,,mies"
    //       sep=','
    //
    // then output gets the following written into it:
    //      skip_empty=true  => "aap" "noot" "mies"
    //      skip_empty=false => "aap" "" "noot" "" "" "mies"
    //
    ////////////////////////////////////////////////////////////////////////////
    template <typename OutputIterator>
    void string_split(std::string const& s, char sep, OutputIterator output, bool skip_empty=true) {
        std::regex  rxSplit( std::string("\\")+sep+(skip_empty ? "+" : "") );
        
        std::copy(std::sregex_token_iterator(std::begin(s), std::end(s), rxSplit, -1),
                  std::sregex_token_iterator(), output);
    }
} // namespace etdc
