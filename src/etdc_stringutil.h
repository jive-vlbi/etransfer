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

        template <typename T>
        struct has_value_type {
            using yes = char;
            using no  = unsigned long;

            template <typename U>
            static auto test(typename U::value_type*) -> yes;
            template <typename U>
            static auto test(U*)                      -> no;

            static const bool value = (sizeof(test<T>(nullptr)) == sizeof(yes));
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

    // Implement case insensitive < for strings
    struct case_insensitive_lt {
        template <typename T, typename = typename std::enable_if<detail::has_value_type<typename std::decay<T>::type>::value>::type>
        bool operator()(T const& l, T const& r) const {
            static const detail::case_insensitive_lt_t<typename T::value_type> ci_comparator{};
            return std::lexicographical_compare(std::begin(l), std::end(l), std::begin(r), std::end(r), ci_comparator);
        }
    };

    // Simple character replacement
    // In-place version
    template <typename Char, typename Traits, typename... Rest>
    void replace_char(std::basic_string<Char, Traits, Rest...>& in,
                      typename std::basic_string<Char, Traits, Rest...>::value_type srch,
                      typename std::basic_string<Char, Traits, Rest...>::value_type repl) {
        std::replace_if( std::begin(in), std::end(in),
                         [&srch](typename std::basic_string<Char, Traits, Rest...>::value_type v) {
                             return Traits::eq(v, srch);
                         }, repl);
    }

    // const input? then return a new string
    template <typename Char, typename Traits, typename... Rest>
    std::basic_string<Char, Traits, Rest...>
    replace_char(std::basic_string<Char, Traits, Rest...> const& in,
                 typename std::basic_string<Char, Traits, Rest...>::value_type srch,
                 typename std::basic_string<Char, Traits, Rest...>::value_type repl) {
        std::basic_string<Char, Traits, Rest...> result{ in };
        std::replace_if( std::begin(result), std::end(result),
                         [&srch](typename std::basic_string<Char, Traits, Rest...>::value_type v) {
                             return Traits::eq(v, srch);
                         }, repl);
        return result;
    }

    std::string replace_char(char const* in, char srch, char repl) {
        return replace_char( std::string(in), srch, repl );
    }
} // namespace etdc 

#endif // include guard
