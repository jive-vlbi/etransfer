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
#ifndef ETDC_STRINGUTIL_H
#define ETDC_STRINGUTIL_H

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
    
    namespace detail {
        // case insensitive sorting [lexicographical compare: is l < r?]
        template <typename Char>
        struct case_insensitive_lt_t {
            bool operator()(Char l, Char r) const {
                return ::toupper((unsigned char)l) < ::toupper((unsigned char)r);
            }
        };
        // case insensitive equality: is l==r?
        template <typename Char>
        struct case_insensitive_eq_t {
            bool operator()(Char l, Char r) const {
                return ::toupper((unsigned char)l) == ::toupper((unsigned char)r);
            }
        };
    }

    // the libc stringcompare functions return 0 if they're equal, i.e. false if equal.
    // let's mimic that behaviour to not upset users
    template <typename Char, typename... Traits>
    bool stricmp(std::basic_string<Char, Traits...> const& l, std::basic_string<Char, Traits...> const& r) {
        static const detail::case_insensitive_eq_t<Char> ci_comparator{};
        if( l.size()==r.size() )
            return std::equal(std::begin(l), std::end(l), std::begin(r), ci_comparator);
        return false;
    }
} // namespace etdc

#endif // include guard
