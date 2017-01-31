// base- and derived classes for wrapping file descriptors
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
#ifndef ETDC_ETDC_FD_H
#define ETDC_ETDC_FD_H

// Own includes
#include <notimplemented.h>
#include <utilities.h>

// C++
#include <tuple>
#include <string>

#if 0
#include <iostream>
#include <sstream>
#include <string>

#include <sys/time.h>   // struct timeval
#include <sys/socket.h> // SOL_SOCKET

template <class CharT, class Traits>
std::basic_ostream<CharT, Traits>&  operator<<(std::basic_ostream<CharT, Traits>& os, struct timeval const& tv) {
    return os << "struct timeval{.tv_sec=" << tv.tv_sec << ", .tv_usec=" << tv.tv_usec << "}";
}
#endif
#if 0
namespace cruft {
    struct host_tag {};
    struct protocol_tag {};
    struct port_tag {};
    struct name_tag {};
    struct level_tag {};
}
using host_type     = tagged<std::string, cruft::host_tag>;
using protocol_type = tagged<std::string, cruft::protocol_tag>;

template <int Name>
using name_type     = tagged<std::integral_constant<int,Name>, cruft::name_tag>;
template <int Level>
using level_type    = tagged<std::integral_constant<int,Level>, cruft::level_tag>;

#include <netinet/in.h>
#include <netinet/tcp.h>
using socket_level  = level_type<SOL_SOCKET>;
using tcp_level     = level_type<IPPROTO_TCP>;

using sndbuf_type   = tagged<int, level_type<SOL_SOCKET>, name_type<SO_SNDBUF>>;
using rcvbuf_type   = tagged<int, level_type<SOL_SOCKET>, name_type<SO_RCVBUF>>;
using rcvbufd_type  = tagged<double, level_type<SOL_SOCKET>, name_type<SO_RCVBUF>>;
using rcvtimeo_type = tagged<struct timeval, name_type<SO_RCVTIMEO>, level_type<SOL_SOCKET>>;
using tcp_nodelay   = tagged<int, tcp_level, name_type<TCP_NODELAY>>;

template <template <typename...> class Pred, typename...>
struct get_tag_p {
    using type = std::tuple<>;
};

template <template <typename...> class Pred, typename T, typename... Tags>
struct get_tag_p<Pred, tagged<T, Tags...>> {
    using result_type  = typename etdc::apply<Pred, Tags...>::type;

    using type = typename std::conditional<etdc::has_type<std::true_type, result_type>::value,
                                           typename std::tuple_element<etdc::index_of<std::true_type, result_type>::value, std::tuple<Tags...>>::type,
                                           std::tuple<>>::type;
};


    std::cout << "level = " << get_tag_p<has_level_tag, tcp_nodelay>::type::type::value << std::endl;
    std::cout << "level = " << get_tag_p<has_level_tag, rcvtimeo_type>::type::type::value << std::endl;

#endif
#include <iostream>

// Make <host> and <protocol> constructible from std::string (and usable as ~)
// but you cannot mix them - they become their own type
namespace etdc {
    class host_type     : public std::string { using std::string::string; };
    class protocol_type : public std::string { using std::string::string; };
    enum class port_type: unsigned short { any = 0 };

    std::ostream& operator<<(std::ostream& os, port_type const& p) {
        return os << static_cast<unsigned short>(p);
    }

    // ipport_type:   <host> : <port>
    // sockname_type: <type> : <host> : <port>
    using ipport_type   = std::tuple<host_type, port_type>;
    using sockname_type = std::tuple<protocol_type, host_type, port_type>;

    // A wrapped file descriptor abstract base class (interface)
    class etdc_fd {

        public:
            // We pretend to be just an interface
            etdc_fd() {}
            virtual ~etdc_fd() {}

            // methods that had preferrably be overridden by concrete implementations before them's being called
            // Note that the base class versions, if they get called, throw a not-implemented-here exception
            // This allows derived classes to skip implementing functions as long as they're not being called ...

            virtual sockname_type getsockname( void ) NOTIMPLEMENTED;
            virtual sockname_type getpeername( void ) NOTIMPLEMENTED;

            virtual ssize_t       read(void* /*buf*/, size_t /*n*/) NOTIMPLEMENTED;
            virtual ssize_t       write(const void* /*buf*/, size_t /*n*/) NOTIMPLEMENTED;

            virtual int           seek(off_t /*off*/, int /*whence*/) NOTIMPLEMENTED;
            virtual off_t         tell( void ) NOTIMPLEMENTED;

            virtual off_t         size( void ) NOTIMPLEMENTED;
            virtual int           close( void ) NOTIMPLEMENTED;

            // server-style interface
            virtual int           bind( ipport_type const& ) NOTIMPLEMENTED;
            virtual int           listen( int ) NOTIMPLEMENTED;
            virtual etdc_fd*      accept( void ) NOTIMPLEMENTED;
    };
#undef NOTIMPLEMENTED
#undef THISFUNCTION

    //////////////////////////////////////////////////////////////////
    //
    //                  Concrete derived classes
    //
    //////////////////////////////////////////////////////////////////

    class etdc_tcp:
        public etdc_fd
    {
        public:
            etdc_tcp() {}
            virtual ~etdc_tcp() {}

    };
// End of the etdc namespace
}

///////////////////////////////////////////////////////////////////////////////////////
//
//                      These live in the global namespace
//
///////////////////////////////////////////////////////////////////////////////////////

// Functions to create an instance
template <typename T>
etdc::ipport_type mk_ipport(T const& host, etdc::port_type port = etdc::port_type::any) {
    return etdc::ipport_type(host, port);
}

template <typename T, typename U>
etdc::sockname_type mk_sockname(T const& proto, U const& host, etdc::port_type port = etdc::port_type::any) {
    return etdc::sockname_type(proto, host, port);
}

///////////////////////////////////////////////////////////////////////////
//
//   Only accept arguments that are sensible to convert to a port number
//
///////////////////////////////////////////////////////////////////////////
template <typename T>
typename std::enable_if<etdc::is_integer_number_type<T>::value, etdc::port_type>::type mk_port(T const& p) {
    // Assert 0 <= port number <= 65535
    if( p<0 || p>65536 )
        throw std::domain_error("Port number must be 0 <= p <= 65535");
    return static_cast<etdc::port_type>(p);
}

// For everything else we attempt string => number [so we can also accept wstring and god knows what
template <typename T>
typename std::enable_if<!etdc::is_integer_number_type<T>::value, etdc::port_type>::type mk_port(T const& s) {
    return mk_port( std::stoi(s) );
}



///////////////////////////////////////////////////////////////////////////
//
//   Extract useful information from types
//
///////////////////////////////////////////////////////////////////////////

// Extract the (first) host_typed value out of whatever was passed 
template <typename... Ts>
etdc::host_type get_host(Ts... t) {
    return std::get< etdc::index_of<etdc::host_type, Ts...>::value >( t... );
}

template <typename... Ts>
etdc::port_type get_port(Ts... t) {
    return std::get< etdc::index_of<etdc::port_type, Ts...>::value >( t... );
}

template <typename... Ts>
etdc::protocol_type get_protocol(Ts... t) {
    return std::get< etdc::index_of<etdc::protocol_type, Ts...>::value >( t... );
}




#endif
