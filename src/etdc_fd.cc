// implementation of base- and derived classes for wrapping file descriptors
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

    // For std::bind( ..., _1, ...) &cet
    using namespace std::placeholders;

    namespace detail {
        // Template for getsockname/getpeername - it's all the same error
        // checking and return value creation, only which bit of the address
        // to process is different
        template <int (*fptr)(int, struct sockaddr*, socklen_t*)>
        sockname_type ipv4_sockname(int fd, std::string const& p, std::string const& s) {
            socklen_t          len( sizeof(struct sockaddr_in) );
            struct sockaddr_in saddr;

            ETDCASSERT( fptr(fd, reinterpret_cast<struct sockaddr*>(&saddr), &len)==0,
                        s << " for protocol=" << p << " fails - " << etdc::strerror(errno) );
            // transform the IPv4 address to string
            char    addr_s[ INET_ADDRSTRLEN+1 ];

            ETDCASSERT( ::inet_ntop(AF_INET, &saddr.sin_addr.s_addr, addr_s, len)!=nullptr,
                        "inet_ntop() fails - "<< etdc::strerror(errno) );
            return mk_sockname(proto(p), host(addr_s), port(etdc::ntohs_(saddr.sin_port)));
        }


        // Id. for IPv6 - we need different sockaddr + addrstrlen and address family
        template <int (*fptr)(int, struct sockaddr*, socklen_t*)>
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
            return mk_sockname(proto(p), host(std::string("[")+addr_s+"]"), port(etdc::ntohs_(saddr.sin6_port)));
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

            const int udt_rv = UDT::getsockname(fd, addr, reinterpret_cast<int*>(sl));
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

            const int udt_rv = UDT::getpeername(fd, addr, reinterpret_cast<int*>(sl));
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
        std::cout << "etdc_udt::etdc_udt(int " << fd << ")" << std::endl;
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
                                    return detail::ipv4_sockname<detail::udt_sockname>(fd, "udt", "getsockname"); } ),
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
                                    return detail::ipv6_sockname<detail::udt_sockname>(fd, "udt6", "getsockname"); } ),
                               getpeername_fn( [](int fd) {
                                    return detail::ipv6_sockname<detail::udt_peername>(fd, "udt6", "getpeername"); } )
                        );
    }

    etdc_udt6::~etdc_udt6() {}


    ////////////////////////////////////////////////////////////////
    //   I/O to a regular file
    ////////////////////////////////////////////////////////////////
    void etdc_file::setup_basic_fns( void ) {
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

        // ::dirname(3) requires a writable string! Eeeeks!
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
    } // namespace detail
} // namespace etdc
