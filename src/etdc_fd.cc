// implementation of base- and derived classes for wrapping file descriptors
//
#include <etdc_fd.h>
#include <reentrant.h>

#include <stdexcept>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

namespace etdc {

    // TCP sockets
    etdc_tcp::etdc_tcp() {
        std::cout << "etdc_tcp::etdc_tcp()" << std::endl;
        auto proto = etdc::getprotobyname("tcp");
        if( (__m_fd=::socket(PF_INET, SOCK_STREAM, proto.p_proto))==-1 )
            throw std::runtime_error( "etdc_tcp: " + etdc::strerror(errno) );
        std::cout << "  __m_fd=" << __m_fd << std::endl;
    }

    etdc_tcp::~etdc_tcp() {
        std::cout << "etdc_tcp::~etdc_tcp()/__m_fd=" << __m_fd << std::endl;
        if( __m_fd!=-1 )
            ::close( __m_fd );
    }

    // UDT sockets
    etdc_udt::etdc_udt() {
        std::cout << "etdc_udt::etdc_udt()" << std::endl;
        auto proto = etdc::getprotobyname("tcp");
        if( (__m_fd=UDT::socket(PF_INET, SOCK_STREAM, proto.p_proto))==-1 )
            throw std::runtime_error( "etdc_udt: " + etdc::strerror(errno) );
        std::cout << "  __m_fd=" << __m_fd << std::endl;
    }

    etdc_udt::~etdc_udt() {
        std::cout << "etdc_udt::~etdc_udt()/__m_fd=" << __m_fd << std::endl;
        if( __m_fd!=(UDTSOCKET)-1 )
            UDT::close( __m_fd );
    }
}
