// etransfer server program/etdc=etransfer daemon + client
#include <version.h>
#include <etdc_fd.h>
#include <etdc_thread.h>
#include <etdc_stringutil.h>
#include <argparse.h>

#include <map>
#include <thread>
#include <string>
#include <vector>
#include <iterator>
#include <iostream>
#include <functional>

// C++ standard headers
#include <map>
#include <iostream>

using     namespace     std;
namespace AP =          argparse;


int main(int, char const*const*const argv) {
    auto pClnt = mk_client("udt", host(argv[1] ? argv[1] : "" ), port(8008));
    cout << "connected to " << pClnt->getpeername(pClnt->__m_fd) << " [local " << pClnt->getsockname(pClnt->__m_fd) << "]" << endl;
    const auto data = "012345";
    pClnt->write(pClnt->__m_fd, data, sizeof(data));
    cout << "wrote " << sizeof(data) << " bytes" << endl;
    return 0;
}


#if 0
int main( void ) {
    map<int,int>    mi{ {0,1} };
    etdc::etdc_tcp  tcpSok;
    etdc::port_type p = mk_port("1024");
    cout << buildinfo() << endl;
    cout << etdc::fmt_tuple(mk_ipport("1.2.3.4", mk_port(13))) << endl;
    cout << etdc::fmt_tuple(mk_sockname("tcp", "1.2.3.4")) << endl;
    cout << "port = " << p << endl;
    cout << tcpSok.seek(42, 0) << endl;
    return 0;
}
#endif
