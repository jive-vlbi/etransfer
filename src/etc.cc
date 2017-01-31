// Own includes
#include <version.h>
#include <etdc_fd.h>
#include <streamutil.h>

// C++ standard headers
#include <map>
#include <iostream>

using namespace std;

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
