// Infrastructure to type-safe set/get socket options
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
#ifndef ETDC_ETDC_SOCKOPT_H
#define ETDC_ETDC_SOCKOPT_H

#include <tagged.h>
#include <reentrant.h>
#include <etdc_streamutil.h>
// UDT includes
#include <udt.h>
#include <ccc.h>

#include <errno.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <map>
#include <stdexcept>

namespace etdc {

    // We're going to tag our socket options
    namespace tags {
        // Believe it or not, UDT has socket options that are only settable
        // or only gettable! (Actually, there are also 'normal' SO_* options that
        // are only gettable)
        struct settable      {};
        struct gettable      {};
        struct level         {}; // the tagged value is the level value of set/getsockopt(2)
        struct option_name   {}; //         ,,              option_name      ,,
        struct udt_option    {};
    }

    namespace detail {
        template <int Value, typename... Tags>
        using TaggedInt    = etdc::tagged<std::integral_constant<int, Value>, Tags...>;

        template <int option_name>
        using Name         = TaggedInt<option_name, tags::option_name>;
        template <int level>
        using Level        = TaggedInt<level      , tags::level>;

        template <typename T, typename... Tags>
        using SocketOption = etdc::tagged<T, Tags...>;

        template <int option_name>
        using SimpleSocketOption  = SocketOption<int, tags::settable, Name<option_name>, tags::gettable, Level<SOL_SOCKET>>;

        template <int option_name>
        using BooleanSocketOption = SocketOption<bool, tags::settable, Name<option_name>, tags::gettable, Level<SOL_SOCKET>>;

        // According to http://udt.sourceforge.net/udt4/doc/opt.htm the level is ignored:
        //
        // int setsockopt(UDTSOCKET u, int level, SOCKOPT optname, const char* optval, int optlen)
        //
        // Parameters:
        //  u       [in] Descriptor identifying a UDT socket.
        //  level   [in] Unused. For compatibility only.
        //  optName [in] The enum name of UDT option. The names and meanings are ...
        template <UDTOpt udtname>
        using UDTName = etdc::tagged<std::integral_constant<UDTOpt, udtname>, tags::option_name>;

        template <UDTOpt udtname> 
        using SimpleUDTOption    = SocketOption<int, UDTName<udtname>, Level<-1>, tags::udt_option, tags::gettable, tags::settable>;

        template <UDTOpt udtname> 
        using BooleanUDTOption   = SocketOption<bool, UDTName<udtname>, Level<-1>, tags::udt_option, tags::gettable, tags::settable>;
    }

    // Helpers
    template <typename T, typename...>
    struct has_level_tag: std::false_type {};

    template <typename T, typename... Ts>
    struct has_level_tag<tagged<T, Ts...>>: etdc::has_type<tags::level, Ts...> {};

    template <typename T, typename...>
    struct has_name_tag: std::false_type {};

    template <typename T, typename... Ts>
    struct has_name_tag<tagged<T, Ts...>>: etdc::has_type<tags::option_name, Ts...> {};

    template <typename T, typename...>
    struct is_udt_option: std::false_type {};

    template <typename T, typename... Ts>
    struct is_udt_option<tagged<T, Ts...>>: etdc::has_type<tags::udt_option, Ts...> {};

    /////////////////////////////////////////////////////////////////////////////////////////
    //
    //  Socket options are not always integers but are typically *mapped to* integers
    //  Here is support for defining application socket option type to
    //  actual data type being sent to the socket layer.
    //
    //  An example would be a boolean option: at application level we want
    //  just 'true' or 'false' which should be translated to the int '1' or
    //  '0' respectively
    //
    /////////////////////////////////////////////////////////////////////////////////////////
    namespace detail {
        template <typename T>
        struct identity_xform {
            template <typename... Us>
            static T from(Us... us) {
                return T( std::forward<Us>(us)... );
            }
            template <typename U>
            static U to(T const& t) {
                return U{t};
            }
        };
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    //
    //                   The socket options we support
    //
    //  This is what it's all about: we encode all vital information in the type
    //
    ////////////////////////////////////////////////////////////////////////////////////////
    using so_sndbuf     = detail::SimpleSocketOption<SO_SNDBUF>;
    using so_rcvbuf     = detail::SimpleSocketOption<SO_RCVBUF>;
    using so_reuseaddr  = detail::BooleanSocketOption<SO_REUSEADDR>;
    using so_rcvtimeo   = detail::SocketOption<struct timeval, detail::Level<SOL_SOCKET>, tags::settable, detail::Name<SO_RCVTIMEO>>;
    using tcp_nodelay   = detail::SocketOption<bool, detail::Name<TCP_NODELAY>, detail::Level<IPPROTO_TCP>, tags::gettable, tags::settable>;
    using ipv6_only     = detail::SocketOption<bool, detail::Name<IPV6_V6ONLY>, detail::Level<IPPROTO_IPV6>, tags::gettable, tags::settable>;

    // The SO_REUSEPORT may or may not be available. 
#ifdef SO_REUSEPORT
    using so_reuseport  = detail::BooleanSocketOption<SO_REUSEPORT>;
#endif

    using udt_fc        = detail::SimpleUDTOption<UDT_FC>;
    using udt_mss       = detail::SimpleUDTOption<UDT_MSS>;
    using udt_sndbuf    = detail::SimpleUDTOption<UDT_SNDBUF>;
    using udt_rcvbuf    = detail::SimpleUDTOption<UDT_RCVBUF>;
    using udp_sndbuf    = detail::SimpleUDTOption<UDP_SNDBUF>;
    using udp_rcvbuf    = detail::SimpleUDTOption<UDP_RCVBUF>;
    using udt_reuseaddr = detail::BooleanUDTOption<UDT_REUSEADDR>;
    using udt_sndsyn    = detail::BooleanUDTOption<UDT_SNDSYN>;
    using udt_rcvsyn    = detail::BooleanUDTOption<UDT_RCVSYN>;
    using udt_linger    = detail::SocketOption<struct linger, detail::UDTName<UDT_LINGER>, tags::udt_option, detail::Level<-1>, tags::settable, tags::gettable>;
    using udt_max_bw    = detail::SocketOption<int64_t, detail::UDTName<UDT_MAXBW>, tags::udt_option, detail::Level<-1>, tags::settable, tags::gettable>;

    // UDT Congestion Control
    template <typename T>
    using udt_set_cc    = detail::SocketOption<CCCFactory<T>*, detail::UDTName<UDT_CC>, detail::Level<-1>, tags::udt_option, tags::settable>;
    using udt_get_cc    = detail::SocketOption<CCC*, detail::UDTName<UDT_CC>, detail::Level<-1>, tags::udt_option, tags::gettable>;


    // Translation between system/low level option type/value and high level
    // type-safe API
    namespace detail {

        // The basic native type is just the type itself
        template <typename T>
        struct native_sockopt {
            using type = T;

            static type to_native(type const& t) {
                return t;
            }
            static type from_native(type const& t) {
                return t;
            }
        };
        // Normally we never support pointer-to-something to be
        // passed as socket option
        template <typename T>
        struct native_sockopt<T*> { };

        /////////////////////////////////////////////////////////////////
        //
        // Now we must discriminate between UDT and system socket options
        // because there are differences in opinion
        //
        /////////////////////////////////////////////////////////////////

        // UDT follows the basic  type - bool=bool, int=int etc
        template <typename T>
        struct udt_native_sockopt: native_sockopt<T> {
            using type = typename native_sockopt<T>::type;
            using native_sockopt<T>::to_native;
            using native_sockopt<T>::from_native;
        };

        // but UDT supports passing T* (e.g. for congestion control options)
        template <typename T>
        struct udt_native_sockopt<T*> {
            using type = void*;

            static type to_native(T* p) {
                return static_cast<void*>(p);
            }
            static type* from_native(void* p) {
                return reinterpret_cast<T*>(p);
            }
        };

        // "system", Berkely, sockets (TCP, UDP, ssl?) also mostly follow the native 
        // types, only they don't know about "bool"
        template <typename T>
        struct sys_native_sockopt: native_sockopt<T> {
            using type = typename native_sockopt<T>::type;
            using native_sockopt<T>::to_native;
            using native_sockopt<T>::from_native;
        };

        // "bool" it has to be translated to "int";
        // make sure that booleans are really really really only translated
        // between 0/1 and false/true
        template <>
        struct sys_native_sockopt<bool> {
            using type = int;

            static type to_native(bool b) {
                return b ? 1 : 0;
            }
            static bool from_native(int i) {
                // Apparently, 'boolean' socket options, when just read, can
                // be either 0 or non-0, not strictly 0 or 1: 
                //
                // int        ndel, s = socket(AF_INET, SOCK_STREAM, 0);
                // socklen_t  sz(sizeof(i));
                //
                // ::getsockopt(s, TCP_NODELAY, IPPROTO_TCP, &ndel, &sz);
                // cout << "tcp_nodelay=" << ndel << endl;
                //
                // Did output:
                // tcp_nodelay=4
                //
                //if( i<0 || i>1 )
                //    throw std::domain_error("from_native: sockopt int value to bool: value is not 0 or 1, it is "+repr(i));
                //return i==1;
                return static_cast<bool>(i);
            }
        };

        ////////////////////// option name to string
        #define OPTION(a) {a, std::string(#a)}

        // For ordinary options
        using i2n_map_type = std::map<int, std::string>;
        static const i2n_map_type i2n_map{ OPTION(TCP_NODELAY), OPTION(SO_RCVBUF), OPTION(SO_REUSEADDR), OPTION(SO_SNDBUF),
                                           #ifdef SO_REUSEPORT
                                           OPTION(SO_REUSEPORT),
                                           #endif
                                           OPTION(SO_RCVTIMEO) };

        inline std::string option_str(int o) {
            i2n_map_type::const_iterator p = i2n_map.find(o);
            return ((p==i2n_map.end()) ? (std::string("** unknown socket option #")+etdc::repr(o)+" **") : p->second);
        }

        // And type safe for UDT
        using i2n_udt_map_type = std::map<UDTOpt, std::string>;
        static const i2n_udt_map_type i2n_udt_map{ OPTION(UDT_MSS), OPTION(UDT_CC), OPTION(UDT_REUSEADDR), OPTION(UDT_SNDBUF),
                                                   OPTION(UDT_RCVBUF), OPTION(UDT_MAXBW) };

        inline std::string udt_option_str(UDTOpt o) {
            i2n_udt_map_type::const_iterator p = i2n_udt_map.find(o);
            return ((p==i2n_udt_map.end()) ? (std::string("** unknown UDT socket option #")+etdc::repr(o)+" **") : p->second);
        }
        #undef OPTION
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    //          templated set/getsockopt methods that can be used to set/get
    //          multiple socket options in 'one' go. Exception will be raised in case of wonky
    //
    //  signature:
    //
    //     etdc::setsockopt(fd, Option1, Option2, ... )
    //     etdc::getsockopt(fd, Option1, Option2, ... )
    //
    //     Option1, Option2 &cet are instances of the "so_..." and "udt_..." types above  ^
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////////

    inline int setsockopt(int) { return 0; } 

    // UDT option
    template <typename Option, typename... Rest>
    typename std::enable_if<is_udt_option<Option>::value && etdc::has_tag<tags::settable, Option>::value, int>::type
    setsockopt(int s, Option const& ov, Rest... rest) {
        using native_type = detail::udt_native_sockopt<typename Option::type>;
        // All socket options MUST have a level and a name
        const int                  level    = etdc::get_tag_p<has_level_tag, Option>::type::type::value;
        const UDTOpt               opt_name = etdc::get_tag_p<has_name_tag,  Option>::type::type::value;
        typename native_type::type opt_val  = native_type::to_native( untag(ov) );

        if( UDT::setsockopt(s, level, opt_name, (char const*)&opt_val, int(sizeof(typename native_type::type)))==UDT::ERROR ) {
            UDT::ERRORINFO & udterr( UDT::getlasterror() );
            throw std::runtime_error("Failed to set UDT option "+detail::udt_option_str(opt_name)+": "+
                                      udterr.getErrorMessage()+" ("+etdc::repr(udterr.getErrorCode())+"/fd="+etdc::repr(s)+")");
        }

        // OK, this option done, carry on with rest
        return 1+setsockopt(s, std::forward<Rest>(rest)...);
    }

    // non-UDT option
    template <typename Option, typename... Rest>
    typename std::enable_if<!is_udt_option<Option>::value && etdc::has_tag<tags::settable, Option>::value, int>::type
    setsockopt(int s, Option const& ov, Rest... rest) {
        using native_type = detail::sys_native_sockopt<typename Option::type>;
        // All socket options MUST have a level and a name
        const int                  level    = etdc::get_tag_p<has_level_tag, Option>::type::type::value;
        const int                  opt_name = etdc::get_tag_p<has_name_tag,  Option>::type::type::value;
        typename native_type::type opt_val  = native_type::to_native( untag(ov) );

        if( ::setsockopt(s, level, opt_name, (void*)&opt_val, socklen_t(sizeof(typename native_type::type)))!=0 )
            throw std::runtime_error("Failed to set socket option "+detail::option_str(opt_name)+": "+
                                     etdc::strerror(errno)+"/fd="+etdc::repr(s));

        // OK, this option done, carry on with rest
        return 1+setsockopt(s, std::forward<Rest>(rest)...);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    //  where there is setsockopt, getsockopt should be near ...
    //
    //  guess what ...
    //
    //  you just found it!
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////////
    inline int getsockopt(int) { return 0; } 

    // get UDT option
    template <typename Option, typename... Rest>
    typename std::enable_if<is_udt_option<Option>::value && etdc::has_tag<tags::gettable, Option>::value, int>::type
    getsockopt(int s, Option& ov, Rest&... rest) {
        using native_type = detail::udt_native_sockopt<typename Option::type>;
        // All socket options MUST have a level and a name
        int                        opt_len{ sizeof(typename native_type::type) };
        const int                  level    = etdc::get_tag_p<has_level_tag, Option>::type::type::value;
        const UDTOpt               opt_name = etdc::get_tag_p<has_name_tag,  Option>::type::type::value;
        typename native_type::type opt_val;

        if( UDT::getsockopt(s, level, opt_name, (char *)&opt_val, &opt_len)==UDT::ERROR ) {
            UDT::ERRORINFO & udterr( UDT::getlasterror() );
            throw std::runtime_error("Failed to get UDT option "+detail::udt_option_str(opt_name)+": "+
                                      udterr.getErrorMessage()+" ("+etdc::repr(udterr.getErrorCode())+"/fd="+etdc::repr(s));
        }
        if( opt_len!=sizeof(typename native_type::type) )
            throw std::domain_error(std::string("getsockopt/udt: returned option_value size (")+etdc::repr(opt_len)+") "+
                                    "does not match native size ("+etdc::repr(sizeof(typename native_type::type))+")"+
                                    "/fd="+etdc::repr(s));

        // Transform from native to actual type and copy into the parameter
        untag( ov ) = native_type::from_native( opt_val );

        // OK, this option done, carry on with rest
        return 1+getsockopt(s, std::forward<Rest&>(rest)...);
    }

    // get non-UDT option
    template <typename Option, typename... Rest>
    typename std::enable_if<!is_udt_option<Option>::value && etdc::has_tag<tags::gettable, Option>::value, int>::type
    getsockopt(int s, Option& ov, Rest&... rest) {
        using native_type = detail::sys_native_sockopt<typename Option::type>;
        // All socket options MUST have a level and a name
        socklen_t                  opt_len{ sizeof(typename native_type::type) };
        const int                  level = etdc::get_tag_p<has_level_tag, Option>::type::type::value;
        const int                  opt_name = etdc::get_tag_p<has_name_tag,  Option>::type::type::value;
        typename native_type::type opt_val;

        if( ::getsockopt(s, level, opt_name, (void*)&opt_val, &opt_len)!=0 )
            throw std::runtime_error("Failed to get socket option "+detail::option_str(opt_name)+": "+
                                     etdc::strerror(errno)+"/fd="+etdc::repr(s));
        if( opt_len!=sizeof(typename native_type::type) )
            throw std::domain_error(std::string("getsockopt: returned option_value size (")+etdc::repr(opt_len)+") " +
                                    "does not match native size ("+etdc::repr(sizeof(typename native_type::type))+")" +
                                    "/fd="+etdc::repr(s));

        // Transform from native to actual type and copy into the parameter
        untag( ov ) = native_type::from_native( opt_val );

        // OK, this option done, carry on with rest
        return 1+getsockopt(s, std::forward<Rest&>(rest)...);
    }
}

#endif
