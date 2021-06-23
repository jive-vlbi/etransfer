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
#include <etdc_ctrlc.h>
#include <construct.h>
#include <reentrant.h>
#include <tagged.h>
#include <udt.h>

// C++
#include <map>
#include <regex>
#include <tuple>
#include <chrono>
#include <memory>
#include <thread>
#include <string>
#include <iostream>
#include <functional>

// Plain-old-C
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <net/if.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
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
    // Tags to connect to built-in types which allow for 
    // flexible type-based updating of struct fields - i.e. can tell
    // different "int" properties apart by their tag.
    namespace tags {
        struct mss_tag        {};
        struct port_tag       {};
        struct max_bw_tag     {};
        struct backlog_tag    {};
        struct blocking_tag   {};
        struct numretry_tag   {};
        struct retrydelay_tag {};
    }

    using mss_type        = etdc::tagged<int, tags::mss_tag>; // only >=64 and <= 64kB are allowed (and enforced)
    using port_type       = etdc::tagged<unsigned short, tags::port_tag>;
    using max_bw_type     = etdc::tagged<int64_t, tags::max_bw_tag>;
    using backlog_type    = etdc::tagged<int, tags::backlog_tag>;
    using blocking_type   = etdc::tagged<bool, tags::blocking_tag>;
    using numretry_type   = etdc::tagged<unsigned int, tags::numretry_tag>;
    using retrydelay_type = etdc::tagged<std::chrono::duration<float>, tags::retrydelay_tag>;
    static constexpr port_type any_port = port_type{ (unsigned short)0 };

    // ipport_type:   <host> : <port>
    // sockname_type: <type> / <host> : <port> / mss = <mss>
    using ipport_type   = std::tuple<host_type, port_type>;
    using sockname_type = std::tuple<protocol_type, host_type, port_type, mss_type, max_bw_type>;

    template <typename CharT, typename... Traits>
    std::basic_string<CharT, Traits...> bracket(std::basic_string<CharT, Traits...> const& s) {
        using stype = std::basic_string<CharT, Traits...>;
        // If the host name contains ":", "%" or "/" it means we may be looking at IPv6 hexformat
        // and then we bracket the host name to '[ .... ]'
        if( s.empty() )
            return s;
        // From here on we know that s is not empty
        auto const colon = s.find(':'), slash = s.find('/'), percent = s.find('%');
        // If we suspect s is an IPv6 literal but is already bracketed then don't do that again
        if( (colon==stype::npos && slash==stype::npos && percent==stype::npos) || s[0]=='[' )
            return s;
        return stype("[") + s + "]";
    }

    // the host name may be surrounded by '[' ... ']' for a literal
    // "coloned hex" IPv6 address
    template <typename CharT, typename... Traits>
    std::basic_string<CharT, Traits...> unbracket(std::basic_string<CharT, Traits...> const& h) {
        static const std::regex rxBracket("\\[([:0-9a-fA-F]+(/[0-9]{1,3})?(%[a-zA-Z0-9]+)?)\\]",
                                          std::regex_constants::ECMAScript | std::regex_constants::icase);
        return std::regex_replace(h, rxBracket, std::string("$1"));
    }

    template <class CharT, class Traits>
    std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, ipport_type const& ipport) {
        return os << "<" << bracket(std::get<0>(ipport)) << ":" << std::get<1>(ipport) << ">";
    }

    // Output to std::basic_ostream always outputs the current version
    template <class CharT, class Traits>
    std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, sockname_type const& sn) {
        os << "<" << std::get<0>(sn) << "/" << bracket(std::get<1>(sn)) << ":" << std::get<2>(sn);
        // Only show udt options when applicable
        if( untag(std::get<0>(sn)).find("udt")!=std::string::npos )
            os << "/mss=" << std::get<3>(sn) << ",max-bw=" << std::get<4>(sn);
        return os << ">";
    }

    // protocol version dependent sockname2string 
    std::string sockname2str_v0( sockname_type const& sn );
    std::string sockname2str_v1( sockname_type const& sn );

    //
    // Update values in a sockname type.
    //

    // base case: nothing more to update
    void update_sockname(sockname_type& );

    template <typename T, typename... Rest>
    void update_sockname(sockname_type& sn, T const& t, Rest&&... rest) {
        std::get<etdc::index_of<T, sockname_type>::value>(sn) = t;
        update_sockname(sn, std::forward<Rest>(rest)...);
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
                                                     &etdc_fd::getsockname, &etdc_fd::getpeername, &etdc_fd::setblocking,
                                                     &etdc_fd::lseek );

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


    namespace detail {
        std::string normalize_path(std::string const&);
        std::string dirname(std::string const&);
        std::string basename(std::string const&);

        // Introduce the template which whill recursively create directories if necessary
        template <typename... Args>
        int open_file(std::string const& path, int mode, Args&&...);

        // Policies to handle the result of "open_file(...)"

        // Standard issue of opening a file is: it should never fail with -1
        struct FailureIsNotAnOption {
            template <typename... Args>
            int operator()(std::string const& path, Args&&... args) const {
                int __m_fd;
                ETDCSYSCALL( (__m_fd=detail::open_file(path, std::forward<Args>(args)...))!=-1,
                             "failed to open/create '" << path << "' - " << etdc::strerror(errno) );
                return __m_fd;
            }
        };
        // This policy throws itself in case of EEXISTS.
        // If the open fails with EEXISTS than it means the New file write was invoked
        // and that error case should be singled out - see
        //    https://github.com/jive-vlbi/etransfer/issues/7
        struct ThrowOnExistThatShouldNotExist {
            template <typename... Args>
            int operator()(std::string const& path, Args&&... args) const {
                int __m_fd = detail::open_file(path, std::forward<Args>(args)...);

                // Did we encounter our special condition?
                if( __m_fd==-1 && errno==EEXIST )
                    throw ThrowOnExistThatShouldNotExist();

                // Otherwise verify that the open did acutally succeed
                ETDCSYSCALL( __m_fd!=-1, "failed to open/create '" << path << "' - " << etdc::strerror(errno) );
                return __m_fd;
            }
        };
    }

    template <typename OpenFilePolicy = detail::FailureIsNotAnOption>
    struct etdc_file:
        public etdc_fd
    {
        etdc_file()    = delete;

        // Take extra arguments that we can forward to ::open(2)
        template <typename... Args>
        explicit etdc_file(std::string const& path, Args&&... args) {
            static OpenFilePolicy openFilePolicy{};
            __m_fd = openFilePolicy(path, std::forward<Args>(args)...);
            setup_basic_fns();
        }

        private:
            ////////////////////////////////////////////////////////////////
            //   I/O to a regular file
            ////////////////////////////////////////////////////////////////
            void setup_basic_fns( void ) {
                // Update basic read/write/close functions
                // and on files seek() makes sense!
                etdc::update_fd(*this, read_fn(&::read), write_fn(&::write), close_fn(&::close),
                                       setblocking_fn(&setfdblockingmode),
                                       // we wrap the ::lseek() inna error check'n lambda dat does error check'n
                                       lseek_fn([](int fd, off_t offset, int whence) { 
                                           off_t  rv;
                                           ETDCASSERT((rv=::lseek(fd, offset, whence))!=(off_t)-1, "lseek fails - " << etdc::strerror(errno));
                                           return rv;
                                       })
                );
            }
    };

    namespace detail {
        constexpr int64_t ipow(int64_t base, int exp, int64_t result = 1) {
              return exp < 1 ? result : ipow(base*base, exp/2, (exp % 2) ? result*base : result);
        }
    }
    // the pattern for /dev/zero:<size>[unit]
    // unit can be empty              [base 1]
    //             kB,  MB,  GB,  TB  [base 1024]
    //             kiB, MiB, GiB, TiB [base 1000]
    // numbers below the regex identify submatch indices
    static const std::regex rxDevZero("^/dev/zero:([0-9]+)(([kMGT])(i?)B)?$");
    //                                            1       23       4

    // the fake file for speed testing
    // can be used for reading from ("/dev/zero:size") or writing to ("/dev/null")
    struct devzeronull:
        public etdc_fd {

            // no default objects
            devzeronull() = delete;

            // only acceptable paths: /dev/zero:<size> or /dev/null
            // we only keep "mode" for testing RDONLY/WRONLY in the read()
            // resp. write() function.
            // This 'file descriptor' will always have data available so
            // blocking/non-blocking is completely ignored
            template <typename... Args>
            devzeronull(std::string const& path, int omode, Args...): __m_closed(false), __m_mode(omode), __m_fSize(0), __m_fPointer(0) {
                // base and power lookups
                static const std::map<std::string, int> exponents{ {"", 0}, {"k", 1}, {"M", 2}, {"G", 3}, {"T", 4} };
                std::smatch             fields;
                const bool              isDevZero( std::regex_match(path, fields, rxDevZero) );
                ETDCASSERT(path=="/dev/null" || isDevZero,
                           std::string("Invalid path '") + path + "' [expect /dev/null or /dev/zero:<size>]");

                // if isDevZero we must parse out the file size
                if( isDevZero )
                    __m_fSize = std::stoull(fields[1].str()) * /* size */
                                (fields[2].str().empty() ?     /* any unit following? */
                                  1 : /* nope */
                                  detail::ipow( (fields[4].str().empty() ? 1024 : 1000), /* yes, base**exp */
                                                etdc::get(exponents, fields[3].str(), 1) /*exponents[ fields[3].str() ]*/ /*exp*/)  );
                setup_basic_fns();
            }
        private:
            void setup_basic_fns( void );

            // we need to do our own bookkeeping: fake file pointer and size
            bool         __m_closed;
            const int    __m_mode;
            std::size_t  __m_fSize;
            std::size_t  __m_fPointer;
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
etdc::sockname_type mk_sockname(T const& proto, U const& host, etdc::port_type port = etdc::any_port,
                                etdc::mss_type mss = etdc::mss_type{1500}, etdc::max_bw_type bw = etdc::max_bw_type{-1}) {
    return etdc::sockname_type(proto, host, port, mss, bw);
}

template <typename T, typename... Args>
etdc::etdc_fdptr mk_fd(Args&&... args) {
    return etdc::etdc_fdptr( std::make_shared<T>(std::forward<Args>(args)...) );
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
    try {
        return port( std::stoi(s) );
    }
    catch( std::exception const& e ) {
        throw std::runtime_error(std::string("Failed to convert port '") + etdc::repr(s) + "' - " + e.what());
    }
    catch( ... ) {
        throw std::runtime_error(std::string("Failed to convert port '") + etdc::repr(s) + "' - Unknown exception");
    }
}

///////////////////////////////////////////////////////////////////////////
//
//   Only accept arguments that are sensible to convert to a maximum 
//   segment size. IETF says "not < 64" and "not > 9000" for IP over 
//   Ethernet. But OTOH max UDP datagramsize is 64kB
//   Let's give the user the possibility to experiment w/ > 9000
//
///////////////////////////////////////////////////////////////////////////
template <typename T>
typename std::enable_if<etdc::is_integer_number_type<T>::value, etdc::mss_type>::type mss(T const& p) {
    // Only accept numbers that are actual valid mss settings
    ETDCASSERT(p>=64 && p<=(64*1024*1024), "MSS " << p << " out of range");
    return static_cast<etdc::mss_type>(p);
}

// For everything else we attempt string => number [so we can also accept wstring and god knows what
template <typename T>
typename std::enable_if<!etdc::is_integer_number_type<T>::value, etdc::mss_type>::type mss(T const& s) {
    try {
        return mss( std::stoi(s) );
    }
    catch( std::exception const& e ) {
        throw std::runtime_error(std::string("Failed to convert MSS '") + etdc::repr(s) + "' - " + e.what());
    }
    catch( ... ) {
        throw std::runtime_error(std::string("Failed to convert MSS '") + etdc::repr(s) + "' - Unknown exception");
    }
}

///////////////////////////////////////////////////////////////////////////
//
// Maximum bandwidts allowed are -1 or positive number
//
///////////////////////////////////////////////////////////////////////////
template <typename T>
typename std::enable_if<etdc::is_integer_number_type<T>::value, etdc::max_bw_type>::type max_bw(T const& bw) {
    // Only accept numbers that are actual valid mss settings
    ETDCASSERT(bw==-1 || bw>0, "MaxBW " << bw << " invalid - either -1 or >0 is allowed");
    return static_cast<etdc::max_bw_type>(bw);
}

// Parse string to bandwidth in bytes per second
// the pattern is <rate>[unit]
// rate
//             integer
// unit can be 
//        empty                           [base 1]
//        bytes
//             kBps,  MBps,  GBps,  TBps  [base 1000]
//             kiBps, MiBps, GiBps, TiBps [base 1024]
//        bits
//             kbps,  Mbps,  Gbps,  Tbps  [base 1000]
//             kibps, Mibps, Gibps, Tibps [base 1024]
etdc::max_bw_type max_bw(std::string const& bandwidthstr);

// For everything else we attempt conversion to int64_t 
template <typename T>
typename std::enable_if<!etdc::is_integer_number_type<T>::value, etdc::max_bw_type>::type max_bw(T const& v) {
    return max_bw( static_cast<int64_t>(v) );
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

template <typename... Ts>
etdc::mss_type get_mss(Ts... t) {
    return std::get< etdc::index_of<etdc::mss_type, Ts...>::value >( t... );
}

template <typename... Ts>
etdc::max_bw_type get_max_bw(Ts... t) {
    return std::get< etdc::index_of<etdc::max_bw_type, Ts...>::value >( t... );
}


namespace etdc {
    // For IPv6 we must be able to extract a scope id to fill in the
    // sin6_scope_id
    const std::regex    rxScope("%([a-z0-9\\.]+)", std::regex::ECMAScript | std::regex::icase);

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
            etdc::so_sndbuf  sndBufSize {};
            etdc::udt_rcvbuf udtBufSize {};
            etdc::udt_sndbuf udtSndBufSize {};
            etdc::udp_rcvbuf udpBufSize {};
            etdc::udp_sndbuf udpSndBufSize {};
            etdc::ipv6_only  ipv6_only  {};
            etdc::udt_linger udtLinger  {};
            etdc::udt_max_bw udtMaxBW   {};
        };
        const etdc::construct<server_settings>  update_srv( &server_settings::blocking,
                                                            &server_settings::backLog,
                                                            &server_settings::srvHost,
                                                            &server_settings::srvPort,
                                                            &server_settings::rcvBufSize,
                                                            &server_settings::sndBufSize,
                                                            &server_settings::udtBufSize,
                                                            &server_settings::udtSndBufSize,
                                                            &server_settings::udpBufSize,
                                                            &server_settings::udpSndBufSize,
                                                            &server_settings::udtMSS,
                                                            &server_settings::ipv6_only,
                                                            &server_settings::udtLinger,
                                                            &server_settings::udtMaxBW );

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
                                                etdc::udt_sndbuf{defaultUDTBufSize},
                                                etdc::udp_sndbuf{32*1024*1024},
                                                etdc::udp_rcvbuf{32*1024*1024},
                                                any_port, etdc::udt_linger{{0,0}},
                                                etdc::udt_mss{1500},
                                                etdc::udt_max_bw{-1} );
                         }},
            {"udt6", []() { return update_srv.mk(backlog_type{4},
                                                blocking_type{true},
                                                etdc::udt_rcvbuf{defaultUDTBufSize},
                                                etdc::udt_sndbuf{defaultUDTBufSize},
                                                etdc::udp_sndbuf{32*1024*1024},
                                                etdc::udp_rcvbuf{32*1024*1024},
                                                any_port, etdc::udt_linger{{0,0}},
                                                etdc::udt_mss{1500},
                                                etdc::udt_max_bw{-1} );
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
                        ETDCSYSCALL(etdc::resolve_host<etdc::EmptyMeansAny>(srv.srvHost, SOCK_STREAM, IPPROTO_TCP, sa),
                                    "Failed to resolve/tcp '" << srv.srvHost << "'");

                        // Set socket options 
                        etdc::setsockopt(pSok->__m_fd, etdc::so_reuseaddr{true});

                        if( srv.rcvBufSize )
                            etdc::setsockopt(pSok->__m_fd, srv.rcvBufSize);
                        if( srv.sndBufSize )
                            etdc::setsockopt(pSok->__m_fd, srv.sndBufSize);

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
                        ETDCSYSCALL(etdc::resolve_host<etdc::EmptyMeansAny>(srv.srvHost, SOCK_STREAM, IPPROTO_TCP, sa),
                                    "Failed to resolve/tcp6 '" << srv.srvHost << "'");
                        // Support scope id
                        std::smatch scope;
                        if( std::regex_search(srv.srvHost, scope, rxScope) )
                            sa.sin6_scope_id = ::if_nametoindex(scope[1].str().c_str());
                        else
                            sa.sin6_scope_id = 0;

                        // Set socket options 
                        etdc::setsockopt(pSok->__m_fd, etdc::so_reuseaddr{true}, srv.ipv6_only);

                        // Override rcvbufsize only if actually set
                        if( srv.rcvBufSize )
                            etdc::setsockopt(pSok->__m_fd, srv.rcvBufSize);
                        if( srv.sndBufSize )
                            etdc::setsockopt(pSok->__m_fd, srv.sndBufSize);

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
                            socklen_t            ipl( sizeof(struct sockaddr_in6) );
                            struct sockaddr_in6  ip;
                            int                  fd = ::accept(f, reinterpret_cast<struct sockaddr*>(&ip), &ipl);

                            // fd<0 is not an error if blocking + errno == EAGAIN || EWOULDBLOCK
                            ETDCSYSCALL(fd>0 || (!srv.blocking && fd==-1 && (errno==EAGAIN || errno==EWOULDBLOCK)),
                                        "failed to accept on tcp6[" << sa << "] - " << etdc::strerror(errno));
                            return (fd==-1) ? std::shared_ptr<etdc_fd>() : std::make_shared<etdc::etdc_tcp6>(fd);
                        };
                    }},
            ////////// UDT server  (IPv4)
            {"udt", [](etdc_fdptr pSok, detail::server_settings const& srv) -> void {
                        // Bind to ipport
                        socklen_t          sl( sizeof(struct sockaddr_in) );
                        struct sockaddr_in sa;
                        
                        // Set a couple of socket options
                        // NOTE: For really large buffers we must set the UDT_FC (flow control, window size)
                        //       to a larger value (25600 in libudt), it is
                        //       measured in MSS packets so we set this
                        //       option from the server's configured values
                        const auto fc = (etdc::untag(srv.udtBufSize)/(etdc::untag(srv.udtMSS)-28))+256;
                        etdc::setsockopt(pSok->__m_fd, etdc::udt_reuseaddr{true}, etdc::udt_fc{fc}, 
                                         srv.udtBufSize, srv.udtSndBufSize, srv.udtMSS, srv.udtLinger,
                                         srv.udtMaxBW);

                        if( srv.udpBufSize )
                            etdc::setsockopt(pSok->__m_fd, srv.udpBufSize);
                        if( srv.udpSndBufSize )
                            etdc::setsockopt(pSok->__m_fd, srv.udpSndBufSize);

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
                            socklen_t           ipl( sizeof(struct sockaddr_in) );
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
                        socklen_t           sl( sizeof(struct sockaddr_in6) );
                        struct sockaddr_in6 sa;
                        
                        // Set a couple of socket options
                        // Note: we cannot set the IPv6 only option through the UDT library at the moment
                        // NOTE: For really large buffers we must set the UDT_FC (flow control, window size)
                        //       to a larger value (25600 in libudt), it is
                        //       measured in MSS packets so we set this
                        //       option from the server's configured values
                        const auto fc = (etdc::untag(srv.udtBufSize)/(etdc::untag(srv.udtMSS)-28))+256;
                        etdc::setsockopt(pSok->__m_fd, etdc::udt_reuseaddr{true}, etdc::udt_fc{fc}, 
                                         srv.udtBufSize, srv.udtSndBufSize, srv.udtMSS, srv.udtLinger,
                                         srv.udtMaxBW);
                        //etdc::setsockopt(pSok->__m_fd, etdc::udt_reuseaddr{true}, srv.udtBufSize, srv.udtSndBufSize, srv.udtMSS, srv.udtLinger);

                        if( srv.udpBufSize )
                            etdc::setsockopt(pSok->__m_fd, srv.udpBufSize);
                        if( srv.udpSndBufSize )
                            etdc::setsockopt(pSok->__m_fd, srv.udpSndBufSize);

                        // Need to resolve? For here we assume empty host means any
                        ETDCSYSCALL(etdc::resolve_host<etdc::EmptyMeansAny>(srv.srvHost, SOCK_STREAM, IPPROTO_TCP, sa),
                                    "Failed to resolve/udt '" << srv.srvHost << "'");
                        // Support scope id
                        std::smatch scope;
                        if( std::regex_search(srv.srvHost, scope, rxScope) )
                            sa.sin6_scope_id = ::if_nametoindex(scope[1].str().c_str());
                        else
                            sa.sin6_scope_id = 0;

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
                            socklen_t            ipl( sizeof(struct sockaddr_in6) );
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

        // holder for function to test if the call is to be cancelled
        using cancelfn_type = std::function<bool(void)>;
        static bool noCancelFn( void ) {
            return false;
        }

        struct client_settings {
            blocking_type    blocking   {};
            host_type        clntHost   {}; 
            port_type        clntPort   {};
            numretry_type    nRetry     {};
            retrydelay_type  retryDelay {};
            etdc::udt_mss    udtMSS     {};
            etdc::so_sndbuf  sndBufSize {};
            etdc::so_rcvbuf  rcvBufSize {};
            etdc::udt_sndbuf udtBufSize {};
            etdc::udt_rcvbuf udtRcvBufSize {};
            etdc::udp_sndbuf udpBufSize {};
            etdc::udp_rcvbuf udpRcvBufSize {};
            etdc::ipv6_only  ipv6_only  {};
            etdc::udt_linger udtLinger  {};
            etdc::udt_max_bw udtMaxBW   {};
            cancelfn_type    cancel_fn  {};
        };
        const etdc::construct<client_settings>  update_clnt( &client_settings::blocking,
                                                             &client_settings::clntPort,
                                                             &client_settings::clntHost,
                                                             &client_settings::nRetry,
                                                             &client_settings::retryDelay,
                                                             &client_settings::sndBufSize,
                                                             &client_settings::rcvBufSize,
                                                             &client_settings::udtMSS,
                                                             &client_settings::udtBufSize,
                                                             &client_settings::udtRcvBufSize,
                                                             &client_settings::udpBufSize,
                                                             &client_settings::udpRcvBufSize,
                                                             &client_settings::ipv6_only,
                                                             &client_settings::udtLinger,
                                                             &client_settings::udtMaxBW,
                                                             &client_settings::cancel_fn );

        using client_defaults_map = std::map<std::string, std::function<client_settings(void)>>;


        // client defaults per protocol type
        static const client_defaults_map client_defaults = {
            // tcp doesn't need to do reconnect by default
            {"tcp", []() { return update_clnt.mk(blocking_type{true},
                                                 numretry_type{0}, retrydelay_type{0},
                                                 any_port, cancelfn_type{noCancelFn} );
                         }},
            {"tcp6", []() { return update_clnt.mk(blocking_type{true}, etdc::ipv6_only{true},
                                                 numretry_type{0}, retrydelay_type{0},
                                                 any_port, cancelfn_type{noCancelFn} );
                         }},
            // for udt a non-zero default retry might not be a bad idea
            {"udt", []() { return update_clnt.mk(etdc::udt_mss{1500},
                                                 any_port, etdc::udt_linger{{0, 0}},
                                                 numretry_type{2}, retrydelay_type{5},
                                                 etdc::udt_sndbuf{defaultUDTBufSize},
                                                 etdc::udt_rcvbuf{defaultUDTBufSize},
                                                 etdc::udp_sndbuf{32*1024*1024},
                                                 etdc::udp_rcvbuf{32*1024*1024},
                                                 blocking_type{true},
                                                 etdc::udt_max_bw{-1},
                                                 cancelfn_type{noCancelFn} );
                         }},
            {"udt6", []() { return update_clnt.mk(etdc::udt_mss{1500},
                                                 // UDT does not allow direct access to the real socket so we can't really
                                                 // set an option at the IPPROTO_IPV6 level.
                                                 any_port, etdc::udt_linger{{0,0}},
                                                 numretry_type{2}, retrydelay_type{5},
                                                 etdc::udt_sndbuf{defaultUDTBufSize},
                                                 etdc::udt_rcvbuf{defaultUDTBufSize},
                                                 etdc::udp_sndbuf{32*1024*1024},
                                                 etdc::udp_rcvbuf{32*1024*1024},
                                                 blocking_type{true},
                                                 etdc::udt_max_bw{-1},
                                                 cancelfn_type{noCancelFn} );
                         }}
        };

        // Default actions to turn a socket into a client socket
        static const std::map<std::string, std::function<void(etdc_fdptr, detail::client_settings const&)>> client_map = {
            {"tcp", [](etdc_fdptr pSok, detail::client_settings const& clnt) {
                        // connect to ipport
                        socklen_t          sl( sizeof(struct sockaddr_in) );
                        struct sockaddr_in sa;
                        
                        // Need to resolve? For clients we assume empty host means not OK!
                        ETDCSYSCALL(etdc::resolve_host<etdc::EmptyMeansInvalid>(clnt.clntHost, SOCK_STREAM, IPPROTO_TCP, sa),
                                    "Failed to resolve/tcp '" << clnt.clntHost << "'");

                        // Get the port info
                        // See "etdc_resolve.h" for FFS glibc shit why we
                        // have to fucking manually wrap htons!!
                        sa.sin_port = etdc::htons_( clnt.clntPort );

                        // Set socket options
                        if( clnt.sndBufSize )
                            etdc::setsockopt(pSok->__m_fd, clnt.sndBufSize);
                        if( clnt.rcvBufSize )
                            etdc::setsockopt(pSok->__m_fd, clnt.rcvBufSize);

                        // Make sure sokkit is in correct blocking mode
                        // XXX FIXME TODO The requested blocking mode should
                        //                be set *after* a succesful connect()
                        //                connect should be done in a
                        //                predefined mode, to make the
                        //                connect() call work predictably
                        pSok->setblocking(pSok->__m_fd, etdc::untag(clnt.blocking));

                        pthread_t  tid = ::pthread_self();
                        etdc::ScopedAction intrpt( etdc::ControlCAction([=](int s) {
                                ::close( pSok->__m_fd );
                                // need to kick this thread to make it
                                // realize the filedescriptor's gone
                                ::pthread_kill(tid, s);
                            }) );

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
                        ETDCSYSCALL(etdc::resolve_host<etdc::EmptyMeansInvalid>(clnt.clntHost, SOCK_STREAM, IPPROTO_TCP, sa),
                                    "Failed to resolve/tcp6 '" << clnt.clntHost << "'");
                        // Support scope id
                        std::smatch scope;
                        if( std::regex_search(clnt.clntHost, scope, rxScope) )
                            sa.sin6_scope_id = ::if_nametoindex(scope[1].str().c_str());
                        else
                            sa.sin6_scope_id = 0;

                        // Get the port info
                        // See "etdc_resolve.h" for FFS glibc shit why we
                        // have to fucking manually wrap htons!!
                        sa.sin6_port = etdc::htons_( clnt.clntPort );

                        // Set socket options
                        etdc::setsockopt(pSok->__m_fd, clnt.ipv6_only);

                        if( clnt.sndBufSize )
                            etdc::setsockopt(pSok->__m_fd, clnt.sndBufSize);
                        if( clnt.rcvBufSize )
                            etdc::setsockopt(pSok->__m_fd, clnt.rcvBufSize);

                        // Make sure sokkit is in correct blocking mode
                        // XXX FIXME TODO The requested blocking mode should
                        //                be set *after* a succesful connect()
                        //                connect should be done in a
                        //                predefined mode, to make the
                        //                connect() call work predictably
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
                        ETDCSYSCALL(etdc::resolve_host<etdc::EmptyMeansInvalid>(clnt.clntHost, SOCK_STREAM, IPPROTO_TCP, sa),
                                    "Failed to resolve/udt '" << clnt.clntHost << "'");

                        // Get the port info
                        // See "etdc_resolve.h" for FFS glibc shit why we
                        // have to fucking manually wrap htons!!
                        sa.sin_port = etdc::htons_( clnt.clntPort );

                        // Set socket options
                        // NOTE: For really large buffers we must set the UDT_FC (flow control, window size)
                        //       to a larger value (25600 in libudt), it is
                        //       measured in MSS packets so we set this
                        //       option from the server's configured values
                        const auto fc = (etdc::untag(clnt.udtRcvBufSize)/(etdc::untag(clnt.udtMSS)-28))+256;
                        etdc::setsockopt(pSok->__m_fd, etdc::udt_reuseaddr{true}, etdc::udt_fc{fc}, 
                                         clnt.udtBufSize, clnt.udtRcvBufSize, clnt.udtMSS, clnt.udtLinger,
                                         clnt.udtMaxBW);

                        if( clnt.udpBufSize )
                            etdc::setsockopt(pSok->__m_fd, clnt.udpBufSize);
                        if( clnt.udpRcvBufSize )
                            etdc::setsockopt(pSok->__m_fd, clnt.udpRcvBufSize);

                        // Make sure sokkit is in correct blocking mode
                        // XXX FIXME TODO The requested blocking mode should
                        //                be set *after* a succesful connect()
                        //                connect should be done in a
                        //                predefined mode, to make the
                        //                connect() call work predictably
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
                        ETDCSYSCALL(etdc::resolve_host<etdc::EmptyMeansInvalid>(clnt.clntHost, SOCK_STREAM, IPPROTO_TCP, sa),
                                    "Failed to resolve/udt6 '" << clnt.clntHost << "'");
                        // Support scope id
                        std::smatch scope;
                        if( std::regex_search(clnt.clntHost, scope, rxScope) )
                            sa.sin6_scope_id = ::if_nametoindex(scope[1].str().c_str());
                        else
                            sa.sin6_scope_id = 0;

                        // Get the port info
                        // See "etdc_resolve.h" for FFS glibc shit why we
                        // have to fucking manually wrap htons!!
                        sa.sin6_port = etdc::htons_( clnt.clntPort );

                        // Set socket options
                        // Note: we cannot currently set the IPv6 only option through the UDT library
                        //       to a larger value (25600 in libudt), it is
                        //       measured in MSS packets so we set this
                        //       option from the server's configured values
                        const auto fc = (etdc::untag(clnt.udtRcvBufSize)/(etdc::untag(clnt.udtMSS)-28))+256;
                        etdc::setsockopt(pSok->__m_fd, etdc::udt_reuseaddr{true}, etdc::udt_fc{fc}, 
                                         clnt.udtBufSize, clnt.udtRcvBufSize, clnt.udtMSS, clnt.udtLinger,
                                         clnt.udtMaxBW);

                        if( clnt.udpBufSize )
                            etdc::setsockopt(pSok->__m_fd, clnt.udpBufSize);
                        if( clnt.udpRcvBufSize )
                            etdc::setsockopt(pSok->__m_fd, clnt.udpRcvBufSize);

                        // Make sure sokkit is in correct blocking mode
                        // XXX FIXME TODO The requested blocking mode should
                        //                be set *after* a succesful connect()
                        //                connect should be done in a
                        //                predefined mode, to make the
                        //                connect() call work predictably
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
//
// Overload for if the user constructed his own serverdefaults
template <typename T>
etdc::etdc_fdptr mk_server(T const& proto, etdc::detail::server_settings const& srvSettings) {
    auto pSok         = mk_socket(proto);
    // And now transform the server settings + sokkit into a real servert
    etdc::detail::server_map.find(proto)->second(pSok, srvSettings);
    return pSok;
}

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

////////////////////////////////////////////////////////////////////////////////////////////////////
//
//    Canned sequence to create a client connection to a server with
//    overrideable compiled in default settings
//    If it finishes without crashing or throwing exceptions, the 
//    connection should have been succesfully made.
//
////////////////////////////////////////////////////////////////////////////////////////////////////

// Overload for if the user constructed his own clientdefaults
// TODO XXX Should allow handling ^C in here
template <typename T>
etdc::etdc_fdptr mk_client(T const& proto, etdc::detail::client_settings const& clntSettings) {
    unsigned int       retry{ 0 };
    while( !clntSettings.cancel_fn()/*true*/ ) {
        std::exception_ptr eptr{ nullptr };
        try {
            auto pSok         = mk_socket(proto);

            ETDCDEBUG(4, "mk_client/attempt #" << retry+1  << "/" << untag(clntSettings.nRetry)+1 << " trying to connect to " <<
                         proto << ":" << clntSettings.clntHost << ":" << clntSettings.clntPort << std::endl);
            // And now transform the client settings + sokkit into a real client
            etdc::detail::client_map.find(proto)->second(pSok, clntSettings);

            return pSok;
        }
        catch( ... ) {
            // That failed! 
            eptr = std::current_exception();
        }
        // Only sleep if there will be a next attempt
        if( clntSettings.cancel_fn() )
            break;
        if( retry++ < untag(clntSettings.nRetry) ) {
            auto sleeptime = untag(clntSettings.retryDelay);
            ETDCDEBUG(4, "mk_client/sleeping for " << sleeptime.count() << "s trying to connect to " <<
                         proto << ":" << clntSettings.clntHost << ":" << clntSettings.clntPort << std::endl);
            std::this_thread::sleep_for( sleeptime );
        } else if( eptr )
            std::rethrow_exception(eptr);
        else
            break;
    }
    if( clntSettings.cancel_fn() )
        return etdc::etdc_fdptr{};
    ETDCASSERT((1+1)==3, "mk_client(" << proto << ", clnt=" << clntSettings.clntHost << ":" << clntSettings.clntPort << ")/Fails w/o exception?!");
}

template <typename T, typename... Ts>
etdc::etdc_fdptr mk_client(T const& proto, Ts... ts) {
    // Create socket and server defaults for the indicated protocol
    auto clntDefaults = etdc::detail::client_defaults.find(proto)->second();

    // Update the defaults with what the user may have given us
    etdc::detail::update_clnt(clntDefaults, std::forward<Ts>(ts)...);

    return mk_client(proto, clntDefaults);
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

namespace etdc { namespace detail {
        // Introduce the template which whill recursively create directories if necessary
        template <typename... Args>
        int open_file(std::string const& path, int mode, Args&&... args) {
            const std::string npath = normalize_path(path);

            ETDCDEBUG(5, "open_file/npath='" << npath << "'" << std::endl);
            // Now we can iterate over all the entries and create them if necessary
            if( (mode&O_CREAT)==O_CREAT ) {
                // we're expected to (attempt to) create the thing
                const std::string      dir( detail::dirname(npath) );
                // XXX NOTE:
                // std::string.find(...) has (as one of the overloads):
                //     .find(CharT ch, size_type pos)
                //
                //  and I was calling it as:
                //     .find(1, '/')
                //  Even with the warnings turned up to 11 and more
                //  not a single chirp from either clang (MacOS) or g++ (Loonix)
                //  But obviously it ain't gonna work that way!
                //
                //  Also tried compiling with -fsigned-char and calling as
                //      .find(1, char('/')) 
                //  to see if we could trigger a signed/unsigned warning
                //  (std::string::size_type is usually unsigned and 
                //  "char('/')" should be signed per -fsigned-char
                //  but also nothing ...
                std::string::size_type slash = dir.find('/', 1);
                ETDCDEBUG(5, "open_file/O_CREAT is set, dir='" << dir << "'" << std::endl);
                // iteratively start growing the path and attempt to create if not exist
                while( slash!=std::string::npos ) {
                    // Create the path - searchable for everyone, r,w,x for usr
                    const std::string path_so_far{ dir.substr(0, slash) };
                    ETDCDEBUG(5, "open_file/path_so_far='" << path_so_far << "'" << std::endl);
                    ETDCASSERT(::mkdir(path_so_far.c_str(), 0755)==0 || errno==EEXIST,
                               "Failed to create path '" << path_so_far << "' - " << etdc::strerror(errno) );
                    // And look for the next slash
                    slash = dir.find('/', slash+1);
                }
            }
            // Rite-o. Directories may have been created, now we can attempt to
            // actually open the file
            return ::open(npath.c_str(), mode, std::forward<Args>(args)...);
        }
        
    } //namespace detail 
} // namespace etdc
#endif
