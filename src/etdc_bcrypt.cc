// OpenSSH bcrypt_pbkdf - see etdc_bcrypt.h.
//
// The Blowfish cipher + EksBlowfish "expensive" key schedule and the
// bcrypt_pbkdf construction follow the public-domain OpenBSD implementation
// (Niels Provos / Ted Unangst). The 1042 Blowfish initialisation words are
// the fractional hexadecimal digits of pi; rather than embedding that large
// table verbatim we derive it once at first use from pi (Machin's formula,
// exact integer arithmetic via OpenSSL BIGNUM) and cache it. The result is
// validated by blowfish_self_test() (standard Blowfish KAT).
//
// Author: Harro Verkouter - verkouter@jive.eu
#include <etdc_bcrypt.h>

#ifdef ETDC_TLS

#include <cstring>
#include <cassert>
#include <vector>
#include <algorithm>

#include <openssl/bn.h>
#include <openssl/evp.h>

namespace etdc { namespace auth {

namespace {
    const int BLF_N = 16;

    struct blf_ctx {
        uint32_t S[4][256];
        uint32_t P[BLF_N + 2];
    };

    // F + round macros, identical in structure to OpenBSD blf.c. 'x' is a
    // simple variable in every use below, so multiple evaluation is fine.
    #define ETDC_BF_F(s, x) ((((s)[0][((x)>>24)&0xff] + (s)[1][((x)>>16)&0xff]) ^ (s)[2][((x)>>8)&0xff]) + (s)[3][(x)&0xff])
    #define ETDC_BF_RND(s, p, i, j, n) (i ^= ETDC_BF_F((s), (j)) ^ (p)[n])

    void Blowfish_encipher(const blf_ctx* c, uint32_t* xl, uint32_t* xr) {
        uint32_t l = *xl, r = *xr;
        l ^= c->P[0];
        ETDC_BF_RND(c->S, c->P, r, l, 1);  ETDC_BF_RND(c->S, c->P, l, r, 2);
        ETDC_BF_RND(c->S, c->P, r, l, 3);  ETDC_BF_RND(c->S, c->P, l, r, 4);
        ETDC_BF_RND(c->S, c->P, r, l, 5);  ETDC_BF_RND(c->S, c->P, l, r, 6);
        ETDC_BF_RND(c->S, c->P, r, l, 7);  ETDC_BF_RND(c->S, c->P, l, r, 8);
        ETDC_BF_RND(c->S, c->P, r, l, 9);  ETDC_BF_RND(c->S, c->P, l, r, 10);
        ETDC_BF_RND(c->S, c->P, r, l, 11); ETDC_BF_RND(c->S, c->P, l, r, 12);
        ETDC_BF_RND(c->S, c->P, r, l, 13); ETDC_BF_RND(c->S, c->P, l, r, 14);
        ETDC_BF_RND(c->S, c->P, r, l, 15); ETDC_BF_RND(c->S, c->P, l, r, 16);
        *xl = r ^ c->P[17];
        *xr = l;
    }

    void blf_enc(const blf_ctx* c, uint32_t* data, uint16_t blocks) {
        uint32_t* d = data;
        for(uint16_t i = 0; i < blocks; i++) {
            Blowfish_encipher(c, d, d + 1);
            d += 2;
        }
    }

    // Read 4 bytes big-endian from 'data', cyclically, advancing *current.
    uint32_t stream2word(const uint8_t* data, uint16_t databytes, uint16_t* current) {
        uint16_t j = *current;
        uint32_t temp = 0;
        for(int i = 0; i < 4; i++) {
            temp = (temp << 8) | data[j];
            j = static_cast<uint16_t>((j + 1) % databytes);
        }
        *current = j;
        return temp;
    }

    // --- pi-derived Blowfish init state (computed once, cached) -----------
    void arctanK(BIGNUM* result, BN_ULONG n, const BIGNUM* K) {
        BIGNUM*        sum   = BN_new();
        BIGNUM*        power = BN_new();
        BIGNUM*        term  = BN_new();
        const BN_ULONG n2    = n * n;
        BN_zero(sum);
        BN_copy(power, K);
        BN_div_word(power, n);          // power = K / n           (k=0 term base)
        for(unsigned long k = 0; !BN_is_zero(power); k++) {
            BN_copy(term, power);
            BN_div_word(term, 2UL*k + 1);
            if( (k & 1U)==0 ) BN_add(sum, sum, term);
            else              BN_sub(sum, sum, term);
            BN_div_word(power, n2);     // power /= n^2  -> next term base
        }
        BN_copy(result, sum);
        BN_free(sum); BN_free(power); BN_free(term);
    }

    void compute_pi_init(uint32_t P[BLF_N + 2], uint32_t S[4][256]) {
        const int nWords = (BLF_N + 2) + 4*256;   // 1042
        const int nHex   = nWords * 8;            // 8336 fractional hex digits
        const int guard  = 48;
        const int D      = nHex + guard;

        BIGNUM* K    = BN_new();
        BIGNUM* a5   = BN_new();
        BIGNUM* a239 = BN_new();
        BIGNUM* t1   = BN_new();
        BIGNUM* t2   = BN_new();
        BIGNUM* piK  = BN_new();

        BN_one(K);
        BN_lshift(K, K, 4*D);                      // K = 16^D = 2^(4D)
        arctanK(a5,   5,   K);
        arctanK(a239, 239, K);
        BN_lshift(t1, a5,   4);                    // 16 * arctan(1/5)
        BN_lshift(t2, a239, 2);                    //  4 * arctan(1/239)
        BN_sub(piK, t1, t2);                       // pi * 16^D (floored)

        char*             hexc = BN_bn2hex(piK);   // uppercase
        const std::string hex(hexc ? hexc : "");
        OPENSSL_free(hexc);

        // BN_bn2hex pads to an even number of nibbles (it may prepend a '0').
        // The integer part of pi is '3' (the first non-zero nibble); the
        // fractional hex digits 243F6A88... begin right after it.
        const std::string::size_type intDigit = hex.find_first_not_of('0');
        const std::string::size_type fracPos  = (intDigit==std::string::npos ? 1 : intDigit + 1);
        for(int w = 0; w < nWords; w++) {
            const std::string grp = hex.substr(fracPos + 8*w, 8);
            const uint32_t    val = static_cast<uint32_t>(strtoul(grp.c_str(), nullptr, 16));
            if( w < (BLF_N + 2) ) {
                P[w] = val;
            } else {
                const int idx = w - (BLF_N + 2);
                S[idx / 256][idx % 256] = val;
            }
        }
        assert(P[0]==0x243f6a88u && P[1]==0x85a308d3u);

        BN_free(K); BN_free(a5); BN_free(a239); BN_free(t1); BN_free(t2); BN_free(piK);
    }

    const blf_ctx& pi_template( void ) {
        // C++11 thread-safe "magic static": compute_pi_init runs exactly once.
        static const blf_ctx tmpl = []() -> blf_ctx {
            blf_ctx c;
            compute_pi_init(c.P, c.S);
            return c;
        }();
        return tmpl;
    }

    void Blowfish_initstate(blf_ctx* c) {
        *c = pi_template();
    }

    void Blowfish_expand0state(blf_ctx* c, const uint8_t* key, uint16_t keybytes) {
        uint16_t j = 0;
        uint32_t datal = 0, datar = 0;
        for(int i = 0; i < BLF_N + 2; i++)
            c->P[i] ^= stream2word(key, keybytes, &j);
        for(int i = 0; i < BLF_N + 2; i += 2) {
            Blowfish_encipher(c, &datal, &datar);
            c->P[i] = datal; c->P[i+1] = datar;
        }
        for(int i = 0; i < 4; i++)
            for(int k = 0; k < 256; k += 2) {
                Blowfish_encipher(c, &datal, &datar);
                c->S[i][k] = datal; c->S[i][k+1] = datar;
            }
    }

    void Blowfish_expandstate(blf_ctx* c, const uint8_t* data, uint16_t databytes,
                              const uint8_t* key, uint16_t keybytes) {
        uint16_t j = 0;
        uint32_t datal = 0, datar = 0;
        for(int i = 0; i < BLF_N + 2; i++)
            c->P[i] ^= stream2word(key, keybytes, &j);
        j = 0;
        for(int i = 0; i < BLF_N + 2; i += 2) {
            datal ^= stream2word(data, databytes, &j);
            datar ^= stream2word(data, databytes, &j);
            Blowfish_encipher(c, &datal, &datar);
            c->P[i] = datal; c->P[i+1] = datar;
        }
        for(int i = 0; i < 4; i++)
            for(int k = 0; k < 256; k += 2) {
                datal ^= stream2word(data, databytes, &j);
                datar ^= stream2word(data, databytes, &j);
                Blowfish_encipher(c, &datal, &datar);
                c->S[i][k] = datal; c->S[i][k+1] = datar;
            }
    }

    // --- bcrypt core ------------------------------------------------------
    const int BCRYPT_WORDS    = 8;
    const int BCRYPT_HASHSIZE = 32;

    void sha512(const uint8_t* d, size_t n, uint8_t out[64]) {
        unsigned int olen = 0;
        EVP_Digest(d, n, out, &olen, EVP_sha512(), nullptr);
    }

    void bcrypt_hash(const uint8_t* sha2pass, const uint8_t* sha2salt, uint8_t* out) {
        blf_ctx  state;
        uint8_t  ciphertext[BCRYPT_HASHSIZE];
        uint32_t cdata[BCRYPT_WORDS];
        uint16_t j = 0;

        std::memcpy(ciphertext, "OxychromaticBlowfishSwatDynamite", BCRYPT_HASHSIZE);

        Blowfish_initstate(&state);
        Blowfish_expandstate(&state, sha2salt, 64, sha2pass, 64);
        for(int i = 0; i < 64; i++) {
            Blowfish_expand0state(&state, sha2salt, 64);
            Blowfish_expand0state(&state, sha2pass, 64);
        }
        for(int i = 0; i < BCRYPT_WORDS; i++)
            cdata[i] = stream2word(ciphertext, BCRYPT_HASHSIZE, &j);
        for(int i = 0; i < 64; i++)
            blf_enc(&state, cdata, BCRYPT_WORDS / 2);
        // little-endian output
        for(int i = 0; i < BCRYPT_WORDS; i++) {
            out[4*i + 3] = static_cast<uint8_t>((cdata[i] >> 24) & 0xff);
            out[4*i + 2] = static_cast<uint8_t>((cdata[i] >> 16) & 0xff);
            out[4*i + 1] = static_cast<uint8_t>((cdata[i] >>  8) & 0xff);
            out[4*i + 0] = static_cast<uint8_t>( cdata[i]        & 0xff);
        }
    }
} // anonymous namespace

    int bcrypt_pbkdf(const uint8_t* pass, size_t passlen,
                     const uint8_t* salt, size_t saltlen,
                     uint8_t* key, size_t keylen, unsigned int rounds) {
        uint8_t        sha2pass[64], sha2salt[64];
        uint8_t        out[BCRYPT_HASHSIZE], tmpout[BCRYPT_HASHSIZE];
        const size_t   origkeylen = keylen;

        if( rounds < 1 || passlen == 0 || saltlen == 0 ||
            keylen == 0 || keylen > 1024 || saltlen > (1u<<20) )
            return -1;

        std::vector<uint8_t> countsalt(saltlen + 4);
        std::memcpy(countsalt.data(), salt, saltlen);

        const size_t stride = (keylen + BCRYPT_HASHSIZE - 1) / BCRYPT_HASHSIZE;
        size_t       amt    = (keylen + stride - 1) / stride;

        sha512(pass, passlen, sha2pass);

        size_t   written;
        for(uint32_t count = 1; keylen > 0; count++) {
            countsalt[saltlen + 0] = static_cast<uint8_t>((count >> 24) & 0xff);
            countsalt[saltlen + 1] = static_cast<uint8_t>((count >> 16) & 0xff);
            countsalt[saltlen + 2] = static_cast<uint8_t>((count >>  8) & 0xff);
            countsalt[saltlen + 3] = static_cast<uint8_t>( count        & 0xff);

            sha512(countsalt.data(), saltlen + 4, sha2salt);
            bcrypt_hash(sha2pass, sha2salt, tmpout);
            std::memcpy(out, tmpout, sizeof(out));

            for(unsigned int r = 1; r < rounds; r++) {
                sha512(tmpout, sizeof(tmpout), sha2salt);
                bcrypt_hash(sha2pass, sha2salt, tmpout);
                for(size_t k = 0; k < sizeof(out); k++)
                    out[k] ^= tmpout[k];
            }

            // pbkdf2 deviation: distribute the block non-linearly
            amt = std::min(amt, keylen);
            written = 0;
            for(size_t i = 0; i < amt; i++) {
                const size_t dest = i * stride + (count - 1);
                if( dest >= origkeylen )
                    break;
                key[dest] = out[i];
                written++;
            }
            keylen -= written;
        }

        // wipe sensitive stack material
        OPENSSL_cleanse(sha2pass, sizeof(sha2pass));
        OPENSSL_cleanse(sha2salt, sizeof(sha2salt));
        OPENSSL_cleanse(out,      sizeof(out));
        OPENSSL_cleanse(tmpout,   sizeof(tmpout));
        return 0;
    }

    bool blowfish_self_test( void ) {
        blf_ctx       c;
        const uint8_t key[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        uint32_t      l = 0, r = 0;
        Blowfish_initstate(&c);
        Blowfish_expand0state(&c, key, 8);
        Blowfish_encipher(&c, &l, &r);
        return l == 0x4EF99745u && r == 0x6198DD78u;
    }

    #undef ETDC_BF_F
    #undef ETDC_BF_RND

}} // namespace etdc::auth

#endif // ETDC_TLS
