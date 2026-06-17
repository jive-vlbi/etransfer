// OpenSSH bcrypt_pbkdf key-derivation (Blowfish-based) for decrypting
// passphrase-protected OpenSSH-native private keys. Only built with ETDC_TLS.
// Author: Harro Verkouter - verkouter@jive.eu
#ifndef ETDC_ETDC_BCRYPT_H
#define ETDC_ETDC_BCRYPT_H

#ifdef ETDC_TLS

#include <cstdint>
#include <cstddef>

namespace etdc { namespace auth {

    // OpenSSH bcrypt_pbkdf. Derives 'keylen' bytes into 'key' from
    // (pass,salt) over 'rounds' iterations. Returns 0 on success, -1 on a
    // bad/ out-of-range argument. This is the KDF used by OpenSSH's
    // "BEGIN OPENSSH PRIVATE KEY" container when the key is encrypted.
    int  bcrypt_pbkdf(const uint8_t* pass, size_t passlen,
                      const uint8_t* salt, size_t saltlen,
                      uint8_t* key, size_t keylen, unsigned int rounds);

    // Known-answer self-test of the internal Blowfish core (zero key / zero
    // plaintext -> 0x4EF99745 0x6198DD78). Returns true on success. Used by
    // the test harness to validate the pi-derived S-boxes + cipher.
    bool blowfish_self_test( void );

}} // namespace etdc::auth

#endif // ETDC_TLS
#endif // ETDC_ETDC_BCRYPT_H
