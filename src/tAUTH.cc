// Standalone test for the ssh-pubkey auth crypto (etdc_auth). Generates a
// key in-process, derives its ssh public-key blob, signs a fixed "exporter"
// buffer, then exercises verify()/authenticate() on the happy path and a
// range of tamper cases. Only meaningful in a TLS build.
//   make TLS=1 tAUTH && ./tAUTH
#include <iostream>
#include <string>
#include <vector>

#ifdef ETDC_TLS

#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

#include <etdc_auth.h>

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>

using etdc::auth::bytes;
using etdc::auth::pkey_ptr;

namespace {
    int g_fail = 0;
    void check(bool cond, std::string const& name) {
        std::cout << (cond ? "  ok  : " : "  FAIL: ") << name << std::endl;
        if( !cond ) g_fail++;
    }

    pkey_ptr keygen(int id, int rsa_bits) {
        EVP_PKEY_CTX* c = EVP_PKEY_CTX_new_id(id, nullptr);
        if( !c || EVP_PKEY_keygen_init(c)!=1 ) { EVP_PKEY_CTX_free(c); return pkey_ptr(); }
        if( id==EVP_PKEY_RSA )
            EVP_PKEY_CTX_set_rsa_keygen_bits(c, rsa_bits);
        if( id==EVP_PKEY_EC )
            EVP_PKEY_CTX_set_ec_paramgen_curve_nid(c, NID_X9_62_prime256v1);
        EVP_PKEY* pk = nullptr;
        const int rv = EVP_PKEY_keygen(c, &pk);
        EVP_PKEY_CTX_free(c);
        return (rv==1 && pk) ? pkey_ptr(pk, &EVP_PKEY_free) : pkey_ptr();
    }

    // The data the real protocol signs is the TLS exporter output; here we
    // just use a fixed buffer to stand in for it.
    bytes exporter_stub( void ) {
        bytes d(32);
        for(size_t i=0; i<d.size(); i++) d[i] = static_cast<unsigned char>(0xA0 ^ i);
        return d;
    }

    void run_one(std::string const& label, int id, int rsa_bits) {
        std::cout << "[" << label << "]" << std::endl;
        pkey_ptr key = keygen(id, rsa_bits);
        check(static_cast<bool>(key), label+": keygen");
        if( !key ) return;

        std::string key_algo;
        bytes       blob = etdc::auth::public_key_blob(key.get(), key_algo);
        const std::string sig_algo = etdc::auth::default_sig_algo(key.get());
        check(!blob.empty(), label+": public_key_blob");

        // base64 round-trip of the blob
        check(etdc::auth::base64_decode(etdc::auth::base64_encode(blob))==blob,
              label+": base64 round-trip");

        const bytes data = exporter_stub();
        const bytes sig  = etdc::auth::sign(key.get(), sig_algo, data);
        check(!sig.empty(), label+": sign");

        // happy path
        check(etdc::auth::verify(data, sig_algo, blob, sig), label+": verify good signature");

        // tamper: flip a byte of the signed data
        {
            bytes bad = data; bad[0] ^= 0x01;
            check(!etdc::auth::verify(bad, sig_algo, blob, sig), label+": reject tampered data");
        }
        // tamper: flip a byte deep inside the signature blob
        {
            bytes bad = sig; bad[bad.size()-1] ^= 0x01;
            check(!etdc::auth::verify(data, sig_algo, blob, bad), label+": reject tampered signature");
        }
        // tamper: a different algorithm name on the wire than the one baked
        // into the signature blob must be refused
        {
            const std::string other = (sig_algo=="ssh-ed25519") ? std::string("rsa-sha2-256")
                                                                 : std::string("ssh-ed25519");
            check(!etdc::auth::verify(data, other, blob, sig), label+": reject mismatched algorithm");
        }

        // authenticate() against an authorized_keys file
        const std::string dir("/tmp");
        const std::string principal("tauthuser");
        const std::string path(dir+"/"+principal);
        {
            std::ofstream of(path.c_str());
            of << key_algo << " " << etdc::auth::base64_encode(blob) << " tAUTH test key\n";
        }
        std::string comment;
        check(etdc::auth::authenticate(dir, principal, data, sig_algo, blob, sig, comment),
              label+": authenticate authorized principal");
        check(comment=="tAUTH test key", label+": authenticate returns key comment");

        // unknown principal -> no file -> reject
        std::string c2;
        check(!etdc::auth::authenticate(dir, "nosuchuser", data, sig_algo, blob, sig, c2),
              label+": reject unknown principal");
        // tampered data with a valid authorized key -> reject
        {
            bytes bad = data; bad[1] ^= 0x80;
            std::string c3;
            check(!etdc::auth::authenticate(dir, principal, bad, sig_algo, blob, sig, c3),
                  label+": authenticate rejects bad signature");
        }
        ::unlink(path.c_str());
    }
} // anonymous namespace

int main( void ) {
    std::cout << "tAUTH: ssh-pubkey auth crypto self-test\n" << std::endl;
    run_one("ed25519",            EVP_PKEY_ED25519, 0);
    run_one("rsa-sha2-256",       EVP_PKEY_RSA,     2048);
    run_one("ecdsa-sha2-nistp256",EVP_PKEY_EC,      0);

    std::cout << "\ntAUTH: " << (g_fail==0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << std::endl;
    return g_fail==0 ? 0 : 1;
}

#else  // !ETDC_TLS

int main( void ) {
    std::cout << "tAUTH: built without TLS support - nothing to test (rebuild with 'make TLS=1 tAUTH')" << std::endl;
    return 0;
}

#endif
