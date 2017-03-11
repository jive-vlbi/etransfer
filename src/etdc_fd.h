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
#include <etdc_setsockopt.h>
#include <etdc_resolve.h>
#include <construct.h>
#include <reentrant.h>
#include <tagged.h>
#include <udt.h>

// C++
#include <map>
#include <tuple>
#include <memory>
#include <string>
#include <iostream>

#include <sys/socket.h>


// Make <host> and <protocol> constructible from std::string (and usable as ~)
// but you cannot mix them - they become their own type
namespace etdc {
    class host_type     : public std::string { using std::string::string; };
    class protocol_type : public std::string { using std::string::string; };
    //enum class port_type: unsigned short { any = 0 };
    namespace tags {
        struct mtu_tag     {};
        struct port_tag    {};
        struct backlog_tag {};
    }

    using mtu_type     = etdc::tagged<unsigned int, tags::mtu_tag>; // MTU<0 don't make sense
    using port_type    = etdc::tagged<unsigned short, tags::port_tag>;
    using backlog_type = etdc::tagged<int, tags::backlog_tag>;
    static constexpr port_type any_port = port_type{ (unsigned short)0 };

    // ipport_type:   <host> : <port>
    // sockname_type: <type> : <host> : <port>
    using ipport_type   = std::tuple<host_type, port_type>;
    using sockname_type = std::tuple<protocol_type, host_type, port_type>;

    // A wrapped file descriptor abstract base class (interface)
    class etdc_fd {

        public:
            int __m_fd {};

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

            virtual int           connect( ipport_type const& ) NOTIMPLEMENTED;

            // server-style interface
            virtual int           bind( ipport_type const& ) NOTIMPLEMENTED;
            virtual int           listen( int ) NOTIMPLEMENTED;
            virtual etdc_fd*      accept( void ) NOTIMPLEMENTED;
    };

    //////////////////////////////////////////////////////////////////
    //
    //                  Concrete derived classes
    //
    //////////////////////////////////////////////////////////////////

    class etdc_tcp:
        public etdc_fd
    {
        public:
            etdc_tcp();
            virtual ~etdc_tcp();

            //int __m_fd {};
    };

    class etdc_udt:
        public etdc_fd
    {
        public:
            // For the data channel we want BIG buffers, typically!
            // these are defaults and can be overridden by the usert
            //constexpr static int bufSize = {320*1024*1024};
            //constexpr extern static int bufSize = {320*1024*1024};
            //const static int bufSize = 320*1024*1024;

            etdc_udt();

            virtual ~etdc_udt();

            //UDTSOCKET __m_fd;
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
etdc::ipport_type mk_ipport(T const& host, etdc::port_type port = etdc::any_port) {
    return etdc::ipport_type(host, port);
}

template <typename T, typename U>
etdc::sockname_type mk_sockname(T const& proto, U const& host, etdc::port_type port = etdc::any_port) {
    return etdc::sockname_type(proto, host, port);
}

///////////////////////////////////////////////////////////////////////////
//
//   Only accept arguments that are sensible to convert to a port number
//
///////////////////////////////////////////////////////////////////////////
template <typename T>
typename std::enable_if<etdc::is_integer_number_type<T>::value, etdc::port_type>::type port(T const& p) {
    // Assert 0 <= port number <= 65535
    if( p<0 || p>65536 )
        throw std::domain_error("Port number must be 0 <= p <= 65535");
    return static_cast<etdc::port_type>(p);
}

// For everything else we attempt string => number [so we can also accept wstring and god knows what
template <typename T>
typename std::enable_if<!etdc::is_integer_number_type<T>::value, etdc::port_type>::type port(T const& s) {
    return mk_port( std::stoi(s) );
}


// And making a host
template <typename T>
etdc::host_type host(T const& t) {
    return etdc::host_type(t);
}
template <typename T>
etdc::protocol_type proto(T const& t) {
    return etdc::protocol_type(t);
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


namespace etdc {
    //////////////////////////////////////////////////////////////////
    //
    //                  Factories
    //
    //////////////////////////////////////////////////////////////////

    using etdc_fdptr        = std::shared_ptr<etdc_fd>;

    namespace detail {
        //constexpr static int udtBufSize = {320*1024*1024};
        constexpr static udt_rcvbuf defaultUDTBufSize{ 320*1024*1024 };

        // For creating sokkits
        using protocol_map_type = std::map<std::string, std::function<etdc_fdptr(void)>>;

        static const  protocol_map_type protocol_map = { 
            {"tcp", []() { return std::make_shared<etdc_tcp>(); }},
            {"udt", []() { return std::make_shared<etdc_udt>(); }}
        };


        struct server_settings {
            backlog_type     backLog    {};
            ipport_type      srvAddress {};
            etdc::so_rcvbuf  rcvBufSize {};
            etdc::udt_rcvbuf udtBufSize {};
        };
        const etdc::construct<server_settings>  update_srv( &server_settings::backLog,
                                                            &server_settings::srvAddress,
                                                            &server_settings::rcvBufSize,
                                                            &server_settings::udtBufSize );

        using server_defaults_map = std::map<std::string, std::function<server_settings(void)>>;

        static const server_defaults_map server_defaults = {
            {"tcp", []() { return update_srv.mk(backlog_type{4}, mk_ipport("0.0.0.0") );}},
            {"udt", []() { return update_srv.mk(backlog_type{4}, defaultUDTBufSize, mk_ipport("0.0.0.0"));}}
            //{"udt", []() { return update_srv.mk(backlog_type{4}, etdc::udt_rcvbuf{etdc::etdc_udt::bufSize}, mk_ipport("0.0.0.0"));}}
        };

        // Basic transformation from plain socket into server or client socket
        using xform_map_type = std::map<std::string, std::function<void(etdc_fdptr, detail::server_settings const&)>>;


        // Default set of actions to turn a socket into a server socket
        static const xform_map_type server_map = {
            {"tcp", [](etdc_fdptr pSok, detail::server_settings const& srv) -> void {
                        // Bind to ipport
                        struct sockaddr_in sa;
                        
                        etdc::setsockopt(pSok->__m_fd, etdc::so_reuseaddr{true});

                        // Need to resolve? For here we assume empty host means any
                        if( !etdc::resolve_host<etdc::EmptyMeansAny>(get_host(srv.srvAddress), SOCK_STREAM, IPPROTO_TCP, sa) )
                            throw std::runtime_error( std::string("Failed to resolve/tcp ")+get_host(srv.srvAddress) );

                        // Get the port info
                        // See "etdc_resolve.h" for FFS glibc shit why we
                        // have to fucking manually wrap htons!!
                        sa.sin_port = etdc::htons_( get_port(srv.srvAddress) );

                        // Now we can bind(2)
                        if( ::bind(pSok->__m_fd, (const struct sockaddr*)&sa, socklen_t(sizeof(struct sockaddr_in)))!=0 )
                            throw std::runtime_error( std::string("Failed to bind/tcp - ")+etdc::strerror(errno) );

                        // And also lissen(2)
                        if( ::listen(pSok->__m_fd, 4)!=0 )
                            throw std::runtime_error( std::string("Failed to listen/tcp - ")+etdc::strerror(errno) );
                    }},
            {"udt", [](etdc_fdptr pSok, detail::server_settings const& srv) -> void {
                        // Bind to ipport
                        struct sockaddr_in sa;

                        etdc::setsockopt(pSok->__m_fd, etdc::so_reuseaddr{true},
                                                       etdc::udt_reuseaddr{true},
                                                       srv.udtBufSize);
                        // Need to resolve? For here we assume empty host means any
                        if( !etdc::resolve_host<etdc::EmptyMeansAny>(get_host(srv.srvAddress), SOCK_STREAM, IPPROTO_TCP, sa) )
                            throw std::runtime_error( std::string("Failed to resolve/udt ")+get_host(srv.srvAddress) );

                        // Get the port info
                        // See "etdc_resolve.h" for FFS glibc shit why we
                        // have to fucking manually wrap htons!!
                        sa.sin_port = etdc::htons_( get_port(srv.srvAddress) );

                        // Now we can bind(2)
                        if( UDT::bind(pSok->__m_fd, (const struct sockaddr*)&sa, socklen_t(sizeof(struct sockaddr_in)))!=0 )
                            throw std::runtime_error( std::string("Failed to bind/udt - ")+UDT::getlasterror().getErrorMessage() );

                        // And also lissen(2)
                        if( UDT::listen(pSok->__m_fd, 4)!=0 )
                            throw std::runtime_error( std::string("Failed to listen/udt - ")+UDT::getlasterror().getErrorMessage() );
                    }}
        };
        // Default id. actions to turn a socket into a client socket
        static const xform_map_type client_map = {
            {"tcp", [](etdc_fdptr /*pSok*/, detail::server_settings const&) {
                    }},
            {"udt", [](etdc_fdptr /*pSok*/, detail::server_settings const&) {
                    }}
        };
    }
}

///////////////////////////////////////////////////////////////////////////////////////
//
//                      These live in the global namespace again
//
///////////////////////////////////////////////////////////////////////////////////////

template <typename T>
etdc::etdc_fdptr mk_socket(T const& proto) {
    auto pEntry = etdc::detail::protocol_map.find(proto);

    if( pEntry==etdc::detail::protocol_map.end() )
        throw std::runtime_error(std::string("mk_socket/No protocol entry found for protocol = ")+std::string(proto));
    return pEntry->second();
}


template <typename T, typename... Ts>
etdc::etdc_fdptr mk_server(T const& proto, Ts... ts) {
    // Create socket and server defaults for the indicated protocol
    auto pSok         = mk_socket(proto);
    auto srvDefaults  = etdc::detail::server_defaults.find(proto)->second();
    // Update the defaults with what the user may have given us
    etdc::detail::update_srv(srvDefaults, std::forward<Ts>(ts)...);

    // And now transform the server details + sokkit into a real servert
    return etdc::detail::server_map.find(proto)->second(pSok, srvDefaults);
#if 0
    auto pSrvEntry    = etdc::detail::server_map.find(proto);
    if( pEntry==etdc::detail::server_map.end() )
        throw std::runtime_error(std::string("mk_server/No server entry found for protocol = ")+std::string(proto));
    return pSrvEntry->second( pSok );
#endif
}

#endif
