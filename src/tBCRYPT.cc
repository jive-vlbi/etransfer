// Quick KAT for the Blowfish core underpinning bcrypt_pbkdf: it validates
// that the pi-derived S-boxes + cipher are correct (standard zero-key vector).
//   make TLS=1 tBCRYPT && ./tBCRYPT
#include <iostream>

#ifdef ETDC_TLS

#include <etdc_bcrypt.h>

int main( void ) {
    const bool ok = etdc::auth::blowfish_self_test();
    std::cout << "tBCRYPT: Blowfish KAT (zero key/zero plaintext -> 0x4EF997456198DD78): "
              << (ok ? "PASS" : "FAIL") << std::endl;
    return ok ? 0 : 1;
}

#else

int main( void ) {
    std::cout << "tBCRYPT: built without TLS - nothing to test (use 'make TLS=1 tBCRYPT')" << std::endl;
    return 0;
}

#endif
