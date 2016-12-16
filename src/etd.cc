#include <etdc_signal.h>
#include <etdc_thread.h>

#include <list>
#include <iostream>


using std::cout;
using std::endl;

int foo(int a, char b) {
    cout << "foo(" << a << ", " << b << ")" << endl;
    cout << etdc::showMaskInHRF;
    etdc::GetMask gm{};
    cout << etdc::pushMaskDisplayFormat << etdc::showMaskInHex << "   getmask: " << gm << etdc::popMaskDisplayFormat << endl;
    etdc::DelMask   ub{SIGUSR1};
    etdc::GetMask gm2{};
    cout << "    after unblock: sigmask = " << gm2 << endl;
    return 1+a;
}

int main(void) {
    std::list<int>  li{SIGSEGV, SIGUSR1, SIGKILL}; 

    etdc::Block         gm{SIGILL};

    cout << etdc::showMaskInHRF << "GetMask: " << gm << endl;
    etdc::AddMask         sm3{li};
    cout << "AddMask: " << etdc::showMaskInHex << sm3 << endl;
    cout << "AddMask (again): " << sm3 << endl;
    //foo(-1, 'b');
    cout << "GetMask/1: " << etdc::GetMask{} << endl << etdc::showMaskInHRF << etdc::GetMask{} << endl;
    cout << "===============================================" << endl;
    std::thread  t = etdc::thread(foo, 42, 'a');
    t.join(); 
    cout << "===============================================" << endl;
    //foo(-2, 'c');
    cout << "GetMask/2: " << etdc::showMaskInHex << etdc::GetMask{} << endl << etdc::showMaskInHRF << etdc::GetMask{} << endl;
    return 0;
}
