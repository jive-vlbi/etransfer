// ssh-pubkey authentication helpers (Phase 2). See etdc_auth.h.
// Author: Harro Verkouter - verkouter@jive.eu
#include <etdc_auth.h>

#ifdef ETDC_TLS

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

#include <termios.h>
#include <unistd.h>

#include <etdc_bcrypt.h>

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/obj_mac.h>

// OpenSSL 3.0 deprecates the low-level RSA_*/EC_KEY_* key-construction API
// that we deliberately use to stay compatible with OpenSSL 1.1.1 / LibreSSL
// (see docs/tls-design.md sec 6). Silence the deprecation noise for this
// module only; we are knowingly pinned to that API subset.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace etdc { namespace auth {

namespace {
    // Drain the OpenSSL error queue into a readable string.
    std::string ssl_err( void ) {
        std::string out; unsigned long e; char buf[256];
        while( (e=ERR_get_error())!=0 ) {
            ERR_error_string_n(e, buf, sizeof(buf));
            if( !out.empty() ) out += "; ";
            out += buf;
        }
        return out.empty() ? std::string("<no OpenSSL error>") : out;
    }

    // ---- SSH wire reader: length(uint32 big-endian) prefixed strings ----
    struct ssh_reader {
        const unsigned char* cur;
        const unsigned char* end;
        explicit ssh_reader(bytes const& b): cur(b.data()), end(b.data()+b.size()) {}

        bool u32(uint32_t& out) {
            if( end-cur < 4 ) return false;
            out = (uint32_t(cur[0])<<24)|(uint32_t(cur[1])<<16)|(uint32_t(cur[2])<<8)|uint32_t(cur[3]);
            cur += 4;
            return true;
        }
        bool str(bytes& out) {
            uint32_t len;
            if( !u32(len) ) return false;
            if( static_cast<uint64_t>(end-cur) < len ) return false;
            out.assign(cur, cur+len);
            cur += len;
            return true;
        }
        bool str(std::string& out) {
            bytes tmp;
            if( !str(tmp) ) return false;
            out.assign(tmp.begin(), tmp.end());
            return true;
        }
    };

    // ---- SSH wire writer ----
    void put_u32(bytes& b, uint32_t v) {
        b.push_back(static_cast<unsigned char>((v>>24)&0xff));
        b.push_back(static_cast<unsigned char>((v>>16)&0xff));
        b.push_back(static_cast<unsigned char>((v>>8)&0xff));
        b.push_back(static_cast<unsigned char>(v&0xff));
    }
    void put_string(bytes& b, const unsigned char* p, size_t n) {
        put_u32(b, static_cast<uint32_t>(n));
        b.insert(b.end(), p, p+n);
    }
    void put_string(bytes& b, std::string const& s) {
        put_string(b, reinterpret_cast<const unsigned char*>(s.data()), s.size());
    }
    void put_string(bytes& b, bytes const& s) {
        put_string(b, s.data(), s.size());
    }
    // SSH mpint: big-endian magnitude, minimised, with a leading 0x00 when
    // the top bit of the first byte is set (to keep it non-negative).
    void put_mpint(bytes& b, bytes raw) {
        size_t i = 0;
        while( i+1<raw.size() && raw[i]==0 ) i++;
        raw.erase(raw.begin(), raw.begin()+i);
        if( raw.empty() || (raw.size()==1 && raw[0]==0) ) { put_u32(b, 0); return; }
        const bool pad = (raw[0]&0x80)!=0;
        put_u32(b, static_cast<uint32_t>(raw.size()+(pad?1u:0u)));
        if( pad ) b.push_back(0x00);
        b.insert(b.end(), raw.begin(), raw.end());
    }

    // ---- ssh public-key blob -> EVP_PKEY (no deprecated *_set0 needed for
    //      ed25519; rsa/ec use the 1.1.1 low-level API under the pragma) ----
    pkey_ptr pubkey_from_blob(bytes const& blob) {
        ssh_reader  r(blob);
        std::string algo;
        if( !r.str(algo) ) return pkey_ptr();

        if( algo=="ssh-ed25519" ) {
            bytes key;
            if( !r.str(key) || key.size()!=32 ) return pkey_ptr();
            EVP_PKEY* pk = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, key.data(), key.size());
            return pk ? pkey_ptr(pk, &EVP_PKEY_free) : pkey_ptr();
        }
        if( algo=="ssh-rsa" ) {
            bytes e, n;
            if( !r.str(e) || !r.str(n) ) return pkey_ptr();
            BIGNUM* be = BN_bin2bn(e.data(), static_cast<int>(e.size()), nullptr);
            BIGNUM* bn = BN_bin2bn(n.data(), static_cast<int>(n.size()), nullptr);
            RSA*    rsa = RSA_new();
            if( !be || !bn || !rsa || RSA_set0_key(rsa, bn, be, nullptr)!=1 ) {
                BN_free(be); BN_free(bn); RSA_free(rsa); return pkey_ptr();
            }
            // rsa now owns be,bn
            EVP_PKEY* pk = EVP_PKEY_new();
            if( !pk || EVP_PKEY_assign_RSA(pk, rsa)!=1 ) { EVP_PKEY_free(pk); RSA_free(rsa); return pkey_ptr(); }
            return pkey_ptr(pk, &EVP_PKEY_free);
        }
        if( algo=="ecdsa-sha2-nistp256" ) {
            std::string curve; bytes point;
            if( !r.str(curve) || curve!="nistp256" || !r.str(point) ) return pkey_ptr();
            EC_KEY* ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
            if( !ec ) return pkey_ptr();
            const EC_GROUP* grp = EC_KEY_get0_group(ec);
            EC_POINT*       pt  = EC_POINT_new(grp);
            if( !pt || EC_POINT_oct2point(grp, pt, point.data(), point.size(), nullptr)!=1 ||
                EC_KEY_set_public_key(ec, pt)!=1 ) {
                EC_POINT_free(pt); EC_KEY_free(ec); return pkey_ptr();
            }
            EC_POINT_free(pt);
            EVP_PKEY* pk = EVP_PKEY_new();
            if( !pk || EVP_PKEY_assign_EC_KEY(pk, ec)!=1 ) { EVP_PKEY_free(pk); EC_KEY_free(ec); return pkey_ptr(); }
            return pkey_ptr(pk, &EVP_PKEY_free);
        }
        return pkey_ptr();
    }

    // signature-algorithm -> message digest. ed25519 uses md==nullptr (the
    // EVP one-shot computes over the whole message). 'ssh-rsa' (SHA-1) is
    // deliberately *not* accepted. Returns false for anything unknown.
    bool md_for(std::string const& algo, const EVP_MD*& md, bool& is_ecdsa, bool& is_ed) {
        md = nullptr; is_ecdsa = false; is_ed = false;
        if( algo=="ssh-ed25519" )            { is_ed = true;  return true; }
        if( algo=="rsa-sha2-256" )           { md = EVP_sha256(); return true; }
        if( algo=="rsa-sha2-512" )           { md = EVP_sha512(); return true; }
        if( algo=="ecdsa-sha2-nistp256" )    { md = EVP_sha256(); is_ecdsa = true; return true; }
        return false;
    }

    // ---- OpenSSH-native private key support ------------------------------
    //
    // Layout of a "BEGIN OPENSSH PRIVATE KEY" container (after un-base64):
    //   "openssh-key-v1\0"
    //   string ciphername          ("none" | "aes256-ctr" | ...)
    //   string kdfname             ("none" | "bcrypt")
    //   string kdfoptions          (bcrypt: string salt || uint32 rounds)
    //   uint32 nkeys
    //   string publickey[nkeys]
    //   string encrypted           (the - possibly encrypted - private section)
    // The decrypted private section is:
    //   uint32 checkint1, uint32 checkint2   (must be equal)
    //   per key: <inline type-specific private fields> string comment
    //   byte   padding 1,2,3,...

    // Read a passphrase from the controlling terminal with echo disabled.
    std::string read_passphrase(std::string const& prompt) {
        FILE* tty = std::fopen("/dev/tty", "r+");
        FILE* in  = tty ? tty : stdin;
        FILE* out = tty ? tty : stderr;
        const int fd = ::fileno(in);

        std::fputs(prompt.c_str(), out); std::fflush(out);

        struct termios oldt, newt;
        const bool haveTermios = (::tcgetattr(fd, &oldt)==0);
        if( haveTermios ) { newt = oldt; newt.c_lflag &= ~static_cast<tcflag_t>(ECHO); ::tcsetattr(fd, TCSAFLUSH, &newt); }

        std::string pass;
        int c;
        while( (c=std::fgetc(in))!=EOF && c!='\n' && c!='\r' )
            pass.push_back(static_cast<char>(c));

        if( haveTermios ) ::tcsetattr(fd, TCSAFLUSH, &oldt);
        std::fputs("\n", out); std::fflush(out);
        if( tty ) std::fclose(tty);
        return pass;
    }

    // OpenSSL PEM password callback shim: forwards to a passphrase_cb passed
    // via 'u', or to the terminal prompt when none was supplied.
    extern "C" {
        static int etdc_pem_pw_cb(char* buf, int size, int /*rwflag*/, void* u) {
            const passphrase_cb* ask = static_cast<const passphrase_cb*>(u);
            const std::string    pass = (ask && *ask) ? (*ask)("Enter passphrase for key: ")
                                                      : read_passphrase("Enter passphrase for key: ");
            const int n = static_cast<int>(std::min(pass.size(), static_cast<size_t>(size<0?0:size)));
            std::memcpy(buf, pass.data(), static_cast<size_t>(n));
            return n;
        }
    }

    bool lookup_cipher(std::string const& name, int& keylen, int& ivlen,
                       int& blocksize, const EVP_CIPHER*& cipher) {
        cipher = nullptr;
        if( name=="none"       ) { keylen=0;  ivlen=0;  blocksize=8;  return true; }
        if( name=="aes256-ctr" ) { keylen=32; ivlen=16; blocksize=16; cipher=EVP_aes_256_ctr(); return true; }
        if( name=="aes256-cbc" ) { keylen=32; ivlen=16; blocksize=16; cipher=EVP_aes_256_cbc(); return true; }
        if( name=="aes192-ctr" ) { keylen=24; ivlen=16; blocksize=16; cipher=EVP_aes_192_ctr(); return true; }
        if( name=="aes192-cbc" ) { keylen=24; ivlen=16; blocksize=16; cipher=EVP_aes_192_cbc(); return true; }
        if( name=="aes128-ctr" ) { keylen=16; ivlen=16; blocksize=16; cipher=EVP_aes_128_ctr(); return true; }
        if( name=="aes128-cbc" ) { keylen=16; ivlen=16; blocksize=16; cipher=EVP_aes_128_cbc(); return true; }
        return false;
    }

    // Reconstruct an EVP_PKEY from the inline private-key fields at the
    // reader's current position. Returns null on unsupported/malformed input.
    pkey_ptr openssh_priv_to_pkey(ssh_reader& r) {
        std::string algo;
        if( !r.str(algo) ) return pkey_ptr();

        if( algo=="ssh-ed25519" ) {
            bytes pub, priv;
            // priv is seed(32) || pub(32); the raw ed25519 private key is the seed.
            if( !r.str(pub) || !r.str(priv) || priv.size()!=64 ) return pkey_ptr();
            EVP_PKEY* pk = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, priv.data(), 32);
            return pk ? pkey_ptr(pk, &EVP_PKEY_free) : pkey_ptr();
        }
        if( algo=="ssh-rsa" ) {
            bytes nb, eb, db, iqmpb, pb, qb;
            // n,e,d are sufficient for signing; p,q,iqmp (CRT) are not needed.
            if( !r.str(nb) || !r.str(eb) || !r.str(db) ||
                !r.str(iqmpb) || !r.str(pb) || !r.str(qb) ) return pkey_ptr();
            BIGNUM* n = BN_bin2bn(nb.data(), static_cast<int>(nb.size()), nullptr);
            BIGNUM* e = BN_bin2bn(eb.data(), static_cast<int>(eb.size()), nullptr);
            BIGNUM* d = BN_bin2bn(db.data(), static_cast<int>(db.size()), nullptr);
            RSA*    rsa = RSA_new();
            if( !n || !e || !d || !rsa || RSA_set0_key(rsa, n, e, d)!=1 ) {
                BN_free(n); BN_free(e); BN_free(d); RSA_free(rsa); return pkey_ptr();
            }
            EVP_PKEY* pk = EVP_PKEY_new();
            if( !pk || EVP_PKEY_assign_RSA(pk, rsa)!=1 ) { EVP_PKEY_free(pk); RSA_free(rsa); return pkey_ptr(); }
            return pkey_ptr(pk, &EVP_PKEY_free);
        }
        if( algo=="ecdsa-sha2-nistp256" ) {
            std::string curve; bytes point, db;
            if( !r.str(curve) || curve!="nistp256" || !r.str(point) || !r.str(db) ) return pkey_ptr();
            EC_KEY* ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
            if( !ec ) return pkey_ptr();
            BIGNUM*         d   = BN_bin2bn(db.data(), static_cast<int>(db.size()), nullptr);
            const EC_GROUP* grp = EC_KEY_get0_group(ec);
            EC_POINT*       pt  = EC_POINT_new(grp);
            const bool      ok  = d && pt &&
                                  EC_POINT_oct2point(grp, pt, point.data(), point.size(), nullptr)==1 &&
                                  EC_KEY_set_public_key(ec, pt)==1 &&
                                  EC_KEY_set_private_key(ec, d)==1;   // copies d
            EC_POINT_free(pt);
            BN_free(d);
            if( !ok ) { EC_KEY_free(ec); return pkey_ptr(); }
            EVP_PKEY* pk = EVP_PKEY_new();
            if( !pk || EVP_PKEY_assign_EC_KEY(pk, ec)!=1 ) { EVP_PKEY_free(pk); EC_KEY_free(ec); return pkey_ptr(); }
            return pkey_ptr(pk, &EVP_PKEY_free);
        }
        return pkey_ptr();
    }

    pkey_ptr parse_openssh_private(bytes const& raw, passphrase_cb const& ask) {
        static const char MAGIC[] = "openssh-key-v1";   // 14 chars + NUL = 15 bytes
        if( raw.size()<sizeof(MAGIC) || std::memcmp(raw.data(), MAGIC, sizeof(MAGIC))!=0 )
            throw std::runtime_error("openssh key: bad magic");

        ssh_reader r(raw);
        r.cur += sizeof(MAGIC);

        std::string ciphername, kdfname;
        bytes       kdfoptions;
        uint32_t    nkeys = 0;
        if( !r.str(ciphername) || !r.str(kdfname) || !r.str(kdfoptions) || !r.u32(nkeys) || nkeys<1 )
            throw std::runtime_error("openssh key: truncated header");
        for(uint32_t i=0; i<nkeys; i++) { bytes pub; if( !r.str(pub) ) throw std::runtime_error("openssh key: truncated public key"); }
        bytes enc;
        if( !r.str(enc) ) throw std::runtime_error("openssh key: truncated private section");

        int              keylen=0, ivlen=0, blocksize=0;
        const EVP_CIPHER* cipher = nullptr;
        if( !lookup_cipher(ciphername, keylen, ivlen, blocksize, cipher) )
            throw std::runtime_error("openssh key: unsupported cipher '"+ciphername+"'");

        bytes plain;
        if( cipher==nullptr ) {
            if( kdfname!="none" ) throw std::runtime_error("openssh key: cipher 'none' with kdf '"+kdfname+"'");
            plain = enc;
        } else {
            if( kdfname!="bcrypt" ) throw std::runtime_error("openssh key: unsupported kdf '"+kdfname+"'");
            ssh_reader kr(kdfoptions);
            bytes      salt;
            uint32_t   rounds = 0;
            if( !kr.str(salt) || !kr.u32(rounds) || salt.empty() || rounds<1 )
                throw std::runtime_error("openssh key: bad bcrypt parameters");
            const std::string pass = (ask ? ask("Enter passphrase for key: ")
                                          : read_passphrase("Enter passphrase for key: "));
            if( pass.empty() )
                throw std::runtime_error("openssh key is encrypted but no passphrase was supplied");

            bytes keyiv(static_cast<size_t>(keylen+ivlen));
            if( bcrypt_pbkdf(reinterpret_cast<const uint8_t*>(pass.data()), pass.size(),
                             salt.data(), salt.size(), keyiv.data(), keyiv.size(), rounds)!=0 )
                throw std::runtime_error("openssh key: bcrypt_pbkdf failed");
            if( enc.empty() || (enc.size()%static_cast<size_t>(blocksize))!=0 )
                throw std::runtime_error("openssh key: bad encrypted length");

            plain.resize(enc.size());
            EVP_CIPHER_CTX* cc = EVP_CIPHER_CTX_new();
            if( !cc ) throw std::runtime_error("EVP_CIPHER_CTX_new - "+ssl_err());
            int  outl=0, total=0; bool ok=false;
            do {
                if( EVP_DecryptInit_ex(cc, cipher, nullptr, keyiv.data(), keyiv.data()+keylen)!=1 ) break;
                EVP_CIPHER_CTX_set_padding(cc, 0);   // OpenSSH pads to blocksize itself
                if( EVP_DecryptUpdate(cc, plain.data(), &outl, enc.data(), static_cast<int>(enc.size()))!=1 ) break;
                total = outl;
                if( EVP_DecryptFinal_ex(cc, plain.data()+total, &outl)!=1 ) break;
                total += outl; ok = true;
            } while(false);
            EVP_CIPHER_CTX_free(cc);
            if( !ok ) { ERR_clear_error(); throw std::runtime_error("openssh key: decryption failed"); }
            plain.resize(static_cast<size_t>(total));
        }

        ssh_reader pr(plain);
        uint32_t   c1=0, c2=0;
        if( !pr.u32(c1) || !pr.u32(c2) ) throw std::runtime_error("openssh key: truncated private body");
        if( c1!=c2 ) throw std::runtime_error("openssh key: incorrect passphrase or corrupt key");
        pkey_ptr pk = openssh_priv_to_pkey(pr);
        if( !pk ) throw std::runtime_error("openssh key: unsupported or malformed private key");
        return pk;
    }

    // If 'data' is an OpenSSH-native PEM container, base64-decode its body
    // into 'out' and return true. Returns false if it is not such a container.
    bool extract_openssh_b64(std::string const& data, bytes& out) {
        static const std::string B = "-----BEGIN OPENSSH PRIVATE KEY-----";
        static const std::string E = "-----END OPENSSH PRIVATE KEY-----";
        const size_t b = data.find(B);
        if( b==std::string::npos ) return false;
        const size_t s = b + B.size();
        const size_t e = data.find(E, s);
        if( e==std::string::npos ) throw std::runtime_error("openssh key: missing END marker");
        out = base64_decode(data.substr(s, e-s));   // base64_decode skips whitespace/newlines
        return true;
    }
} // anonymous namespace

    // ---- base64 (standard alphabet) -------------------------------------
    static const char b64_alpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    bytes base64_decode(std::string const& s) {
        int rev[256];
        for(int i=0; i<256; i++) rev[i] = -1;
        for(int i=0; i<64; i++)  rev[static_cast<unsigned char>(b64_alpha[i])] = i;

        bytes out;
        int   val = 0, bits = 0;
        for(char c: s) {
            if( c=='=' ) break;
            if( c=='\n' || c=='\r' || c==' ' || c=='\t' ) continue;
            const int d = rev[static_cast<unsigned char>(c)];
            if( d<0 ) throw std::runtime_error("invalid base64 input");
            val = (val<<6)|d; bits += 6;
            if( bits>=8 ) { bits -= 8; out.push_back(static_cast<unsigned char>((val>>bits)&0xff)); }
        }
        return out;
    }

    std::string base64_encode(bytes const& b) {
        std::string out;
        size_t      i = 0;
        for( ; i+3<=b.size(); i+=3 ) {
            const unsigned v = (unsigned(b[i])<<16)|(unsigned(b[i+1])<<8)|unsigned(b[i+2]);
            out += b64_alpha[(v>>18)&0x3f]; out += b64_alpha[(v>>12)&0x3f];
            out += b64_alpha[(v>>6)&0x3f];  out += b64_alpha[v&0x3f];
        }
        const size_t rem = b.size()-i;
        if( rem==1 ) {
            const unsigned v = unsigned(b[i])<<16;
            out += b64_alpha[(v>>18)&0x3f]; out += b64_alpha[(v>>12)&0x3f]; out += "==";
        } else if( rem==2 ) {
            const unsigned v = (unsigned(b[i])<<16)|(unsigned(b[i+1])<<8);
            out += b64_alpha[(v>>18)&0x3f]; out += b64_alpha[(v>>12)&0x3f];
            out += b64_alpha[(v>>6)&0x3f];  out += "=";
        }
        return out;
    }

    // ---- verification ---------------------------------------------------
    bool verify(bytes const& signed_data, std::string const& sig_algo,
                bytes const& pubkey_blob, bytes const& sig_blob) {
        ssh_reader  sr(sig_blob);
        std::string blob_algo;
        bytes       rawsig;
        if( !sr.str(blob_algo) || !sr.str(rawsig) || blob_algo!=sig_algo )
            return false;

        const EVP_MD* md = nullptr;
        bool          is_ecdsa = false, is_ed = false;
        if( !md_for(sig_algo, md, is_ecdsa, is_ed) )
            return false;

        pkey_ptr pk = pubkey_from_blob(pubkey_blob);
        if( !pk ) return false;

        // The key type must match the signature algorithm family.
        const int id = EVP_PKEY_base_id(pk.get());
        if( (is_ed    && id!=EVP_PKEY_ED25519) ||
            (is_ecdsa && id!=EVP_PKEY_EC) ||
            (!is_ed && !is_ecdsa && id!=EVP_PKEY_RSA) )
            return false;

        // ECDSA: ssh packs the signature as mpint(r) || mpint(s); libcrypto
        // wants a DER ECDSA_SIG. Convert.
        bytes dersig;
        const unsigned char* sigp = rawsig.data();
        size_t               siglen = rawsig.size();
        if( is_ecdsa ) {
            ssh_reader er(rawsig);
            bytes rb, sb;
            if( !er.str(rb) || !er.str(sb) ) return false;
            BIGNUM* r = BN_bin2bn(rb.data(), static_cast<int>(rb.size()), nullptr);
            BIGNUM* s = BN_bin2bn(sb.data(), static_cast<int>(sb.size()), nullptr);
            ECDSA_SIG* es = ECDSA_SIG_new();
            if( !r || !s || !es || ECDSA_SIG_set0(es, r, s)!=1 ) {
                BN_free(r); BN_free(s); ECDSA_SIG_free(es); return false;
            }
            const int dl = i2d_ECDSA_SIG(es, nullptr);
            if( dl<=0 ) { ECDSA_SIG_free(es); return false; }
            dersig.resize(static_cast<size_t>(dl));
            unsigned char* dp = dersig.data();
            i2d_ECDSA_SIG(es, &dp);
            ECDSA_SIG_free(es);
            sigp = dersig.data(); siglen = dersig.size();
        }

        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        bool        ok = false;
        if( ctx && EVP_DigestVerifyInit(ctx, nullptr, md, nullptr, pk.get())==1 )
            ok = (EVP_DigestVerify(ctx, sigp, siglen, signed_data.data(), signed_data.size())==1);
        EVP_MD_CTX_free(ctx);
        ERR_clear_error();
        return ok;
    }

    // ---- authorized_keys ------------------------------------------------
    std::vector<authkey> authorized_keys_for(std::string const& dir, std::string const& principal) {
        std::vector<authkey> result;
        if( dir.empty() || principal.empty() || principal.find('/')!=std::string::npos )
            return result;

        std::string path( dir );
        if( path.back()!='/' ) path += '/';
        path += principal;

        std::ifstream f( path.c_str() );
        if( !f ) return result;

        std::string line;
        while( std::getline(f, line) ) {
            const size_t a = line.find_first_not_of(" \t\r\n");
            if( a==std::string::npos || line[a]=='#' ) continue;

            std::istringstream iss( line.substr(a) );
            std::string        algo, b64, comment;
            if( !(iss>>algo>>b64) ) continue;
            if( algo!="ssh-ed25519" && algo!="ssh-rsa" && algo!="ecdsa-sha2-nistp256" ) continue;
            std::getline(iss, comment);
            const size_t c = comment.find_first_not_of(" \t");
            comment = (c==std::string::npos) ? std::string() : comment.substr(c);

            authkey k;
            try { k.blob = base64_decode(b64); } catch(...) { continue; }
            // blob's embedded algo must match the line's key type
            ssh_reader  rdr(k.blob);
            std::string ba;
            if( !rdr.str(ba) || ba!=algo ) continue;
            k.key_algo = algo;
            k.comment  = comment;
            result.push_back(std::move(k));
        }
        return result;
    }

    bool authenticate(std::string const& dir, std::string const& principal,
                      bytes const& signed_data, std::string const& sig_algo,
                      bytes const& pubkey_blob, bytes const& sig_blob,
                      std::string& out_comment) {
        const std::vector<authkey> keys = authorized_keys_for(dir, principal);
        for(authkey const& k: keys) {
            if( k.blob.size()==pubkey_blob.size() &&
                std::equal(k.blob.begin(), k.blob.end(), pubkey_blob.begin()) ) {
                if( verify(signed_data, sig_algo, pubkey_blob, sig_blob) ) {
                    out_comment = k.comment;
                    return true;
                }
                return false; // presented key matched but its signature failed
            }
        }
        return false;
    }

    // ---- signing (client / test) ----------------------------------------
    pkey_ptr load_private_key(std::string const& path, passphrase_cb ask) {
        std::ifstream f(path.c_str(), std::ios::binary);
        if( !f ) throw std::runtime_error("cannot open identity file '"+path+"'");
        std::ostringstream ss; ss << f.rdbuf();
        const std::string data = ss.str();

        // Native OpenSSH container (incl. ed25519 and encrypted keys)?
        bytes osshblob;
        if( extract_openssh_b64(data, osshblob) )
            return parse_openssh_private(osshblob, ask);

        // Otherwise PEM / PKCS#8 (possibly encrypted - the callback prompts).
        BIO* bio = BIO_new_mem_buf(data.data(), static_cast<int>(data.size()));
        if( !bio ) throw std::runtime_error("BIO_new_mem_buf - "+ssl_err());
        EVP_PKEY* pk = PEM_read_bio_PrivateKey(bio, nullptr, &etdc_pem_pw_cb, &ask);
        BIO_free(bio);
        if( !pk )
            throw std::runtime_error("failed to load private key from '"+path+
                                     "' (not a supported PEM/PKCS#8 or OpenSSH key, or wrong passphrase) - "+ssl_err());
        return pkey_ptr(pk, &EVP_PKEY_free);
    }

    std::string default_sig_algo(EVP_PKEY* pkey) {
        switch( EVP_PKEY_base_id(pkey) ) {
            case EVP_PKEY_ED25519: return "ssh-ed25519";
            case EVP_PKEY_RSA:     return "rsa-sha2-256";
            case EVP_PKEY_EC:      return "ecdsa-sha2-nistp256";
            default: throw std::runtime_error("unsupported key type for ssh signing");
        }
    }

    bytes public_key_blob(EVP_PKEY* pkey, std::string& out_key_algo) {
        bytes blob;
        switch( EVP_PKEY_base_id(pkey) ) {
            case EVP_PKEY_ED25519: {
                size_t len = 0;
                if( EVP_PKEY_get_raw_public_key(pkey, nullptr, &len)!=1 )
                    throw std::runtime_error("ed25519 raw public key - "+ssl_err());
                bytes key(len);
                if( EVP_PKEY_get_raw_public_key(pkey, key.data(), &len)!=1 )
                    throw std::runtime_error("ed25519 raw public key - "+ssl_err());
                key.resize(len);
                out_key_algo = "ssh-ed25519";
                put_string(blob, std::string("ssh-ed25519"));
                put_string(blob, key);
                return blob;
            }
            case EVP_PKEY_RSA: {
                const RSA* rsa = EVP_PKEY_get0_RSA(pkey);
                const BIGNUM *n = nullptr, *e = nullptr;
                if( !rsa ) throw std::runtime_error("no RSA key");
                RSA_get0_key(rsa, &n, &e, nullptr);
                bytes nb(static_cast<size_t>(BN_num_bytes(n))), eb(static_cast<size_t>(BN_num_bytes(e)));
                BN_bn2bin(n, nb.data()); BN_bn2bin(e, eb.data());
                out_key_algo = "ssh-rsa";
                put_string(blob, std::string("ssh-rsa"));
                put_mpint(blob, eb);
                put_mpint(blob, nb);
                return blob;
            }
            case EVP_PKEY_EC: {
                const EC_KEY* ec = EVP_PKEY_get0_EC_KEY(pkey);
                if( !ec ) throw std::runtime_error("no EC key");
                const EC_GROUP* grp = EC_KEY_get0_group(ec);
                const EC_POINT* pt  = EC_KEY_get0_public_key(ec);
                if( EC_GROUP_get_curve_name(grp)!=NID_X9_62_prime256v1 )
                    throw std::runtime_error("unsupported EC curve (only nistp256)");
                const size_t plen = EC_POINT_point2oct(grp, pt, POINT_CONVERSION_UNCOMPRESSED, nullptr, 0, nullptr);
                if( plen==0 ) throw std::runtime_error("EC point encode - "+ssl_err());
                bytes point(plen);
                EC_POINT_point2oct(grp, pt, POINT_CONVERSION_UNCOMPRESSED, point.data(), plen, nullptr);
                out_key_algo = "ecdsa-sha2-nistp256";
                put_string(blob, std::string("ecdsa-sha2-nistp256"));
                put_string(blob, std::string("nistp256"));
                put_string(blob, point);
                return blob;
            }
            default:
                throw std::runtime_error("unsupported key type for ssh public key blob");
        }
    }

    bytes sign(EVP_PKEY* pkey, std::string const& sig_algo, bytes const& data) {
        const EVP_MD* md = nullptr;
        bool          is_ecdsa = false, is_ed = false;
        if( !md_for(sig_algo, md, is_ecdsa, is_ed) )
            throw std::runtime_error("unsupported signature algorithm: "+sig_algo);

        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if( !ctx ) throw std::runtime_error("EVP_MD_CTX_new - "+ssl_err());

        bytes rawsig;
        try {
            if( EVP_DigestSignInit(ctx, nullptr, md, nullptr, pkey)!=1 )
                throw std::runtime_error("DigestSignInit - "+ssl_err());
            size_t slen = 0;
            if( EVP_DigestSign(ctx, nullptr, &slen, data.data(), data.size())!=1 )
                throw std::runtime_error("DigestSign(len) - "+ssl_err());
            rawsig.resize(slen);
            if( EVP_DigestSign(ctx, rawsig.data(), &slen, data.data(), data.size())!=1 )
                throw std::runtime_error("DigestSign - "+ssl_err());
            rawsig.resize(slen);
        } catch(...) { EVP_MD_CTX_free(ctx); throw; }
        EVP_MD_CTX_free(ctx);

        bytes blob;
        put_string(blob, sig_algo);
        if( is_ecdsa ) {
            const unsigned char* p  = rawsig.data();
            ECDSA_SIG*           es = d2i_ECDSA_SIG(nullptr, &p, static_cast<long>(rawsig.size()));
            if( !es ) throw std::runtime_error("d2i_ECDSA_SIG - "+ssl_err());
            const BIGNUM *r = nullptr, *s = nullptr;
            ECDSA_SIG_get0(es, &r, &s);
            bytes inner, rb(static_cast<size_t>(BN_num_bytes(r))), sb(static_cast<size_t>(BN_num_bytes(s)));
            BN_bn2bin(r, rb.data()); BN_bn2bin(s, sb.data());
            put_mpint(inner, rb);
            put_mpint(inner, sb);
            ECDSA_SIG_free(es);
            put_string(blob, inner);
        } else {
            put_string(blob, rawsig);
        }
        return blob;
    }

    // ---- channel binding + signer abstraction ---------------------------
    bytes auth_channel_binding(exporter_fn const& exporter, std::string const& principal) {
        // Single source of truth for the exporter parameters; the daemon's
        // verify path derives the same bytes the same way.
        if( !exporter )
            throw std::runtime_error("no TLS channel binding available (cleartext connection?)");
        return exporter("EXPORTER-etransfer-auth-v1", principal, 32);
    }

    signer_fn make_identity_signer(std::string const& path, passphrase_cb ask) {
        // Load (and, if encrypted, decrypt) the key now so a bad path / wrong
        // passphrase fails fast - before any TLS handshake or negotiation.
        // The returned closure keeps the key alive via the shared_ptr.
        pkey_ptr          key     = load_private_key(path, ask);
        std::string       keyAlgo;                                  // unused out-param
        const bytes       pub     = public_key_blob(key.get(), keyAlgo);
        const std::string sigAlgo = default_sig_algo(key.get());

        return signer_fn([key, pub, sigAlgo](std::string const& principal,
                                             exporter_fn const& exporter) -> auth_material {
            auth_material mat;
            mat.sig_algo    = sigAlgo;
            mat.pubkey_blob = pub;
            mat.sig_blob    = sign(key.get(), sigAlgo, auth_channel_binding(exporter, principal));
            return mat;
        });
    }

}} // namespace etdc::auth

#pragma GCC diagnostic pop

#endif // ETDC_TLS
