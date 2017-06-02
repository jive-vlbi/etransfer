#include <list>
#include <set>
#include <thread>
#include <iostream>
#include <iomanip>
#include <type_traits>
#include <utility>
#include <numeric>

#include <etdc_signal.h>
#include <etdc_thread.h>
#include <etdc_fd.h>

using std::cout;
using std::endl;
using std::flush;

int main( void ) {
    auto pServer = mk_server("tcp", port(2620));
    
    cout << "Server is-at " << pServer->getsockname(pServer->__m_fd) << endl;
    
    auto clnt = pServer->accept(pServer->__m_fd);
    cout << "Incoming from " << clnt->getpeername(clnt->__m_fd) << " [local " << clnt->getsockname(clnt->__m_fd) << "]" << endl;
    
    char    buf[1024];
    ssize_t n = 0, r;
    while( (r = clnt->read(clnt->__m_fd, buf, sizeof(buf)))>0 )
        n += r, cout << "." << flush;
    cout << "OK - client sent " << n << " bytes" << endl;
    return 0;
}



// list of thunks [ std::function<void(*)(void)> ]
// own thread creation wrapper - ignore all signals before creating thread
// return std::thread&& - such that it can be a true wrappert!

#if 0
int main( void ) {
    etdc::etdc_fdptr  pSok;
    etdc::so_reuseaddr ra;
    etdc::udt_reuseaddr ura;
    etdc::udt_rcvbuf   udtrcv;
    etdc::so_rcvbuf    tcprcv;

    pSok = mk_server("tcp", port(2620));//mk_socket("tcp");
    std::cout << "Server is-at " << pSok->getsockname(pSok->__m_fd) << std::endl;
    etdc::getsockopt(pSok->__m_fd, ra, tcprcv);
    std::cout << "TCP: reuseaddr=" << ra << ", backlog=" << tcprcv << std::endl;
    //struct sockaddr_in sa;
    //socklen_t          sl( sizeof(struct sockaddr_in) );
    std::cout << "Calling accept" << std::endl;
    //auto clnt = pSok->accept(pSok->__m_fd, reinterpret_cast<struct sockaddr*>(&sa), &sl);
    auto clnt = pSok->accept(pSok->__m_fd);
    std::cout << "Incoming from " << clnt->getpeername(clnt->__m_fd) << " [local " << clnt->getsockname(clnt->__m_fd) << "]";

    pSok = mk_server("udt", port(800002));
    etdc::setsockopt(pSok->__m_fd, etdc::udt_reuseaddr(false));
    etdc::getsockopt(pSok->__m_fd, udtrcv, ura);
    std::cout << "UDT: udtrcv=" << udtrcv << ", reuseaddr=" << ura << std::endl;

    return 0;
}
#endif
