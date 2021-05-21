// implementation of base- and derived classes for wrapping file descriptors
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
//
#include <etdc_fd.h>
#include <reentrant.h>
#include <etdc_assert.h>
#include <etdc_nullfn.h>

#include <ios>
#include <regex>
#include <stdexcept>
#include <functional>

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

// this one should live in the global namespace
etdc::max_bw_type max_bw(std::string const& bandwidthstr) {
    // Special case for "-1", every other string should pass the below code
    if( bandwidthstr=="-1" )
        return etdc::max_bw_type{ -1 };

    // base and power lookups
    static const std::map<std::string, int> exponents{ {"", 0}, {"k", 1}, {"M", 2}, {"G", 3}, {"T", 4} };
    static const std::map<std::string, int> bitsbytes{ {"b", 1}, {"B", 8} };
    // numbers below the regex identify submatch indices
    static const std::regex rxBandwidth("^([0-9]+)(([kMGT])(i?)([Bb])ps)?$");
    //                                    1       23       4   5

    std::smatch fields;

    ETDCASSERT(std::regex_match(bandwidthstr, fields, rxBandwidth),
               std::string("Invalid bandwidth string '") + bandwidthstr + "' [expect <number>{kMGT[i](Bb)ps]}");

    // do computation in bits per second; UDT lib expects bytes / second
    // so we do the conversion after that
    //int64_t const maxbw = static_cast<int64_t>(
    return max_bw( static_cast<int64_t>(
                        std::stoll(fields[1].str()) * /* rate */
                        etdc::get(bitsbytes, fields[5].str(), 8) * /* bytes vs bits; if field[5] == empty => no unit, use implicit bytes */
                        (fields[2].str().empty() ?     /* any unit following? */
                           1 : /* nope */
                           etdc::detail::ipow( (fields[4].str().empty() ? 1024 : 1000), /* yes, base**exp */
                                                etdc::get(exponents, fields[3].str(), 1) ))
                ) / 8 ); /* and convert to bytes / second*/
}

namespace etdc {
    // set file descriptor in blocking or non-blocking mode
    void setfdblockingmode(int fd, bool blocking) {
        int  fmode;

        ETDCASSERT(fd>=0, "fd=" << fd);
     
        fmode = ::fcntl(fd, F_GETFL);
        fmode = (blocking?(fmode&(~O_NONBLOCK)):(fmode|O_NONBLOCK));
        ETDCASSERT( ::fcntl(fd, F_SETFL, fmode)!=-1,
                     "fd=" << fd << ", blocking=" << std::boolalpha << blocking );
        fmode = ::fcntl(fd, F_GETFL);
        ETDCASSERT(blocking ? ((fmode&O_NONBLOCK)==0) : ((fmode&O_NONBLOCK)==O_NONBLOCK),
                   "Failed to set blocking=" << std::boolalpha << blocking << " on fd#" << fd);
        return;
    }


    // protocol version dependent sockname2string 
    std::string sockname2str_v0( sockname_type const& sn ) {
        std::ostringstream oss;
        oss << "<" << std::get<0>(sn) << "/" << bracket(std::get<1>(sn)) << ":" << std::get<2>(sn) << ">";
        return oss.str();
    }
    // protocol version 1 allows passing socket parameters
    std::string sockname2str_v1( sockname_type const& sn ) {
        std::ostringstream oss;
        oss << "<" << std::get<0>(sn) << "/" << bracket(std::get<1>(sn)) << ":" << std::get<2>(sn)
            // here is where the extra options are present
            << "/mss=" << std::get<3>(sn)
            << ",max-bw=" << std::get<4>(sn)
            << ">";
        return oss.str();
    }

    // The base case of update_sockname
    void update_sockname(sockname_type&) { }

    // For std::bind( ..., _1, ...) &cet
    using namespace std::placeholders;

    namespace detail {


        // The no-op function for not extracting MSS from the socket
        static int     no_mss_fn(int /*fd*/) { return 0; }
        static int64_t no_bw_fn(int /*fd*/ ) { return 0; }

        // Template for getsockname/getpeername - it's all the same error
        // checking and return value creation, only which bit of the address
        // to process is different
        template <int (*fptr)(int, struct sockaddr*, socklen_t*), int (*mss_fn)(int) = no_mss_fn, int64_t(*bw_fn)(int) = no_bw_fn>
        sockname_type ipv4_sockname(int fd, std::string const& p, std::string const& s) {
            socklen_t          len{ sizeof(struct sockaddr_in) };
            struct sockaddr_in saddr;

            ETDCASSERT( fptr(fd, reinterpret_cast<struct sockaddr*>(&saddr), &len)==0,
                        s << " for protocol=" << p << " fails - " << etdc::strerror(errno) );
            // transform the IPv4 address to string
            char    addr_s[ INET_ADDRSTRLEN+1 ];

            ETDCASSERT( ::inet_ntop(AF_INET, &saddr.sin_addr.s_addr, addr_s, len)!=nullptr,
                        "inet_ntop() fails - "<< etdc::strerror(errno) );

            auto  sn = mk_sockname(proto(p), host(addr_s), port(etdc::ntohs_(saddr.sin_port)));

            // Check if the MSS can be extracted
            const int mss_v{ mss_fn(fd) };
            if( mss_v )
                etdc::update_sockname(sn, etdc::mss_type{mss_v});

            const int64_t bw_v{ bw_fn(fd) };
            if( bw_v==0 )
                etdc::update_sockname(sn, etdc::max_bw_type{-1});
            else
                etdc::update_sockname(sn, max_bw(bw_v));
            return sn;
        }


        // Id. for IPv6 - we need different sockaddr + addrstrlen and address family
        template <int (*fptr)(int, struct sockaddr*, socklen_t*), int (*mss_fn)(int) = no_mss_fn, int64_t(*bw_fn)(int) = no_bw_fn>
        sockname_type ipv6_sockname(int fd, std::string const& p, std::string const& s) {
            socklen_t           len( sizeof(struct sockaddr_in6) );
            struct sockaddr_in6 saddr;

            ETDCASSERT( fptr(fd, reinterpret_cast<struct sockaddr*>(&saddr), &len)==0,
                        s << " for protocol=" << p << " fails - " << etdc::strerror(errno) );
            // transform the IPv6 address to string
            char    addr_s[ INET6_ADDRSTRLEN+1 ];

            ETDCASSERT( ::inet_ntop(AF_INET6, &saddr.sin6_addr, addr_s, len)!=nullptr,
                        "inet_ntop() fails - "<< etdc::strerror(errno) );

            // IPv6 'coloned-hex' format does square brackets around it, to
            // be able to separate it from the ":port" suffix
            auto  sn = mk_sockname(proto(p), host(std::string("[")+addr_s+"]"), port(etdc::ntohs_(saddr.sin6_port)));

            // Check if the MSS can be extracted
            const int mss_v{ mss_fn(fd) };
            if( mss_v )
                etdc::update_sockname(sn, etdc::mss_type{mss_v});

            const int64_t bw_v{ bw_fn(fd) };
            if( bw_v==0 )
                etdc::update_sockname(sn, etdc::max_bw_type{-1});
            else
                etdc::update_sockname(sn, max_bw(bw_v));
            return sn;
        }
    }


    ////////////////////////////////////////////////////////////////////////
    //           The basic struct - initialize them
    //           with etdc::nullfn such that if they
    //           accidentally get called the error
    //           tells you which one was called
    //           (much better than "std::exception - bad function call"
    //            with no further information)
    ////////////////////////////////////////////////////////////////////////
    etdc_fd::etdc_fd():
        __m_fd( -1 ),
        read( nullfn(decltype(read)) ),
        write( nullfn(decltype(write)) ),
        close( nullfn(decltype(close)) ),
        lseek( nullfn(decltype(lseek)) ), 
        accept( nullfn(decltype(accept)) ),
        getsockname( nullfn(typename decltype(getsockname)::type) ),
        getpeername( nullfn(typename decltype(getpeername)::type) )
    {}

    etdc_fd::~etdc_fd() {
        if( __m_fd!=-1 )
            this->close( __m_fd );
        __m_fd = -1;
    }

    ////////////////////////////////////////////////////////////////////////
    //                        TCP/IPv4 sockets
    ////////////////////////////////////////////////////////////////////////
    etdc_tcp::etdc_tcp() {
        ETDCSYSCALL( (__m_fd=::socket(PF_INET, SOCK_STREAM, etdc::getprotobyname("tcp").p_proto))!=-1,
                     "failed to create TCP socket - " << etdc::strerror(errno) );
        // Update basic read/write/close functions
        setup_basic_fns();
    }
    etdc_tcp::etdc_tcp(int fd) {
        ETDCASSERT(fd>=0, "constructing TCP file descriptor from invalid fd#" << fd);
        __m_fd = fd;
        // Update basic read/write/close functions
        setup_basic_fns();
    }

    void etdc_tcp::setup_basic_fns( void ) {
        // Update basic read/write/close functions
        etdc::update_fd(*this, read_fn(&::read), write_fn(&::write), close_fn(&::close),
                               getsockname_fn( [](int fd) {
                                    return detail::ipv4_sockname<::getsockname>(fd, "tcp", "getsockname"); } ),
                               getpeername_fn( [](int fd) {
                                    return detail::ipv4_sockname<::getpeername>(fd, "tcp", "getpeername"); } ),
                               setblocking_fn(&setfdblockingmode)
        );
    }

    etdc_tcp::~etdc_tcp() {}

    ////////////////////////////////////////////////////////////////////////
    //                        TCP/IPv6 sockets
    ////////////////////////////////////////////////////////////////////////
    etdc_tcp6::etdc_tcp6() {
        ETDCSYSCALL( (__m_fd=::socket(PF_INET6, SOCK_STREAM, etdc::getprotobyname("tcp").p_proto))!=-1,
                     "failed to create TCP6 socket - " << etdc::strerror(errno) );
        // Update basic read/write/close functions
        setup_basic_fns();
    }
    etdc_tcp6::etdc_tcp6(int fd) {
        ETDCASSERT(fd>=0, "constructing TCP6 file descriptor from invalid fd#" << fd);
        __m_fd = fd;
        // Update basic read/write/close functions
        setup_basic_fns();
    }

    void etdc_tcp6::setup_basic_fns( void ) {
        // Most of the functions we can share with IPv4, only some need
        // overriding for IPv6:
        // http://long.ccaba.upc.es/long/045Guidelines/eva/ipv6.html
        
        // Get all IPv4 versions
        this->etdc_tcp::setup_basic_fns();

        // And override the ones we need to
        etdc::update_fd(*this, getsockname_fn( [](int fd) {
                                    return detail::ipv6_sockname<::getsockname>(fd, "tcp6", "getsockname"); } ),
                               getpeername_fn( [](int fd) {
                                    return detail::ipv6_sockname<::getpeername>(fd, "tcp6", "getpeername"); } )
        );
    }

    etdc_tcp6::~etdc_tcp6() {}

    ////////////////////////////////////////////////////////////////////////
    //                        UDT sockets
    ////////////////////////////////////////////////////////////////////////
    namespace detail {
        // Provide a function that extracts the UDT::MSS value from a socket
        static int udt_mss_fn(int s) {
            etdc::udt_mss   mss;
            etdc::getsockopt(s, mss);
            return untag( mss );
        }
        // and one that extracts the UDT_MAXBW setting
        static int64_t udt_maxbw_fn(int s) {
            etdc::udt_max_bw   maxBW;
            etdc::getsockopt(s, maxBW);
            return untag( maxBW );
        }

        // provide correct wrappers around UDT::recv and UDT::send because
        // their signatures do not exactly match ::recv and ::send.
        // The wrapper's signatures do.
        ssize_t udtrecv(int s, void* b, size_t n, int f) {
            int   r = UDT::recv((UDTSOCKET)s, (char*)b, (int)n, f);

            if( r!=UDT::ERROR )
                return (ssize_t)r;
            // Hmmm. recv returned an error
            // We /copy/ the udt error to make sure it don't get
            // overwritten
            UDT::ERRORINFO   udtinfo( UDT::getlasterror() );
            const auto       udterrno( udtinfo.getErrorCode() );
            etdc::udt_rcvsyn blocking;

            etdc::getsockopt((UDTSOCKET)s, blocking);
            // Returning an error is not an error:
            //    blocking    + ECONNLOST (=> other side hung up)
            //    nonblocking + EASYNCRCV (=> no data was available)
            //    ETIMEOUT                (=> rcvtimeo was set but no data was received
            //                                this only applies to blocking sokkits)
            // POSIX sais:
            //    read() should return
            //       blocking:  0 on eof, -1 on error, >0 if data read.
            //                  we translate "-1 on error" to exception
            //       nonblocking:
            //                  -1 + errno = EAGAIN if no data available
            //                  >0    data was available
            //                  we only translate EASYNCRCV to "-1 + EAGAIN",
            //                  the rest is an error

            // Note: we already /know/ there was an error condition so we
            //       can now just filter out the non-errors, i.e. prevent
            //       exception throwing in case of not-an-actual-error
            ETDCSYSCALL( (blocking && ((udterrno==UDT::ERRORINFO::ECONNLOST)||(udterrno==UDT::ERRORINFO::ETIMEOUT))) ||
                         (!blocking && udterrno==UDT::ERRORINFO::EASYNCRCV),
                         "udtrecv(" << s << ", .., n=" << n << " ..)/" << udtinfo.getErrorMessage() << " (" << udterrno << ")" );
            // No exception was thrown so now we must actually transform the
            // UDT errno into a proper return value + optionally set errno
            return (ssize_t)((udterrno==UDT::ERRORINFO::ECONNLOST) ? 0 : (errno=EAGAIN, -1));
        }

        ssize_t udtsend(int s, const void* b, size_t n, int f) {
            int   r = UDT::send((UDTSOCKET)s, (const char*)b, (int)n, f);

            if( r==UDT::ERROR ) {
                UDT::ERRORINFO&  udterror = UDT::getlasterror();
                // If error==2001 ("Connection was broken." we return 0 in
                // stead of throwing
                // FIXME XXX Actually - this should be dependent on wether the socket
                // is in blocking mode or not ... yikes
                if( udterror.getErrorCode()!=2001 ) {
                    std::ostringstream oss;
                    oss << "udtsend(" << s << ", .., n=" << n << " ..)/" << udterror.getErrorMessage()
                        << " (" << udterror.getErrorCode() << ")";
                    throw std::runtime_error( oss.str() );
                }
                r = 0;
            }
            return (ssize_t)r;
        }
        // Again, UDT does not provide their API with socklen_t
        // so we wrap and make sure that sizeof socklen_t is compatible with
        // what UDT expects.
        // Also: the error handling for UDT APIs is different from libc. Jeebus.
        static const std::map<int, int> sockname_udt2libc{ {CUDTException::ENOCONN,   ENOTCONN}, 
                                                           {CUDTException::EINVPARAM, EINVAL},
                                                           {CUDTException::EINVSOCK,  ENOTSOCK} };

        int udt_sockname(int fd, struct sockaddr* addr, socklen_t* sl) {
            static_assert(sizeof(socklen_t)==sizeof(int), "UDT parameter int not compatible with socklen_t");

            const int udt_rv = UDT::getsockname(fd, addr, sl);
            // OK that didn't work - need to extract the UDT error and translate to standard errno
            if( udt_rv==UDT::ERROR ) {
                UDT::ERRORINFO   udtinfo( UDT::getlasterror() );
                auto const       pErr = sockname_udt2libc.find(udtinfo.getErrorCode());

                ETDCASSERT(pErr!=sockname_udt2libc.end(),
                           "UDT::getsockname() returned unrecognized error code " << udtinfo.getErrorCode() << " - " << udtinfo.getErrorMessage());
                errno = pErr->second;
            }
            return (udt_rv==UDT::ERROR);
        }
        int udt_peername(int fd, struct sockaddr* addr, socklen_t* sl) {
            static_assert(sizeof(socklen_t)==sizeof(int), "UDT parameter int not compatible with socklen_t");

            const int udt_rv = UDT::getpeername(fd, addr, sl);
            // OK that didn't work - need to extract the UDT error and translate to standard errno
            if( udt_rv==UDT::ERROR ) {
                UDT::ERRORINFO   udtinfo( UDT::getlasterror() );
                auto const       pErr = sockname_udt2libc.find(udtinfo.getErrorCode());

                ETDCASSERT(pErr!=sockname_udt2libc.end(),
                           "UDT::getpeername() returned unrecognized error code " << udtinfo.getErrorCode() << " - " << udtinfo.getErrorMessage());
                errno = pErr->second;
            }
            return (udt_rv==UDT::ERROR);
        }
    }

    // UDT over IPv4
    etdc_udt::etdc_udt() {
        auto proto = etdc::getprotobyname("tcp");
        if( (__m_fd=UDT::socket(PF_INET, SOCK_STREAM, proto.p_proto))==-1 )
            throw std::runtime_error( "etdc_udt: " + etdc::strerror(errno) );

        setup_basic_fns();
    }
    etdc_udt::etdc_udt(int fd) {
        ETDCASSERT(fd>=0, "constructing UDT file descriptor from invalid fd#" << fd);
        __m_fd = fd;
        // Update basic read/write/close functions
        setup_basic_fns();
    }

    void etdc_udt::setup_basic_fns( void ) {
        // Update basic read/write/close functions
        etdc::update_fd(*this, read_fn(std::bind(&detail::udtrecv, _1, _2, _3, 0)), 
                               write_fn(std::bind(&detail::udtsend, _1, _2, _3, 0)),
                               close_fn( &UDT::close ),
                               getsockname_fn( [](int fd) {
                                    return detail::ipv4_sockname<detail::udt_sockname, detail::udt_mss_fn, detail::udt_maxbw_fn>(fd, "udt", "getsockname"); } ),
                               getpeername_fn( [](int fd) {
                                    return detail::ipv4_sockname<detail::udt_peername>(fd, "udt", "getpeername"); } ),
                               // Setting blocking mode on an UDT socket is different 
                               setblocking_fn( [](int fd, bool blocking) {
                                   etdc::setsockopt(fd, etdc::udt_sndsyn(blocking), etdc::udt_rcvsyn(blocking));} )
                        );
    }

    etdc_udt::~etdc_udt() {}

    // UDT over IPv6
    etdc_udt6::etdc_udt6() {
        auto proto = etdc::getprotobyname("tcp");
        if( (__m_fd=UDT::socket(PF_INET6, SOCK_STREAM, proto.p_proto))==-1 )
            throw std::runtime_error( "etdc_udt6: " + etdc::strerror(errno) );

        setup_basic_fns();
    }
    etdc_udt6::etdc_udt6(int fd) {
        std::cout << "etdc_udt6::etdc_udt6(int " << fd << ")" << std::endl;
        ETDCASSERT(fd>=0, "constructing UDT6 file descriptor from invalid fd#" << fd);
        __m_fd = fd;
        // Update basic read/write/close functions
        setup_basic_fns();
    }

    void etdc_udt6::setup_basic_fns( void ) {
        // Most of the functions we can share with IPv4, only some need
        // overriding for IPv6:
        // http://long.ccaba.upc.es/long/045Guidelines/eva/ipv6.html
        
        // Get all IPv4 versions
        this->etdc_udt::setup_basic_fns();

        // Override the ones we need to for IPv6
        etdc::update_fd(*this, getsockname_fn( [](int fd) {
                                    return detail::ipv6_sockname<detail::udt_sockname, detail::udt_mss_fn, detail::udt_maxbw_fn>(fd, "udt6", "getsockname"); } ),
                               getpeername_fn( [](int fd) {
                                    return detail::ipv6_sockname<detail::udt_peername>(fd, "udt6", "getpeername"); } )
                        );
    }

    etdc_udt6::~etdc_udt6() {}


    namespace detail {
        // normalize path according to http://en.cppreference.com/w/cpp/filesystem/path
        // but we limit ourselves to '/' as preferred path separator.
        // Numbers below correspond to the step numbers in the algorithm described in the mentioned URL.
        //
        // Note: we do this here because currently our c++ only goes to 11, not 17 yet!
        // (In c++17 the filesystem library should be used)
        std::string normalize_path(std::string const& p) {
            // We keep the regexes statically compiled
            static const std::regex rxMultipleSeparators("(/+)");
            static const std::regex rxDotSlash("(/\\.(/|$))");
            static const std::regex rxDirSlashDotDot("(/(?!\\.\\.)[^/]+/\\.\\./)");
            static const std::regex rxRootDotDotSlash("^/((\\.\\./)*)");
            static const std::regex rxTrailingDotDot("/\\.\\./$");

            // Start with a copy of the input
            std::string result( p );

            // 2) multiple path separators into 1
            //result = std::regex_replace(result, regex("(/+)"), "/");
            result = std::regex_replace(result, rxMultipleSeparators, "/");

            // 4) Remove each dot and any immediately following directory-separator.
            //    we only remove /./ because we don't want to strip leading "./"
            //    nor break anything of the form ".../aap./..."
            //result = regex_replace(result, regex("(/\\.(/|$))"), "/");
            result = std::regex_replace(result, rxDotSlash, "/");

            // 5) Remove each non-dot-dot filename immediately followed by a directory-separator and a dot-dot,
            //    along with any immediately following directory-separator.
            bool    done = false;
            while( !done ) {
                //const string new_result( regex_replace(result, regex("(/(?!\\.\\.)[^/]+/\\.\\./)"), "/") );
                const std::string new_result( std::regex_replace(result, rxDirSlashDotDot, "/") );
                done   = (new_result==result);
                result = new_result;
            }

            // 6) If there is root-directory, remove all dot-dots and any directory-separators immediately following them.
            //result = regex_replace(result, regex("^/((\\.\\./)*)"), "/");
            result = std::regex_replace(result, rxRootDotDotSlash, "/");

            // 7) If the last filename is dot-dot, remove any trailing directory-separator.
            //result = regex_replace(result, regex("/\\.\\./$"), "/..");
            result = std::regex_replace(result, rxTrailingDotDot, "/..");

            // 8) If the path is empty, add a dot (normal form of ./ is .)
            if( result.empty() )
                result = ".";
            return result;
        }

        // ::dirname(3) and ::basename(3) require writable strings! Eeeeks!
        // reproduce the following behaviour:
        // Note: the "category" column added by HV, first three columns taken from 
        //       "man 3 basename"
        //
        // path       dirname   basename  category
        // /usr/lib   /usr      lib       (1)
        // /usr/      /         usr       (1)
        // usr        .         usr       (2)
        // /          /         /         (3)
        // .          .         .         (3)
        // ..         .         ..        (3)
        std::string dirname(std::string const& path) {
            // Any of the 'special' paths retun themselves [category (3)]
            if( path=="/" || path=="." || path==".." )
                return path;

            // from basename(3) (on OSX):
            // "If path is a null pointer, the empty string, or contains no `/'
            // characters, dirname() returns a pointer to the string ".",
            // signifying the current directory."
            if( path.empty() or path.find('/')==std::string::npos )
                return ".";

            // OK inspect what we got
            std::string::size_type epos = (path.empty() ? 0 : path.size()-1);

            // igore any trailing '/'es to make sure that epos 'points' at the last non-slash character;
            // we don't want to find the trailing slash
            while( epos>0 && path[epos]=='/' )
                epos -= 1;
            // Look for the last-but-one slash
            const std::string::size_type slash = path.rfind('/', epos);
            // no slash at all? [category (2)]
            if( slash==std::string::npos )
                return ".";
            // Remaining category [(1)] is the substring from start of path up to the slash
            return path.substr(0, slash+1);
        }

        std::string basename(std::string const& path) {
            static const std::regex allSlash("^\\\\+$");
            // Any of the 'special' paths retun themselves [category (3)]
            if( path=="/" || path=="." || path==".." )
                return path;

            // from basename(3) (on OSX):
            // "If path is a null pointer(*) or the empty string, a pointer to the string "." is returned."
            // (*) we don't have that possibility here so don't have to check for that
            if( path.empty() )
                return ".";

            // "If path consists entirely of `/' characters, a pointer to the string "/" is returned.
            if( std::regex_match(path, allSlash) )
                return "/";
            
            // "The basename() function returns the last component from the pathname pointed to by path, deleting any trailing `/' characters."
            // We've already ruled out the case that path consists of all slashes.
            // So now we must remove all trailing slashes
            auto  lastNonSlash = path.rbegin();
            // We can do this w/o any testing because of our previous assertions:
            //  * path is non-empty so *iter is always valid
            //  * path does not consist of all slashes so we're guaranteed to find a non-slash
            //    before hitting path.rend()
            while( *lastNonSlash=='/' )
                lastNonSlash++;

            // Now we must find the bit of string between lastNonSlash and the next
            std::string rv;
            std::reverse_copy(lastNonSlash, std::find(lastNonSlash, path.rend(), '/'), std::back_inserter(rv));
            return rv;
       }


    } // namespace detail

    ////////////////////////////////////////////////////////////////
    //   I/O to a non-existant file; /dev/null or /dev/zero
    ////////////////////////////////////////////////////////////////
    void devzeronull::setup_basic_fns( void ) {
        // Because this a pure memory file w/ no backing storage or file
        // system or O/S behind it, we must emulate the read, write, seek
        // and close calls
        etdc::update_fd(*this,
                        // we only update the file pointer, no i/o happens
                        // try to be POSIX compliant
                        // readin' always succeeds, apart from readin' past EOF
                        // or file not opened for readin
                        read_fn([this](int, void*, size_t n) {
                                // if this one wasn't opened for readin'
                                // return an errors
                                if( __m_closed || ((__m_mode&O_RDWR)!=O_RDWR && (__m_mode&O_RDONLY)!=O_RDONLY) ) {
                                    errno = EBADF;
                                    return (ssize_t)-1;
                                }
                                // 'no read shall happen past the end'
                                if( __m_fPointer>=__m_fSize )
                                    return (ssize_t)0;
                                // how many bytes can be read? save old file
                                // pointer
                                const std::size_t  old_fPointer = __m_fPointer;
                                // compute new file pointer, topping it off
                                // at file size
                                __m_fPointer = std::min(__m_fSize, __m_fPointer+n);
                                // And the difference between the two file
                                // pointers is the number of bytes 'read'
                                return (ssize_t)(__m_fPointer - old_fPointer);
                            }),
                        // we only update the file pointer, no i/o happens
                        // try to be POSIX compliant too.
                        // Writing always succeeds unless the file wasn't
                        // opened for writin'
                        write_fn([this](int, const void*, size_t n) {
                                // if this one wasn't opened for readin'
                                // return an errors
                                if( __m_closed || ((__m_mode&O_RDWR)==0 && (__m_mode&O_WRONLY)==0) ) {
                                    errno = EBADF;
                                    return (ssize_t)-1;
                                }
                                // bump the current file pointer
                                __m_fPointer += n;
                                // new file size is maximum of old size and
                                // new file pointer
                                __m_fSize     = std::max(__m_fSize, __m_fPointer);
                                return (ssize_t)n;
                            }),
                        // attempt to comply to POSIX
                        lseek_fn([this](int, off_t offset, int whence) {
                                if( __m_closed ) {
                                    errno = EBADF;
                                    return (off_t)-1;
                                }
                                std::size_t   new_fPointer;
                                switch( whence ) {
                                    case SEEK_SET:
                                        new_fPointer = offset;
                                        break;
                                    case SEEK_CUR:
                                        new_fPointer = __m_fPointer + offset;
                                        break;
                                    // return '-1' + EINVAL if 'whence' is
                                    // unrecognized or attempt to seek from end by
                                    // more than the file size
                                    case SEEK_END:
                                        if( offset>(off_t)__m_fSize ) {
                                            errno = EINVAL;
                                            return (off_t)-1;
                                        }
                                        new_fPointer = __m_fSize - offset;
                                        break;
                                    default:
                                        errno = EINVAL;
                                        return (off_t)-1;
                                }
                                return (off_t)(__m_fPointer = new_fPointer);
                            }),
                        // mark the file as closed
                        close_fn([this](int) { __m_closed = true; return 0; }),
                        // setting blocking flag doesn't do /anything/
                        setblocking_fn([](int, bool) { return; })
        );
    }
} // namespace etdc
