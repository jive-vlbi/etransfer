#include <etdc_signal.h>
#include <etdc_thread.h>
//#include <keywordargs.h>
#include <stdkeys.h>

namespace stdkeys = etdc::stdkeys;

#include <list>
#include <utility>
#include <iostream>


using std::cout;
using std::endl;

struct OhMyStruct :
    public etdc::KeywordDict<OhMyStruct>
{
    int   i1, i2;
    float f1;
    template <typename... Args>
    OhMyStruct(Args&&... args):
        //KeywordDict(mk_kv(3, &OhMyStruct::i1), mk_kv("i1", &OhMyStruct::f1)),
        KeywordDict(std::make_pair(3, &OhMyStruct::i1), mk_kv("i1", &OhMyStruct::f1)),
        f1( 3.14 )
    {
        this->update(*this, std::forward<Args>(args)...);
    }
};
std::ostream& operator<<(std::ostream& os, OhMyStruct const& oms) {
    return os << "OMS{i1: " << oms.i1 << ", i2: " << oms.i2 << ", f1: " << oms.f1 << "}";
}


struct NohMyStruct {
    int   MTU;
    float f1;
    NohMyStruct(): MTU(0), f1(0.0f) {}
};

std::ostream& operator<<(std::ostream& os, NohMyStruct const& noms) {
    //return os << "NoMS[" << static_cast<void const*>(&noms) << "]{MTU: " << noms.MTU << ", f1: " << noms.f1 << "}";
    return os << "NoMS{MTU: " << noms.MTU << ", f1: " << noms.f1 << "}";
}


static const etdc::KeywordDict<NohMyStruct> kwmap{ key(3)=&NohMyStruct::f1, stdkeys::mtu=&NohMyStruct::MTU };

int main( void ) {
    OhMyStruct oms0;
    OhMyStruct oms2{ key("i1")=42.0f, key(3)=33 };
    OhMyStruct oms1;//( kv("i1"_key, -1.0f), kv(3, 42) );

    update( oms1, std::make_pair(3, -1), "i1"_key=2.71f );
    cout << "oms1 = " << oms1 << endl
         << "oms2 = " << oms2 << endl
         << "oms0 = " << oms0 << endl;

    NohMyStruct noms, noms2, noms3;
    //noms.f1 = 5.5e-31;
    noms3 = noms2 = noms;
    cout << noms << " (" << noms2 << ", " << noms3 << ")" << endl;
    kwmap.update(noms, stdkeys::mtu=9000);
    cout << noms << " (" << noms2 << ", " << noms3 << ") memcmp(noms, noms2)=" << ::memcmp(&noms, &noms2, sizeof(NohMyStruct)) << endl;
    noms2 = noms;
    kwmap.update(noms, key(3)=2.71f);
    cout << noms << " (" << noms2 << ", " << noms3 << ") memcmp(noms, noms2)=" << ::memcmp(&noms, &noms2, sizeof(NohMyStruct)) 
         << " memcmp(noms, noms3)=" << ::memcmp(&noms, &noms3, sizeof(NohMyStruct)) << endl;
    return 0;
}


#if 0
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
#endif
