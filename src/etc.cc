#include <iostream>
#include <version.h>
#include <etdc_fd.h>
#include <map>

using namespace std;

int main( void ) {
    map<int,int> mi{ {0,1} };
    etdc_tcp    tcpSok;
    cout << buildinfo() << endl;
    cout << tcpSok.seek(42, 0) << endl;
    return 0;
}
