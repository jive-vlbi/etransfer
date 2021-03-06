// get string representation of value + unit with inferred SI prefix or prettyprint numbers w/o modifying global stream
// Copyright (C) 2003-2018 Harro Verkouter
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
// Author:  Harro Verkouter - verkouter@jive.nl
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
//
//  NOTE: this code was imported from jive5a(b) into etransfer and
//        fully rewritten to use c++11 and localization features
//  NOTE: this code was imported from the PCInt project into jive5a(b)
//
//
//
// sciprint(value, "unit", ...)
//              reduces <value> to be >=1 and < 'a thousand', adjusting
//              the SI prefix as necessary
//              returns the reduced value formatted with the final SI prefix
//              before the unit.
//              If the value was 0, smaller than yocto (10^-24) or larger
//              than Yotta (10^24)  the value is returned formatted with
//              just the unit without prefix
//
//                  cout << sciprint(1.6193654E9, "Hz") << endl
//                       << sciprint(3345.356E-6, "m") << endl;
//
//              would yield:
//			        1.6193654 GHz
//                  3.345 mm
//
//  to_string(value, ...)
//              takes number + optional formatting parameters
//              to_string(103, std::setw(10), std::setfill('+'))
//                  => '+++++++103'
//              to_string(103, std::setw(10), std::setfill('>'), std::hex)
//                  => '>>>>>>0x67'
//
//  mk_formatter<T>("unit", ....)
//              returns a unary function which takes a type T
//              and returns std::string formatted as if
//                  sciprint(value, "unit", ...) was called
//              can be used to dynamically set/change formatting function
//
// These three API functions take an arbitrary amount of formatting
// parameters following their formal argument(s).
//
// These formatting parameters can be:
//  * I/O manipulators like std::hex, std::setw(n), std::setiosflagst(...)
//                      - manipulate the underlying streambuffer as per the manipulator
//  * std::ios_base::fmtflags like 'std::ios_base::showbase'
//                      - calls ".setf(...)" on the underlying streambuffer
//  * std::locale       - make the underlying streambuffer use this locale
//  * std::ostream&     - make the underlying streambuffer mimic that
//                        stream's formatting options (uses '.copyfmt()')
//                        https://en.cppreference.com/w/cpp/io/basic_ios/copyfmt
//  * std::locale::{numpunct/num_put}
//                      - instructs the underlying streambuf's locale to use
//                        this facet instead of what's presently there
//                        Limited to those two facets in order to limit
//                        surprises because the code only deals with
//                        numbers. So allowing to set facets that don't
//                        impact number formatting could be considered a
//                        no-op but also would lead to 'unexpected results'
//                        for those unaware of this not-being-used
//  * etdc::thousand(value)
//                      - sometimes a metric 'decade' of 1000 is not what is
//                      wanted, .g. printing kB, MB, GB, which are base
//                      1024.
//                      
//                          sciprint(1024.0, "B", thousand(1024))
//                              => "1 kB"
//                      whilst 
//                          sciprint(1024.0, "B")
//                              => "1.024 kB"
//
//
//  We also define some shorthand large-number punctuations.
//  This /could/ be done using locale's but it is not given that all
//  requested localizations are available on all systems.
//
//  So we implement these three 'pretty printing large numbers'
//  spacing+grouping settings:
//
//      english             '123,456,780.00'
//      european            '123.456.780,00'
//      english_spaced      '123 456 780.00'
//      european_spaced     '123 456 780,00'
//
//  which are all std::numpunct<char> derivatives
#ifndef ETDC_SCIPRINT_H
#define ETDC_SCIPRINT_H

#include <list>
#include <tuple>
#include <cmath>
#include <string>
#include <limits>
#include <algorithm>
#include <exception>
#include <sstream>
#include <iomanip>
#include <locale>
#include <functional>
#include <type_traits>

namespace etdc {
    //////////////////////////////////////////////////////////////////////////////////////////
    //
    //    before getting to the goodies, keep implementation details in, well, 
    //    the detail subnamespace ...
    //
    //////////////////////////////////////////////////////////////////////////////////////////
    namespace detail {
        // Keep the list of prefixes 
        using prefixlist_type = std::list<std::string>;

        // the list of prefixes, the empty string is the starting sentinel.
        // yeah i know: c++11 has compile time SI ratios
        // (https://en.cppreference.com/w/cpp/numeric/ratio/ratio)
        // but each of them is a different /type/
        // It is inconvenient (if possible at all) to loop over a list of *types* at runtime.
        // (compile time is ok of course)
        // Note: we try to detect at startup if the 'outside world' seems to
        // support ISO LATIN1 (ISO-8859-1) or UTF8 for display of the Greek
        // lowercase m (mu). If we cannot establish this is true then we
        // default to ASCII lower case m.
        static prefixlist_type const prefixes_u  = { "y", "z", "a", "f", "n", u8"\u00b5", "m", "", "k", "M", "G", "T", "P", "E", "Z", "Y" };
        static prefixlist_type const prefixes_nu = { "y", "z", "a", "f", "n", "u", "m", "", "k", "M", "G", "T", "P", "E", "Z", "Y" };

        // I was getting error: call to 'abs' is ambiguous (on OSX),
        // that was for an unsigned type ... apparently you can't do 
        // abs on an unsigned type.
        template <typename T>
        typename std::enable_if<std::is_unsigned<T>::value, T>::type
        the_abs(T t) {
            return t;
        }
        template <typename T>
        typename std::enable_if<!std::is_unsigned<T>::value, T>::type
        the_abs(T t) {
            return std::abs( t );
        }

        // Here we try to set the locale for the library based on the user's
        // preference. If that fails we default to "C"
        static std::locale getLocale( void ) {
            try {
                return std::locale("");
            }
            catch ( ... ) {
                // something went wrong - possibly user's LC_* environment
                // variables set to something erroneous
            }
            return std::locale::classic();
        }

        static std::locale            theEnvironment = getLocale();

        static inline prefixlist_type const& prefixes( void ) {
#ifdef __APPLE__
            // Bastard apple devs don't seem to give the environment a name
            // even it it was initialized from the user's environment
            static bool mayDoMicro = true;
#else
            static bool mayDoMicro = (theEnvironment.name().find("UTF-8")!=std::string::npos || theEnvironment.name().find("ISO8859-1")!=std::string::npos);
#endif
            if( mayDoMicro )
                return prefixes_u;
            return prefixes_nu;
        }

        // In order to do typesafe formatting (...) we need to have the
        // 'thousands' value and the requested format and the unit together
        // the 'thousands' type must be arithmetic but not bool!
        template <typename T,
                  typename std::enable_if<std::is_arithmetic<T>::value && !std::is_same<T, bool>::value, int>::type value = 0>
        struct thousands_type {
            T   __m_thousand;

            // no default!
            thousands_type() = delete;
            thousands_type(T const& t): __m_thousand(t) {
                // our algorithms only make sense if the thousand's value is greater than 1
                if( __m_thousand<=T{1} ) {
                    std::ostringstream es;
                    es << "The thousands's value must be > 1 (passed value = " << __m_thousand << ", tested against " << T{1} << ")";
                    throw std::runtime_error( es.str() );
                }
            }
        };

        // Test if type is a supported iostream manipulator
        template <typename T>
        struct is_iomanip :
            std::integral_constant<bool,
                                   std::is_same<T, decltype(std::setw(std::declval<int>()))>::value ||
                                   std::is_same<T, decltype(std::setfill(std::declval<char>()))>::value ||
                                   std::is_same<T, decltype(std::setbase(std::declval<int>()))>::value ||
                                   std::is_same<T, decltype(std::setprecision(std::declval<int>()))>::value ||
                                   std::is_same<T, decltype(std::setiosflags(std::declval<std::ios_base::fmtflags>()))>::value ||
                                   std::is_same<T, decltype(std::resetiosflags(std::declval<std::ios_base::fmtflags>()))>::value>
        {};
        // Test if type is a useful facet
        template <typename T>
        struct is_facet :
            std::integral_constant<bool,
                                   std::is_base_of<std::numpunct<char>, T>::value ||
                                   std::is_base_of<std::num_put<char> , T>::value>
        {};

        // The actual tuple of parameters:
        //  0: the value to format
        //  1: the unit to attach
        //  2: the unserlying stream buffer
        //  3: the thousands
        template <typename T>
        using sp_tuple = std::tuple<T, std::string, std::ostringstream, thousands_type<T>>;

        template <typename T>
        auto mk_default(T const& value, std::string const& unit) -> sp_tuple<T> {
            // remember the indices of the properties in the tuple:
            //                     0      1     2                     3
            auto rv =  sp_tuple<T>(value, unit, std::ostringstream(), thousands_type<T>(1000));
            std::get<2>(rv).imbue( theEnvironment ); 
            return rv;
        }
        template <typename T>
        auto mk_default_ptr(T const& value, std::string const& unit) -> std::shared_ptr<sp_tuple<T>> {
            // remember the indices of the properties in the tuple:
            //                     0      1     2                     3
            auto rv =  std::make_shared<sp_tuple<T>>(value, unit, std::ostringstream(), thousands_type<T>(1000));
            std::get<2>(*rv).imbue( theEnvironment ); 
            return rv;
        }
        
        // allow updating of the settings tuple

        // base case: nothing more to update
        template <typename T, typename ...>
        void update_sp_tuple(sp_tuple<T>&) { }

        // supported updates
        // * if a ref to an ostream is given copy all formatting rules
        //   (so you can make this thing mimic the format of other streams
        //    in your prgrm)
        template <typename T, typename CharT, typename Traits, typename... Args>
        void update_sp_tuple(sp_tuple<T>& sp, std::basic_ostream<CharT, Traits> const& fmt, Args&&... args) {
            std::get<2>(sp).copyfmt( fmt );
            update_sp_tuple(sp, std::forward<Args>(args)...);
        }
        // * change the thousands type - but only if it seems to be
        //   convertible to the destination type
        template <typename T, typename U, typename... Args,
                  typename std::enable_if<std::is_convertible<T, U>::value, int>::type v1 = 0>
        void update_sp_tuple(sp_tuple<T>& sp, thousands_type<U> const& thousands, Args&&... args) {
            std::get<3>(sp) = thousands_type<T>( thousands.__m_thousand );
            update_sp_tuple(sp, std::forward<Args>(args)...); 
        }
        template <typename T, typename U, typename... Args,
                  typename std::enable_if<!std::is_convertible<T, U>::value, int>::type v1 = 0>
        void update_sp_tuple(sp_tuple<T>&, thousands_type<U> const&, Args&&...) {
            static_assert(std::is_convertible<T, U>::value, "The thousand's type is not convertible to the value's type");
        }

        // * support the std I/O manipulators that make sense for ostream
        template <typename T, typename Manip, typename... Args,
                 typename std::enable_if<is_iomanip<Manip>::value, int>::type v = 0>
        void update_sp_tuple(sp_tuple<T>& sp, Manip const& manip, Args&&... args) {
            std::get<2>(sp) << manip;
            update_sp_tuple(sp, std::forward<Args>(args)...);
        }
        // * ios base formatflags get sent through .setf(...)
        template <typename T, typename... Args>
        void update_sp_tuple(sp_tuple<T>& sp, std::ios_base::fmtflags const& flags, Args&&... args) {
            std::get<2>(sp).setf( flags );
            update_sp_tuple(sp, std::forward<Args>(args)...);
        }
        // * manipulators that take std::ios_base& and return std::ios_base&
        template <typename T, typename... Args>
        void update_sp_tuple(sp_tuple<T>& sp, std::function<std::ios_base&(std::ios_base&)> const& manip, Args&&... args) {
            (void)manip( std::get<2>(sp) );
            update_sp_tuple(sp, std::forward<Args>(args)...);
        }
        // * support localization
        template <typename T, typename... Args>
        void update_sp_tuple(sp_tuple<T>& sp, std::locale const& locale, Args&&... args) {
            std::get<2>(sp).imbue(locale);
            update_sp_tuple(sp, std::forward<Args>(args)...);
        }
        // * facets?
        template <typename T, typename Facet, typename... Args,
                 typename std::enable_if<is_facet<Facet>::value, int>::type v = 0>
        void update_sp_tuple(sp_tuple<T>& sp, Facet const&, Args&&... args) {
            std::get<2>(sp).imbue( std::locale(std::get<2>(sp).getloc(), new Facet) );
            update_sp_tuple(sp, std::forward<Args>(args)...);
        }

        // Find the correct suffix - recursively
        template <typename T>
        prefixlist_type::const_iterator get_prefix(sp_tuple<T>& reduced, prefixlist_type::const_iterator p) {
            static const auto epsilon  = std::numeric_limits<T>::epsilon();
            static const auto one      = T{ 1 };

            // value sooooooh huge that we don't know what to do with it
            if( p==prefixes().end() )
                return p;

            // Get current value
            const auto value    = std::get<0>(reduced);
            const auto thousand = std::get<3>(reduced).__m_thousand;

            // If the reduced value is indescernible from 0 don't do anything either
            //if( std::abs(value) <= epsilon )
            if( detail::the_abs(value) <= epsilon )
                return p;
            // if value >=1 and < thousand we're also done
            if( value>=one && value<thousand )
                return p;
            // recurse in the correct direction, modifying the value and
            // moving the iterator one 'tricade' (decade = "10", tricade = "10^3")
            if( value<one ) {
                // We need to multiply by another factor of thousand to get
                // between 1 and '1000' but that means taking the previous
                // suffix.
                // But we can only do that if there *IS* a previous suffix!
                p = (p==prefixes().begin() ? prefixes().end() : (std::get<0>(reduced) *= thousand, std::prev(p)));
            } else {
                // divide by another factor of 'thousand' and move to next
                // larger prefix
                std::get<0>(reduced) /= thousand;
                p = std::next(p);
            }
            return get_prefix(reduced, p);
        }
    }
    
    // Function to make a thousands type of the correct persuasion
    template <typename T>
    auto thousand(T const& t) -> detail::thousands_type<T> {
        return detail::thousands_type<T>(t);
    }


    // you must pass:
    //   an arithmetic type - the value to format
    //   a std::string      - the unit
    // you may pass:
    //   a thousands value  - etdc::thousands([1000|1024|...])
    //                        (again: wrapped in own type for identification)
    //   any of the formatting primitives as explained in the header of the
    template <typename T, typename... Args>
    std::string sciprint(T const& value, std::string const& unit, Args&&... args) {
        // Start from a default setting based on primary input
        auto    settings = detail::mk_default(value, unit);
        // And allow customization
        detail::update_sp_tuple(settings, std::forward<Args>(args)...);

        // Now that's passed we can carry on with our lives!
        // we know the empty string does appear in the list of prefixes
        detail::prefixlist_type::const_iterator  pfx = detail::get_prefix(settings, std::find(std::begin(detail::prefixes()), std::end(detail::prefixes()), ""));
        auto&                                    strm = std::get<2>(settings);

        strm.str( std::string() );
        if( pfx==detail::prefixes().end() )
            strm << value << " " << unit;
        else
            strm << std::get<0>(settings) << " " << *pfx << unit;
        return strm.str();
    }

    template <typename T, typename... Args>
    std::string to_string(T const& value, Args&&... args) {
        // Start from a default setting based on primary input
        auto    settings = detail::mk_default(value, std::string());
        // And allow customization
        detail::update_sp_tuple(settings, std::forward<Args>(args)...);
        // Do them formattin'
        std::get<2>(settings) << std::get<0>(settings);
        return std::get<2>(settings).str();
    }

    // Handy: make formatting function
    // You get to specify the type of the value because we can't infer that
    template <typename T, typename... Args>
    auto mk_formatter(std::string const& unit, Args&&... args) -> std::function<std::string(T const&)> {
        // Start from a default setting based on primary input
        auto                                    settings = detail::mk_default_ptr(T{}, unit);
        detail::prefixlist_type::const_iterator empty_pfx = std::find(std::begin(detail::prefixes()), std::end(detail::prefixes()), "");

        // And allow customization
        detail::update_sp_tuple(*settings, std::forward<Args>(args)...);

        // Now that we've got the basic settings, we can generate the lambda
        // that does the formattin' of any value according to our likin'
        return [=](T const& value) {
            // Copy the new value into the formatting tuple
            std::get<0>(*settings) = value;

            // After updating the value we can go look for a suitable SI prefix, if any
            detail::prefixlist_type::const_iterator  pfx = detail::get_prefix(*settings, empty_pfx);
            // Get reference to the ostringstream
            auto&                                    strm = std::get<2>(*settings);
            strm.str( std::string() );
            // Output raw value w/ unit or reduced value w/ prefix and unit
            if( pfx==detail::prefixes().end() )
                strm << value << " " << std::get<1>(*settings);
            else
                strm << std::get<0>(*settings) << " " << *pfx << std::get<1>(*settings);
            return strm.str();
        };
    }

    template <typename T, typename... Args>
    auto mk_to_string(Args&&... args) -> std::function<std::string(T const&)> {
        // Start from a default setting based on primary input
        auto    settings = detail::mk_default_ptr(T{}, std::string());
        // And allow customization
        detail::update_sp_tuple(*settings, std::forward<Args>(args)...);
        return [=](T const& value) {
            auto&  strm = std::get<2>(*settings);
            // Do them formattin'
            strm.str( std::string() );
            strm << value;
            return strm.str();
        };
    }

    ////////////////////////////////
    //  numpunct details
    ////////////////////////////////
    namespace detail {
        template <typename CharT, CharT sep, CharT dp>
        struct spaced_out : std::numpunct<CharT> {
            CharT       do_decimal_point() const { return dp;}    // ...
            CharT       do_thousands_sep() const { return sep; }  // ...
            std::string do_grouping()      const { return "\3"; } // groups of 3 digits
        };
    }
    static const detail::spaced_out<char, ',', '.'>         english{};
    static const detail::spaced_out<char, ' ', '.'>  spaced_english{};
    static const detail::spaced_out<char, '.', ','>        european{};
    static const detail::spaced_out<char, ' ', ','> spaced_european{};

    static auto const& imperial    = english;
    static auto const& continental = european;
}

#endif
