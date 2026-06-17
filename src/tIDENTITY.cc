// End-to-end test for OpenSSH private-key loading (etdc::auth::load_private_key).
//
// It shells out to the system ssh-keygen to produce real ed25519 / rsa / ecdsa
// keys - both unencrypted and passphrase-encrypted (which exercises the
// bcrypt_pbkdf + AES decryption path) - then for each key:
//   * loads it with load_private_key (supplying the passphrase via callback),
//   * derives the SSH public-key blob and checks it equals `ssh-keygen -y`,
//   * signs a message and verifies it with etdc::auth::verify.
//
//   make TLS=1 tIDENTITY && ./tIDENTITY
#include <iostream>

#ifdef ETDC_TLS

#include <etdc_auth.h>

#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
    int nPass = 0, nFail = 0;

    void check(bool ok, std::string const& what) {
        std::cout << (ok ? "  ok  : " : "  FAIL: ") << what << std::endl;
        if( ok ) nPass++; else nFail++;
    }

    // Run a command, return its stdout (stderr is sent to /dev/null).
    std::string run_capture(std::string const& cmd) {
        std::string full = cmd + " 2>/dev/null";
        FILE*       p    = ::popen(full.c_str(), "r");
        std::string out;
        if( !p ) return out;
        char   buf[4096];
        size_t n;
        while( (n=std::fread(buf, 1, sizeof(buf), p))>0 ) out.append(buf, n);
        ::pclose(p);
        return out;
    }

    int run(std::string const& cmd) {
        return ::system((cmd + " >/dev/null 2>&1").c_str());
    }

    // 2nd whitespace-delimited token of an "ssh-keygen -y" line (the base64 blob)
    std::string second_token(std::string const& s) {
        std::istringstream iss(s);
        std::string        a, b;
        iss >> a >> b;
        return b;
    }
} // anonymous namespace

int main( void ) {
    std::cout << "tIDENTITY: OpenSSH private-key load/parse self-test\n" << std::endl;

    if( run("command -v ssh-keygen")!=0 ) {
        std::cout << "tIDENTITY: ssh-keygen not found - SKIPPING" << std::endl;
        return 0;
    }

    // Use the shell's "mktemp -d" rather than libc mkdtemp(): the latter is a
    // BSD extension hidden on macOS by this project's strict _POSIX_C_SOURCE /
    // _XOPEN_SOURCE feature macros. Both macOS and Linux mktemp accept a
    // trailing-XXXXXX template and print the created directory on stdout.
    std::string dir = run_capture("mktemp -d /tmp/etdc_tIDENTITY_XXXXXX");
    while( !dir.empty() && (dir.back()=='\n' || dir.back()=='\r') ) dir.pop_back();
    if( dir.empty() ) { std::cout << "tIDENTITY: cannot create temp dir" << std::endl; return 2; }

    const std::string        passphrase = "test-passphrase-123";
    const etdc::auth::bytes   message    = { 'e','t','r','a','n','s','f','e','r',' ','a','u','t','h',' ','t','e','s','t' };

    struct Case { std::string type, args; };
    const std::vector<Case>   types = {
        { "ed25519", "" },
        { "rsa",     "-b 2048" },
        { "ecdsa",   "-b 256" }
    };

    for(Case const& c: types) {
        for(int enc=0; enc<2; enc++) {
            const bool        encrypted = (enc==1);
            const std::string label     = c.type + (encrypted ? " (encrypted)" : " (plain)");
            const std::string keyfile   = std::string(dir) + "/id_" + c.type + (encrypted ? "_enc" : "");
            const std::string pass      = encrypted ? passphrase : std::string();

            std::cout << "[" << label << "]" << std::endl;

            // generate
            const std::string gen = "ssh-keygen -t " + c.type + " " + c.args +
                                    " -f " + keyfile + " -N '" + pass + "'" +
                                    " -C 'etdc-test-" + c.type + "' -q";
            if( run(gen)!=0 ) { check(false, label+": ssh-keygen generate"); continue; }

            // canonical public-key blob from ssh-keygen
            std::string yp = "ssh-keygen -y -f " + keyfile;
            if( encrypted ) yp += " -P '" + pass + "'";
            const std::string ypout     = run_capture(yp);
            const std::string canonical = second_token(ypout);
            check(!canonical.empty(), label+": ssh-keygen -y produced a blob");

            // load via our code
            etdc::auth::pkey_ptr pkey;
            try {
                pkey = etdc::auth::load_private_key(keyfile,
                            [&](std::string const&){ return pass; });
            } catch(std::exception const& e) {
                check(false, label+std::string(": load_private_key threw: ")+e.what());
                continue;
            }
            check(static_cast<bool>(pkey), label+": load_private_key");
            if( !pkey ) continue;

            // derive blob and compare to ssh-keygen
            try {
                std::string             algo;
                const etdc::auth::bytes blob   = etdc::auth::public_key_blob(pkey.get(), algo);
                const std::string       ourb64 = etdc::auth::base64_encode(blob);
                check(ourb64==canonical, label+": derived public key matches ssh-keygen");

                // sign + verify round-trip
                const std::string       sigalgo = etdc::auth::default_sig_algo(pkey.get());
                const etdc::auth::bytes sig     = etdc::auth::sign(pkey.get(), sigalgo, message);
                check(etdc::auth::verify(message, sigalgo, blob, sig),
                      label+": sign + verify round-trip");

                // negative: a message that was NOT signed must not verify
                // against this signature. A thrown exception (malformed input)
                // counts as a rejection just like a false return.
                {
                    etdc::auth::bytes badMsg(message);
                    badMsg[0] = static_cast<etdc::auth::bytes::value_type>(badMsg[0] ^ 0xff);
                    bool rejected = false;
                    try   { rejected = !etdc::auth::verify(badMsg, sigalgo, blob, sig); }
                    catch(std::exception const&) { rejected = true; }
                    check(rejected, label+": tampered message rejected");
                }
                // negative: a tampered signature must not verify either.
                {
                    etdc::auth::bytes badSig(sig);
                    const size_t      mid = badSig.size()/2;
                    badSig[mid] = static_cast<etdc::auth::bytes::value_type>(badSig[mid] ^ 0xff);
                    bool rejected = false;
                    try   { rejected = !etdc::auth::verify(message, sigalgo, blob, badSig); }
                    catch(std::exception const&) { rejected = true; }
                    check(rejected, label+": tampered signature rejected");
                }

                // signer abstraction + daemon authenticate() round-trip: the
                // full client->daemon path minus the TLS transport. A
                // deterministic stand-in plays the role of the shared TLS
                // keying-material exporter (both ends derive the same bytes).
                {
                    const std::string principal = "tester";
                    auto exporter = [](std::string const& lbl, std::string const& ctx, size_t len) -> etdc::auth::bytes {
                        etdc::auth::bytes out(len);
                        const std::string seed = lbl + "|" + ctx + "|";
                        for(size_t i=0; i<len; i++)
                            out[i] = static_cast<etdc::auth::bytes::value_type>(seed[i % seed.size()] + static_cast<int>(i));
                        return out;
                    };
                    // authorized_keys file = ssh-keygen -y output verbatim.
                    const std::string akf = std::string(dir) + "/" + principal;
                    { std::ofstream of(akf.c_str()); of << ypout << "\n"; }

                    etdc::auth::signer_fn    signer = etdc::auth::make_identity_signer(keyfile,
                            [&](std::string const&){ return pass; });
                    etdc::auth::auth_material mat    = signer(principal, exporter);

                    const etdc::auth::bytes  binding = etdc::auth::auth_channel_binding(exporter, principal);
                    std::string              comment;
                    const bool               ok = etdc::auth::authenticate(dir, principal, binding,
                                                       mat.sig_algo, mat.pubkey_blob, mat.sig_blob, comment);
                    check(ok, label+": signer + authenticate round-trip");

                    // negative: a different principal => different binding, so
                    // the same material must NOT authenticate (channel binding).
                    const etdc::auth::bytes  binding2 = etdc::auth::auth_channel_binding(exporter, "intruder");
                    const bool               ok2 = etdc::auth::authenticate(dir, principal, binding2,
                                                       mat.sig_algo, mat.pubkey_blob, mat.sig_blob, comment);
                    check(!ok2, label+": wrong channel binding rejected");
                }
            } catch(std::exception const& e) {
                check(false, label+std::string(": ")+e.what());
            }
        }
    }

    // best-effort cleanup
    run(std::string("rm -rf ") + dir);

    std::cout << "\ntIDENTITY: " << (nFail==0 ? "ALL TESTS PASSED" : "FAILURES PRESENT")
              << " (" << nPass << " passed, " << nFail << " failed)" << std::endl;
    return nFail==0 ? 0 : 1;
}

#else

int main( void ) {
    std::cout << "tIDENTITY: built without TLS - nothing to test (use 'make TLS=1 tIDENTITY')" << std::endl;
    return 0;
}

#endif
