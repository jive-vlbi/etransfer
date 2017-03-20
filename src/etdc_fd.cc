// implementation of base- and derived classes for wrapping file descriptors
//
#include <etdc_fd.h>
#include <reentrant.h>
#include <etdc_assert.h>
#include <etdc_nullfn.h>

#include <ios>
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
        // what UDT expects
        int udt_sockname(int fd, struct sockaddr* addr, socklen_t* sl) {
            static_assert(sizeof(socklen_t)==sizeof(int), "UDT parameter int not compatible with socklen_t");
            return UDT::getsockname(fd, addr, reinterpret_cast<int*>(sl));
        }
        int udt_peername(int fd, struct sockaddr* addr, socklen_t* sl) {
            static_assert(sizeof(socklen_t)==sizeof(int), "UDT parameter int not compatible with socklen_t");
            return UDT::getpeername(fd, addr, reinterpret_cast<int*>(sl));
        }
    }

    etdc_udt::etdc_udt() {
        std::cout << "etdc_udt::etdc_udt()" << std::endl;
        auto proto = etdc::getprotobyname("tcp");
        if( (__m_fd=UDT::socket(PF_INET, SOCK_STREAM, proto.p_proto))==-1 )
            throw std::runtime_error( "etdc_udt: " + etdc::strerror(errno) );
        std::cout << "  __m_fd=" << __m_fd << std::endl;

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
}
