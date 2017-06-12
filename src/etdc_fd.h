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
#include <etdc_assert.h>
#include <etdc_debug.h>
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
#include <functional>

#include <sys/socket.h>


// Define global ostream operator for struct sockaddr_in[6], dat's handy
// (forward declaration) - implementation at end of file
template <class CharT, class Traits>
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, struct sockaddr_in const& sa);

template <class CharT, class Traits>
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, struct sockaddr_in6 const& sa);


// Make <host> and <protocol> constructible from std::string (and usable as ~)
// but you cannot mix them - they become their own type
namespace etdc {
    // set file descriptor in blocking or non-blocking mode
    void setfdblockingmode(int fd, bool blocking);


    ////////////////////////////////////////////////////////////////////////////////
    //         support types for protocols/destination &cet
    ////////////////////////////////////////////////////////////////////////////////
    class host_type     : public std::string {
        public:
            template <typename... Args>
            host_type(Args... args): std::string(std::forward<Args>(args)...) {}
    };
    class protocol_type : public std::string { 
        public:
            template <typename... Args>
            protocol_type(Args... args): std::string(std::forward<Args>(args)...) {}
    };
    //enum class port_type: unsigned short { any = 0 };
    namespace tags {
        struct mtu_tag      {};
        struct port_tag     {};
        struct backlog_tag  {};
        struct blocking_tag {};
    }

    using mtu_type      = etdc::tagged<unsigned int, tags::mtu_tag>; // MTU<0 don't make sense
    using port_type     = etdc::tagged<unsigned short, tags::port_tag>;
    using backlog_type  = etdc::tagged<int, tags::backlog_tag>;
    using blocking_type = etdc::tagged<bool, tags::blocking_tag>;
    static constexpr port_type any_port = port_type{ (unsigned short)0 };

    // ipport_type:   <host> : <port>
    // sockname_type: <type> : <host> : <port>
    using ipport_type   = std::tuple<host_type, port_type>;
    using sockname_type = std::tuple<protocol_type, host_type, port_type>;

    template <class CharT, class Traits>
    std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, ipport_type const& ipport) {
        return os << std::get<0>(ipport) << ":" << std::get<1>(ipport);
    }
    template <class CharT, class Traits>
    std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, sockname_type const& sn) {
        return os << "<" << std::get<0>(sn) << "/" << std::get<1>(sn) << ":" << std::get<2>(sn) << ">";
    }
    // Forward declare
    struct etdc_fd;
    using etdc_fdptr     = std::shared_ptr<etdc_fd>;

    namespace detail {
        struct connect_tag  {};
        struct bind_tag     {};
        struct sockname_tag {};
        struct peername_tag {};
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    //                      Prototypes for calls on file descriptors
    //
    //////////////////////////////////////////////////////////////////////////////////////////////////////////
    using read_fn        = std::function<ssize_t(int, void*, size_t)>;
    using write_fn       = std::function<ssize_t(int, const void*, size_t)>;
    using close_fn       = std::function<int(int)>;
    using lseek_fn       = std::function<off_t(int, off_t, int)>;
    // connect and bind have same signature but we must be able to tell'm
    // apart so we use tagging!
    //using connect_fn     = etdc::tagged<std::function<int(int, ipport_type const&)>, detail::connect_tag>;
    //using bind_fn        = etdc::tagged<std::function<int(int, ipport_type const&)>, detail::bind_tag   >;
    //using listen_fn      = std::function<int(int, int)>;
    //using accept_fn      = std::function<etdc_fdptr(int, struct sockaddr*, socklen_t*)>;
    using accept_fn      = std::function<etdc_fdptr(int)>;
    // Get either end of the socket connection
    using getsockname_fn = etdc::tagged<std::function<sockname_type(int)>, detail::sockname_tag>;
    using getpeername_fn = etdc::tagged<std::function<sockname_type(int)>, detail::peername_tag>;
    using setblocking_fn = std::function<void(int, bool)>;

    // A wrapped file descriptor - the actual systemcalls travel with the fd
    // such that we can write functions that can call the appropriate
    // methods to their own liking (e.g. writing a big block in smaller
    // chunks or whatever
    struct etdc_fd {

        int __m_fd {};

        // We pretend to be just an interface
        explicit etdc_fd();
        virtual ~etdc_fd();

        // Functionpointers
        read_fn        read;
        write_fn       write;
        close_fn       close;
        lseek_fn       lseek;
        //connect_fn     connect;
        //bind_fn        bind;
        //listen_fn      listen;
        accept_fn      accept;
        getsockname_fn getsockname;
        getpeername_fn getpeername;
        setblocking_fn setblocking;
    };

    static const etdc::construct<etdc_fd> update_fd( &etdc_fd::read, &etdc_fd::write, &etdc_fd::close, &etdc_fd::accept,
                                                     &etdc_fd::getsockname, &etdc_fd::getpeername, &etdc_fd::setblocking );

    //////////////////////////////////////////////////////////////////
    //
    //                  Concrete derived classes
    //
    //////////////////////////////////////////////////////////////////

    // A TCP socket for IPv4
    struct etdc_tcp:
        public etdc_fd
    {
        etdc_tcp();
        etdc_tcp(int fd); // take over a file descriptor e.g. from ::accept()
        virtual ~etdc_tcp();

        protected:
            void setup_basic_fns( void );
    };

    // id. for IPv6
    struct etdc_tcp6:
        public etdc_tcp
    {
        etdc_tcp6();
        etdc_tcp6(int fd); // take over a file descriptor e.g. from ::accept()
        virtual ~etdc_tcp6();

        private:
            void setup_basic_fns( void );
    };

    // An UDT socket
    struct etdc_udt:
        public etdc_fd
    {
        etdc_udt();
        etdc_udt(int fd);

        virtual ~etdc_udt();

        protected:
            void setup_basic_fns( void );
    };

    struct etdc_udt6:
        public etdc_udt
    {
        etdc_udt6();
        etdc_udt6(int fd); // take over a file descriptor e.g. from ::accept()
        virtual ~etdc_udt6();

        private:
            void setup_basic_fns( void );
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
    // Only accept port numbers that are actual valid port numbers
    ETDCASSERT(p>=0 && p<=65535, "Port number " << p << " out of range");
    return static_cast<etdc::port_type>(p);
}

// For everything else we attempt string => number [so we can also accept wstring and god knows what
template <typename T>
typename std::enable_if<!etdc::is_integer_number_type<T>::value, etdc::port_type>::type port(T const& s) {
    return port( std::stoi(s) );
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


    namespace detail {
        constexpr static int defaultUDTBufSize{ 320*1024*1024 };

        // For creating sokkits
        using protocol_map_type = std::map<std::string, std::function<etdc_fdptr(void)>>;

        static const  protocol_map_type protocol_map = { 
            {"tcp",  []() { return std::make_shared<etdc_tcp>();  }},
            {"tcp6", []() { return std::make_shared<etdc_tcp6>(); }},
            {"udt",  []() { return std::make_shared<etdc_udt>();  }},
            {"udt6", []() { return std::make_shared<etdc_udt6>(); }}
        };


        struct server_settings {
            blocking_type    blocking   {};
            backlog_type     backLog    {};
            host_type        srvHost    {}; // empty host for servers means 0.0.0.0 anyway
            port_type        srvPort    {};
            etdc::udt_mss    udtMSS     {};
            etdc::so_rcvbuf  rcvBufSize {};
            etdc::udt_rcvbuf udtBufSize {};
            etdc::ipv6_only  ipv6_only  {};
        };
        const etdc::construct<server_settings>  update_srv( &server_settings::blocking,
                                                            &server_settings::backLog,
                                                            &server_settings::srvHost,
                                                            &server_settings::srvPort,
                                                            &server_settings::rcvBufSize,
                                                            &server_settings::udtBufSize,
                                                            &server_settings::udtMSS,
                                                            &server_settings::ipv6_only );

        using server_defaults_map = std::map<std::string, std::function<server_settings(void)>>;

        // server defaults per protocol type
        static const server_defaults_map server_defaults = {
            {"tcp", []() { return update_srv.mk(backlog_type{4},
                                                any_port,
                                                blocking_type{true} );
                         }},
            {"tcp6", []() { return update_srv.mk(backlog_type{4},
                                                any_port, etdc::ipv6_only{true},
                                                blocking_type{true} );
                         }},
            {"udt", []() { return update_srv.mk(backlog_type{4},
                                                blocking_type{true},
                                                etdc::udt_rcvbuf{defaultUDTBufSize},
                                                any_port,
                                                etdc::udt_mss{1500});
                         }},
            {"udt6", []() { return update_srv.mk(backlog_type{4},
                                                blocking_type{true},
                                                etdc::udt_rcvbuf{defaultUDTBufSize},
                                                any_port, etdc::ipv6_only{true},
                                                etdc::udt_mss{1500});
                         }}
        };

        // Basic transformation from plain socket into server 
        //using server_map_type = std::map<std::string, std::function<void(etdc_fdptr, detail::server_settings const&)>>;

        // Default set of actions to turn a socket into a server socket
        //server_map_type server_map = {
        static const std::map<std::string, std::function<void(etdc_fdptr, detail::server_settings const&)>> server_map = {
            ////////// TCP server (IPv4)
            {"tcp", [](etdc_fdptr pSok, detail::server_settings const& srv) -> void {
                        // Bind to ipport
                        socklen_t          sl( sizeof(struct sockaddr_in) );
                        struct sockaddr_in sa;
                       
                        // Need to resolve? For here we assume empty host means any
                        //ETDCSYSCALL(etdc::resolve_host<etdc::EmptyMeansAny>(srv.srvHost, SOCK_STREAM, IPPROTO_TCP, sa),
                        ETDCSYSCALL(etdc::resolve_host<etdc::EmptyMeansAny>(srv.srvHost, SOCK_STREAM, IPPROTO_IP, sa),
                                    "Failed to resolve/tcp '" << srv.srvHost << "'");

                        // Set socket options 
                        etdc::setsockopt(pSok->__m_fd, etdc::so_reuseaddr{true});
                        if( srv.rcvBufSize )
                            etdc::setsockopt(pSok->__m_fd, srv.rcvBufSize);

                        // Get the port info
                        // See "etdc_resolve.h" for FFS glibc shit why we
                        // have to fucking manually wrap htons!!
                        sa.sin_port = etdc::htons_( srv.srvPort );

                        // Make sure sokkit is in correct blocking mode
                        pSok->setblocking(pSok->__m_fd, etdc::untag(srv.blocking));

                        // Now we can bind(2)
                        ETDCSYSCALL(::bind(pSok->__m_fd, reinterpret_cast<const struct sockaddr*>(&sa), sl)==0,
                                    "binding to tcp[" << sa << "] - " << etdc::strerror(errno) );

                        // And also lissen(2)
                        ETDCSYSCALL(::listen(pSok->__m_fd, etdc::untag(srv.backLog))==0,
                                    "listening on tcp[" << sa << "] - " << etdc::strerror(errno));

                        // And we can now actually enable the accept function
                        pSok->accept = [=](int f) {
                            socklen_t           ipl( sizeof(struct sockaddr_in) );
                            struct sockaddr_in  ip;
                            int                 fd = ::accept(f, reinterpret_cast<struct sockaddr*>(&ip), &ipl);

                            // fd<0 is not an error if blocking + errno == EAGAIN || EWOULDBLOCK
                            ETDCSYSCALL(fd>0 || (!srv.blocking && fd==-1 && (errno==EAGAIN || errno==EWOULDBLOCK)),
                                        "failed to accept on tcp[" << sa << "] - " << etdc::strerror(errno));
                            return (fd==-1) ? std::shared_ptr<etdc_fd>() : std::make_shared<etdc::etdc_tcp>(fd);
                        };
                    }},
            ////////// TCP server (IPv6)
            {"tcp6", [](etdc_fdptr pSok, detail::server_settings const& srv) -> void {
                        // Bind to ipport
                        socklen_t           sl( sizeof(struct sockaddr_in6) );
                        struct sockaddr_in6 sa;
                       
                        // Need to resolve? For here we assume empty host means any
                        ETDCSYSCALL(etdc::resolve_host<etdc::EmptyMeansAny>(srv.srvHost, SOCK_STREAM, IPPROTO_IPV6, sa),
                                    "Failed to resolve/tcp6 '" << srv.srvHost << "'");

                        // Set socket options 
                        etdc::setsockopt(pSok->__m_fd, etdc::so_reuseaddr{true}, srv.ipv6_only);

                        // Override rcvbufsize only if actually set
                        if( srv.rcvBufSize )
                            etdc::setsockopt(pSok->__m_fd, srv.rcvBufSize);

                        // Get the port info
                        // See "etdc_resolve.h" for FFS glibc shit why we
                        // have to fucking manually wrap htons!!
                        sa.sin6_port = etdc::htons_( srv.srvPort );

                        // Make sure sokkit is in correct blocking mode
                        pSok->setblocking(pSok->__m_fd, etdc::untag(srv.blocking));

                        // Now we can bind(2)
                        ETDCSYSCALL(::bind(pSok->__m_fd, reinterpret_cast<const struct sockaddr*>(&sa), sl)==0,
                                    "binding to tcp6[" << sa << "] - " << etdc::strerror(errno) );

                        // And also lissen(2)
                        ETDCSYSCALL(::listen(pSok->__m_fd, etdc::untag(srv.backLog))==0,
                                    "listening on tcp6[" << sa << "] - " << etdc::strerror(errno));

                        // And we can now actually enable the accept function
                        pSok->accept = [=](int f) {
                            ETDCDEBUG(2, "waiting for incoming TCP6 connection ..." << std::endl);
                            socklen_t            ipl( sizeof(struct sockaddr_in6) );
                            struct sockaddr_in6  ip;
                            int                  fd = ::accept(f, reinterpret_cast<struct sockaddr*>(&ip), &ipl);

                            ETDCDEBUG(2, "OK accept6 returned fd=" << fd << std::endl);
                            // fd<0 is not an error if blocking + errno == EAGAIN || EWOULDBLOCK
                            ETDCSYSCALL(fd>0 || (!srv.blocking && fd==-1 && (errno==EAGAIN || errno==EWOULDBLOCK)),
                                        "failed to accept on tcp6[" << sa << "] - " << etdc::strerror(errno));
                            return (fd==-1) ? std::shared_ptr<etdc_fd>() : std::make_shared<etdc::etdc_tcp6>(fd);
                        };
                    }},
            ////////// UDT server  (IPv4)
            {"udt", [](etdc_fdptr pSok, detail::server_settings const& srv) -> void {
                        // Bind to ipport
                        int                sl( sizeof(struct sockaddr_in) );
                        struct sockaddr_in sa;
                        
                        // Set a couple of socket options
                        etdc::setsockopt(pSok->__m_fd, etdc::udt_reuseaddr{true}, srv.udtBufSize, srv.udtMSS);

                        // Need to resolve? For here we assume empty host means any
                        ETDCSYSCALL(etdc::resolve_host<etdc::EmptyMeansAny>(srv.srvHost, SOCK_STREAM, IPPROTO_TCP, sa),
                                    "Failed to resolve/udt '" << srv.srvHost << "'");

                        // Get the port info
                        // See "etdc_resolve.h" for FFS glibc shit why we
                        // have to fucking manually wrap htons!!
                        sa.sin_port = etdc::htons_( srv.srvPort );

                        // Make sure sokkit is in correct blocking mode
                        pSok->setblocking(pSok->__m_fd, etdc::untag(srv.blocking));

                        // Now we can bind(2)
                        ETDCSYSCALL( UDT::bind(pSok->__m_fd, reinterpret_cast<const struct sockaddr*>(&sa), sl)!=UDT::ERROR,
                                     "binding to udt[" << sa << "] - " << UDT::getlasterror().getErrorMessage() );

                        // And also lissen(2)
                        ETDCSYSCALL( UDT::listen(pSok->__m_fd, etdc::untag(srv.backLog))!=UDT::ERROR,
                                     "listening on udt[" << sa << "] - " << UDT::getlasterror().getErrorMessage() );

                        // And we can now actually enable the accept function
                        pSok->accept = [=](int f) {
                            int                 ipl( sizeof(struct sockaddr_in) );
                            struct sockaddr_in  ip;
                            UDTSOCKET           fd = UDT::accept(f, reinterpret_cast<struct sockaddr*>(&ip), &ipl);
                            UDT::ERRORINFO      udterr( UDT::getlasterror() );

                            // UDT does things differently ... obviously
                            ETDCSYSCALL(fd!=UDT::INVALID_SOCK || /* This is never an error - a connection was accepted!*/
                                        (!srv.blocking && fd==UDT::INVALID_SOCK && udterr.getErrorCode()==CUDTException::EASYNCRCV),
                                        "failed to accept on udt[" << sa << "] - " << udterr.getErrorMessage());
                            return (fd==-1) ? std::shared_ptr<etdc_fd>() : std::make_shared<etdc::etdc_udt>(fd);
                        };
                    }},
            ////////// UDT server  (IPv6)
            {"udt6", [](etdc_fdptr pSok, detail::server_settings const& srv) -> void {
                        // Bind to ipport
                        int                 sl( sizeof(struct sockaddr_in6) );
                        struct sockaddr_in6 sa;
                        
                        // Set a couple of socket options
                        // Note: we cannot set the IPv6 only option through the UDT library at the moment
                        etdc::setsockopt(pSok->__m_fd, etdc::udt_reuseaddr{true}, srv.udtBufSize, srv.udtMSS/*, srv.ipv6_only*/);

                        // Need to resolve? For here we assume empty host means any
                        ETDCSYSCALL(etdc::resolve_host<etdc::EmptyMeansAny>(srv.srvHost, SOCK_STREAM, IPPROTO_IPV6, sa),
                                    "Failed to resolve/udt '" << srv.srvHost << "'");

                        // Get the port info
                        // See "etdc_resolve.h" for FFS glibc shit why we
                        // have to fucking manually wrap htons!!
                        sa.sin6_port = etdc::htons_( srv.srvPort );

                        // Make sure sokkit is in correct blocking mode
                        pSok->setblocking(pSok->__m_fd, etdc::untag(srv.blocking));

                        // Now we can bind(2)
                        ETDCSYSCALL( UDT::bind(pSok->__m_fd, reinterpret_cast<const struct sockaddr*>(&sa), sl)!=UDT::ERROR,
                                     "binding to udt6[" << sa << "] - " << UDT::getlasterror().getErrorMessage() );

                        // And also lissen(2)
                        ETDCSYSCALL( UDT::listen(pSok->__m_fd, etdc::untag(srv.backLog))!=UDT::ERROR,
                                     "listening on udt6[" << sa << "] - " << UDT::getlasterror().getErrorMessage() );

                        // And we can now actually enable the accept function
                        pSok->accept = [=](int f) {
                            int                  ipl( sizeof(struct sockaddr_in6) );
                            struct sockaddr_in6  ip;
                            UDTSOCKET            fd = UDT::accept(f, reinterpret_cast<struct sockaddr*>(&ip), &ipl);
                            UDT::ERRORINFO       udterr( UDT::getlasterror() );

                            // UDT does things differently ... obviously
                            ETDCSYSCALL(fd!=UDT::INVALID_SOCK || /* This is never an error - a connection was accepted!*/
                                        (!srv.blocking && fd==UDT::INVALID_SOCK && udterr.getErrorCode()==CUDTException::EASYNCRCV),
                                        "failed to accept on udt6[" << sa << "] - " << udterr.getErrorMessage());
                            return (fd==-1) ? std::shared_ptr<etdc_fd>() : std::make_shared<etdc::etdc_udt6>(fd);
                        };
                    }}
            /////////// More protocols may follow?
        };


        //////////////////////////////////////////////////////////////////////////////////////////////////////////
        //
        //        Repeat for client side of the connection
        //
        //////////////////////////////////////////////////////////////////////////////////////////////////////////

        struct client_settings {
            blocking_type    blocking   {};
            host_type        clntHost   {}; 
            port_type        clntPort   {};
            etdc::udt_mss    udtMSS     {};
            etdc::so_sndbuf  sndBufSize {};
            etdc::udt_sndbuf udtBufSize {};
            etdc::ipv6_only  ipv6_only  {};
        };
        const etdc::construct<client_settings>  update_clnt( &client_settings::blocking,
                                                             &client_settings::clntPort,
                                                             &client_settings::clntHost,
                                                             &client_settings::sndBufSize,
                                                             &client_settings::udtMSS,
                                                             &client_settings::udtBufSize,
                                                             &client_settings::ipv6_only );

        using client_defaults_map = std::map<std::string, std::function<client_settings(void)>>;

        // client defaults per protocol type
        static const client_defaults_map client_defaults = {
            {"tcp", []() { return update_clnt.mk(blocking_type{true},
                                                 any_port );
                         }},
            {"tcp6", []() { return update_clnt.mk(blocking_type{true}, etdc::ipv6_only{true},
                                                 any_port );
                         }},
            {"udt", []() { return update_clnt.mk(etdc::udt_mss{1500},
                                                 any_port,
                                                 etdc::udt_sndbuf{defaultUDTBufSize},
                                                 blocking_type{true});
                         }},
            {"udt6", []() { return update_clnt.mk(etdc::udt_mss{1500},
                                                 // UDT does not allow direct access to the real socket so we can't really
                                                 // set an option at the IPPROTO_IPV6 level.
                                                 any_port, /*etdc::ipv6_only{true},*/ 
                                                 etdc::udt_sndbuf{defaultUDTBufSize},
                                                 blocking_type{true});
                         }}
        };

        // Default actions to turn a socket into a client socket
        static const std::map<std::string, std::function<void(etdc_fdptr, detail::client_settings const&)>> client_map = {
            {"tcp", [](etdc_fdptr pSok, detail::client_settings const& clnt) {
                        // connect to ipport
                        socklen_t          sl( sizeof(struct sockaddr_in) );
                        struct sockaddr_in sa;
                        
                        // Need to resolve? For clients we assume empty host means not OK!
                        ETDCSYSCALL(etdc::resolve_host<etdc::EmptyMeansInvalid>(clnt.clntHost, SOCK_STREAM, IPPROTO_IP, sa),
                                    "Failed to resolve/tcp '" << clnt.clntHost << "'");

                        // Get the port info
                        // See "etdc_resolve.h" for FFS glibc shit why we
                        // have to fucking manually wrap htons!!
                        sa.sin_port = etdc::htons_( clnt.clntPort );

                        // Set socket options
                        if( clnt.sndBufSize )
                            etdc::setsockopt(pSok->__m_fd, clnt.sndBufSize);

                        // Make sure sokkit is in correct blocking mode
                        pSok->setblocking(pSok->__m_fd, etdc::untag(clnt.blocking));

                        // Connect
                        ETDCSYSCALL(::connect(pSok->__m_fd, reinterpret_cast<struct sockaddr const*>(&sa), sl)==0,
                                    "connecting to tcp[" << sa << "] - " << etdc::strerror(errno));
                        // Not much else to do ...
                    }},
            {"tcp6", [](etdc_fdptr pSok, detail::client_settings const& clnt) {
                        // connect to ipport
                        socklen_t           sl( sizeof(struct sockaddr_in6) );
                        struct sockaddr_in6 sa;
                        
                        // Need to resolve? For clients we assume empty host means not OK!
                        ETDCSYSCALL(etdc::resolve_host<etdc::EmptyMeansInvalid>(clnt.clntHost, SOCK_STREAM, IPPROTO_IPV6, sa),
                                    "Failed to resolve/tcp6 '" << clnt.clntHost << "'");

                        // Get the port info
                        // See "etdc_resolve.h" for FFS glibc shit why we
                        // have to fucking manually wrap htons!!
                        sa.sin6_port = etdc::htons_( clnt.clntPort );

                        // Set socket options
                        etdc::setsockopt(pSok->__m_fd, clnt.ipv6_only);

                        if( clnt.sndBufSize )
                            etdc::setsockopt(pSok->__m_fd, clnt.sndBufSize);

                        // Make sure sokkit is in correct blocking mode
                        pSok->setblocking(pSok->__m_fd, etdc::untag(clnt.blocking));

                        // Connect
                        ETDCSYSCALL(::connect(pSok->__m_fd, reinterpret_cast<struct sockaddr const*>(&sa), sl)==0,
                                    "connecting to tcp6[" << sa << "] - " << etdc::strerror(errno));
                        // Not much else to do ...
                    }},
            {"udt", [](etdc_fdptr pSok, detail::client_settings const& clnt) {
                        // connect to ipport
                        int                sl( sizeof(struct sockaddr_in) );
                        struct sockaddr_in sa;
                        
                        // Need to resolve? For clients we assume empty host means not OK!
                        ETDCSYSCALL(etdc::resolve_host<etdc::EmptyMeansInvalid>(clnt.clntHost, SOCK_STREAM, IPPROTO_IP, sa),
                                    "Failed to resolve/udt '" << clnt.clntHost << "'");

                        // Get the port info
                        // See "etdc_resolve.h" for FFS glibc shit why we
                        // have to fucking manually wrap htons!!
                        sa.sin_port = etdc::htons_( clnt.clntPort );

                        // Set socket options
                        etdc::setsockopt(pSok->__m_fd, clnt.udtBufSize, clnt.udtMSS);

                        // Make sure sokkit is in correct blocking mode
                        pSok->setblocking(pSok->__m_fd, etdc::untag(clnt.blocking));

                        // Connect
                        ETDCSYSCALL(UDT::connect(pSok->__m_fd, reinterpret_cast<struct sockaddr const*>(&sa), sl)!=UDT::ERROR,
                                    "connecting to udt[" << sa << "] - " << UDT::getlasterror().getErrorMessage());
                        // Not much else to do ...
                    }},
            {"udt6", [](etdc_fdptr pSok, detail::client_settings const& clnt) {
                        // connect to ipport
                        int                 sl( sizeof(struct sockaddr_in6) );
                        struct sockaddr_in6 sa;
                        
                        // Need to resolve? For clients we assume empty host means not OK!
                        ETDCSYSCALL(etdc::resolve_host<etdc::EmptyMeansInvalid>(clnt.clntHost, SOCK_STREAM, IPPROTO_IPV6, sa),
                                    "Failed to resolve/udt6 '" << clnt.clntHost << "'");

                        // Get the port info
                        // See "etdc_resolve.h" for FFS glibc shit why we
                        // have to fucking manually wrap htons!!
                        sa.sin6_port = etdc::htons_( clnt.clntPort );

                        // Set socket options
                        // Note: we cannot currently set the IPv6 only option through the UDT library
                        etdc::setsockopt(pSok->__m_fd, clnt.udtBufSize, clnt.udtMSS/*, clnt.ipv6_only*/);

                        // Make sure sokkit is in correct blocking mode
                        pSok->setblocking(pSok->__m_fd, etdc::untag(clnt.blocking));

                        // Connect
                        ETDCSYSCALL(UDT::connect(pSok->__m_fd, reinterpret_cast<struct sockaddr const*>(&sa), sl)!=UDT::ERROR,
                                    "connecting to udt6[" << sa << "] - " << UDT::getlasterror().getErrorMessage());
                        // Not much else to do ...
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

////////////////////////////////////////////////////////////////////////////////////////////////////
//
//    Canned sequence to create a server for a particular protocol with 
//    overridable compiled in default settings.
//    If it finishes without crashing or throwing exceptions, you may
//    call the "accept(...)" function to extract incoming connections
//
////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename T, typename... Ts>
etdc::etdc_fdptr mk_server(T const& proto, Ts... ts) {
    // Create socket and server defaults for the indicated protocol
    auto pSok         = mk_socket(proto);
    auto srvDefaults  = etdc::detail::server_defaults.find(proto)->second();
    // Update the defaults with what the user may have given us
    etdc::detail::update_srv(srvDefaults, std::forward<Ts>(ts)...);

    // And now transform the server details + sokkit into a real servert
    etdc::detail::server_map.find(proto)->second(pSok, srvDefaults);
    return pSok;
}

// Overload for if the user constructed his own serverdefaults
template <typename T>
etdc::etdc_fdptr mk_server(T const& proto, etdc::detail::server_settings const& srvSettings) {
    auto pSok         = mk_socket(proto);
    // And now transform the server settings + sokkit into a real servert
    etdc::detail::server_map.find(proto)->second(pSok, srvSettings);
    return pSok;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
//    Canned sequence to create a client connection to a server with
//    overrideable compiled in default settings
//    If it finishes without crashing or throwing exceptions, the 
//    connection should have been succesfully made.
//
////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename T, typename... Ts>
etdc::etdc_fdptr mk_client(T const& proto, Ts... ts) {
    // Create socket and server defaults for the indicated protocol
    auto pSok         = mk_socket(proto);
    auto clntDefaults = etdc::detail::client_defaults.find(proto)->second();

    // Update the defaults with what the user may have given us
    etdc::detail::update_clnt(clntDefaults, std::forward<Ts>(ts)...);

    // And now transform the client details + sokkit into a real client
    etdc::detail::client_map.find(proto)->second(pSok, clntDefaults);
    return pSok;
}

// Overload for if the user constructed his own clientdefaults
template <typename T>
etdc::etdc_fdptr mk_client(T const& proto, etdc::detail::client_settings const& clntSettings) {
    auto pSok         = mk_socket(proto);
    // And now transform the client settings + sokkit into a real client
    etdc::detail::client_map.find(proto)->second(pSok, clntSettings);
    return pSok;
}



// Define global ostream operator for struct sockaddr_in, dat's handy
template <class CharT, class Traits>
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, struct sockaddr_in const& sa) {
    char           buf[ INET_ADDRSTRLEN ];

    ETDCASSERTX(sa.sin_family==AF_INET); // otherwise we don't know what!
    ETDCSYSCALL(::inet_ntop(sa.sin_family, reinterpret_cast<const void*>(&sa.sin_addr.s_addr), buf, socklen_t(sizeof(buf))),
                "::inet_ntop() fails because of " << etdc::strerror(errno));
    return os << buf << ":" << etdc::ntohs_(sa.sin_port);
}

template <class CharT, class Traits>
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, struct sockaddr_in6 const& sa) {
    char           buf[ INET6_ADDRSTRLEN ];

    ETDCASSERTX(sa.sin6_family==AF_INET6); // otherwise we don't know what!
    ETDCSYSCALL(::inet_ntop(sa.sin6_family, reinterpret_cast<const void*>(&sa.sin6_addr), buf, socklen_t(sizeof(buf))),
                "::inet_ntop6() fails because of " << etdc::strerror(errno));
    return os << buf << ":" << etdc::ntohs_(sa.sin6_port);
}

#endif
