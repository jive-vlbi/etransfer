#include <etdc_fd.h>
#include <iostream>

using std::cout;
using std::endl;
using std::flush;

int main( void ) {
    //etdc::so_rcvbuf  rcv;
    etdc::udt_rcvbuf  rcv;
    auto pServer = mk_server("udt", port(2620), etdc::udt_rcvbuf{2*1024*1024} /*etdc::so_rcvbuf{2*1024}*/);
    
    cout << "etd: server is-at " << pServer->getsockname(pServer->__m_fd) << endl;
    etdc::getsockopt(pServer->__m_fd, rcv/*, sync*/);
    cout << "  server socket has receive buffer size of: " << rcv /*<< " and sync=" << std::boolalpha << sync*/ << endl;
    
    auto clnt = pServer->accept(pServer->__m_fd);
    cout << "Incoming from " << clnt->getpeername(clnt->__m_fd) << " [local " << clnt->getsockname(clnt->__m_fd) << "]" << endl;
    etdc::getsockopt(clnt->__m_fd, rcv/*, sync*/);
    cout << "  client socket has receive buffer size of: " << rcv /*<< " and sync=" << std::boolalpha << sync*/ << endl;
    
    char    buf[1024];
    ssize_t n = 0, r;
    while( (r = clnt->read(clnt->__m_fd, buf, sizeof(buf)))>0 )
        n += r, cout << "." << flush;
    cout << "OK - client sent " << n << " bytes" << endl;
    return 0;
}





#if 0
#include <etdc_signal.h>
#include <etdc_thread.h>
//#include <keywordargs.h>
#include <stdkeys.h>

namespace stdkeys = etdc::stdkeys;

#include <list>
#include <utility>
#include <iostream>
#endif
