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
#include <udt.h>

#include <errno.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <stdexcept>

namespace etdc {

    // We're going to tag our socket options
    namespace tags {
        struct level         {}; // the tagged value is the level value of set/getsockopt(2)
        struct option_name   {}; //         ,,              option_name      ,,
        struct is_udt_option {};
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
        using SimpleSocketOption = SocketOption<int, Name<option_name>, Level<SOL_SOCKET>>;

        // According to http://udt.sourceforge.net/udt4/doc/opt.htm the level is ignored:
        //
        // int setsockopt(UDTSOCKET u, int level, SOCKOPT optname, const char* optval, int optlen)
        //
        // Parameters:
        //  u       [in] Descriptor identifying a UDT socket.
        //  level   [in] Unused. For compatibility only.
        //  optName [in] The enum name of UDT option. The names and meanings are ...
        template <UDTOpt udtname> 
        using SimpleUDTOption    = SocketOption<int, Name<udtname>, Level<-1>, tags::is_udt_option>;
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
#if 0
    template <typename T, typename...>
    struct has_udt_tag: std::false_type {};

    template <typename T, typename... Ts>
    struct has_udt_tag<tagged<T, Ts...>>: etdc::has_type<tags::is_udt_option, Ts...> {};
#endif
    ////////////////////////////////////////////////////////////////////////////////////////
    //
    //                   The socket options we support
    //
    //  This is what it's all about: we encode all vital information in the type
    //
    ////////////////////////////////////////////////////////////////////////////////////////
    using so_sndbuf     = SimpleSocketOption<SO_SNDBUF>;
    using so_rcvbuf     = SimpleSocketOption<SO_RCVBUF>;
    using so_reuseaddr  = SimpleSocketOption<SO_REUSEADDR>;
    using so_rcvtimeo   = SocketOption<struct timeval, Level<SOL_SOCKET>, Name<SO_RCVTIMEO>>;
    using tcp_nodelay   = SocketOption<int, Name<TCP_NODELAY>, Level<IPPROTO_TCP>>;
    using udt_mss       = SimpleUDTOption<UDT_MSS>;
    using udt_sndbuf    = SimpleUDTOption<UDT_SNDBUF>;
    using udt_rcvbuf    = SimpleUDTOption<UDT_RCVBUF>;
    using udt_reuseaddr = SimpleUDTOption<UDT_REUSEADDR>;


    // And templated set/getsockopt methods that can be used to set/get
    // multiple socket options in 'one' go. Exception will be raised in case of wonky

    void setsockopt(int) {} 

    template <typename Option, typename... Rest>
    void setsockopt(int s, Option const& ov, Rest... rest) {
        // All socket options MUST have a level and a name
        const int level    = etdc::get_tag_p<has_level_tag, Option>::type::type::value;
        const int opt_name = etdc::get_tag_p<has_name_tag,  Option>::type::type::value;
        const bool is_udt  = etdc::has_tag<tags::is_udt_option, Option>::type::value;

        if( is_udt ) {
            if( UDT::setsockopt(s, opt_name, level, (char const*)&untag(ov).get(), int(sizeof(Option::type)))==0 ) {
                UDT::ERRORINFO const& udterr( UDT::getlasterror() );
                throw std::runtime_error("Failed to set UDT option "+repr(opt_name)+": "+
                                          udterr.getErrorMessage()+" ("+repr(udterr.getErrorCode()));
            }
        } else {
            if( ::setsockopt(s, opt_name, level, (void*)&untag(ov).get(), socklen_t(sizeof(Option::type)))==0 )
                throw std::runtime_error("Failed to set socket option "+repr(opt_name)+": "+::strerror(errno));
        }
        // OK, this option done, carry on with rest
        setsockopt(s, std::forward<Rest>(rest)...);
    }

    template <typename Option, typename... Rest>
    void getsockopt(int s, Option& ov, Rest... rest) {
        // All socket options MUST have a level and a name
        const int level    = etdc::get_tag_p<has_level_tag, Option>::type::type::value;
        const int opt_name = etdc::get_tag_p<has_name_tag,  Option>::type::type::value;
        const bool is_udt  = etdc::has_tag<tags::is_udt_option, Option>::type::value;

        if( is_udt ) {
            int   sl{ sizeof(Option::type) };
            if( UDT::getsockopt(s, opt_name, level, (char const*)&untag(ov).get(), &sl)==0 ) {
                UDT::ERRORINFO const& udterr( UDT::getlasterror() );
                throw std::runtime_error("Failed to get UDT option "+repr(opt_name)+": "+
                                          udterr.getErrorMessage()+" ("+repr(udterr.getErrorCode()));
            }
        } else {
            socklen_t   sl{ sizeof(Option::type) };
            if( ::getsockopt(s, opt_name, level, (void*)&untag(ov).get(), &sl)==0 )
                throw std::runtime_error("Failed to get socket option "+repr(opt_name)+": "+::strerror(errno));
        }
        // OK, this option done, carry on with rest
        getsockopt(s, std::forward<Rest>(rest)...);
    }
#if 0
    template <template <typename...> class Option, typename T, typename... Tags, typename... Rest>
    void setsockopt(int, Option<T, Tags...> const& ov, Rest... rest) {
        // Need to extract the level and the name from Tags...
        std::cout << "Setting socket option/tags, value=" << ov << std::endl;
        std::cout << "  has_tag/cruft::level_tag = " << etdc::has_type<cruft::level_tag, Tags...>::value << std::endl;
        std::cout << "  has_pred<is_level, Tags...>::type::value = " << has_pred<is_level, Tags...>::type::value << std::endl;
        std::cout << "  type2str<Tags...> = " << type2str<Tags...>() << std::endl;
        std::cout << "  type2str<xform<is_level,Tags...>> = " << type2str<typename xform<is_level,Tags...>::type>() << std::endl;
        setSocketOption(0, std::forward<Rest>(rest)...);
    }
#endif
}

#endif
