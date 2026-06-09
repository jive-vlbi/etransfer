// Unit test for the data-channel transport preference logic in
// etdc_channel_pref.h: token parsing, scheme ranking and the tier-stable
// reorder it is designed to drive (std::list::sort in etc.cc; mirrored
// here with std::stable_sort over a vector of scheme names).
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

#include <etdc_channel_pref.h>
#include <etdc_assert.h>

namespace {
    // Mirror of the production reorder in etc.cc: key each scheme on its
    // preference rank and stable-sort. std::stable_sort here matches the
    // guaranteed-stable std::list::sort used on the live dataaddrlist_type.
    std::vector<std::string> reorder(std::vector<std::string> schemes, std::string const& spec) {
        auto const prefs = etdc::parse_channel_preference(spec);
        std::stable_sort(schemes.begin(), schemes.end(),
                         [&](std::string const& a, std::string const& b) {
                            return etdc::preference_rank(prefs, a) < etdc::preference_rank(prefs, b);
                         });
        return schemes;
    }

    std::string join(std::vector<std::string> const& v) {
        std::string  rv;
        for(auto const& s : v)
            rv += (rv.empty() ? "" : ",") + s;
        return rv;
    }

    struct reorder_case {
        std::string              spec;
        std::vector<std::string> offered;
        std::vector<std::string> expect;
        std::string              why;
    };

    bool run_reorder_cases(std::vector<reorder_case> const& cases) {
        bool all_ok = true;
        for(auto const& tc : cases) {
            auto const got = reorder(tc.offered, tc.spec);
            if( got!=tc.expect ) {
                std::cout << "FAIL: --prefer-data-channel " << tc.spec
                          << " on [" << join(tc.offered) << "]" << std::endl
                          << "        expected [" << join(tc.expect) << "]" << std::endl
                          << "        got      [" << join(got) << "]" << std::endl
                          << "        (" << tc.why << ")" << std::endl;
                all_ok = false;
            } else {
                std::cout << "  " << tc.spec << " : [" << join(tc.offered)
                          << "] -> [" << join(got) << "]" << std::endl;
            }
        }
        return all_ok;
    }

    bool expect_reject(std::string const& spec) {
        try {
            (void)etdc::parse_channel_preference(spec);
        } catch(etdc::assertion_error const& e) {
            std::cout << "  rejected '" << spec << "': " << e.what() << std::endl;
            return true;
        }
        std::cout << "FAIL: preference '" << spec << "' was accepted but should have been rejected" << std::endl;
        return false;
    }
}

int main() {
    bool ok = true;

    // The canonical set a fully-featured (v3+) daemon might advertise, in
    // some sysadmin-chosen order.
    const std::vector<std::string> full{ "tcp", "tcp6", "udt", "udt6", "srt", "srt6" };

    std::cout << "Evaluating reorder behaviour:" << std::endl;
    ok &= run_reorder_cases({
        // Bare 'srt' lifts both SRT schemes to the front but keeps the
        // daemon's srt-before-srt6 order between them (tier stability).
        { "srt", full, { "srt", "srt6", "tcp", "tcp6", "udt", "udt6" },
          "bare protocol = both families, daemon order preserved within tier" },

        // Fine-grained ordering with explicit families; unlisted schemes
        // (tcp,tcp6,srt) fall to the back in their original relative order.
        { "udt6,udt4,srt6", full, { "udt6", "udt", "srt6", "tcp", "tcp6", "srt" },
          "explicit family suffixes; udt4 == bare udt scheme" },

        // Bare 'udt' with the IPv6 scheme advertised first: must NOT force
        // v4-before-v6 - the daemon's udt6-before-udt order wins.
        { "udt", { "udt6", "tcp", "udt" }, { "udt6", "udt", "tcp" },
          "bare token does not impose a family order" },

        // Pre-SRT daemon: preferring srt matches nothing offered, so the
        // list is left exactly as advertised (safe no-op).
        { "srt", { "tcp", "tcp6", "udt", "udt6" }, { "tcp", "tcp6", "udt", "udt6" },
          "no-op when preferred transport is not offered" },

        // tcp6 only: single scheme to the front, rest keep order.
        { "tcp6", full, { "tcp6", "tcp", "udt", "udt6", "srt", "srt6" },
          "single explicit IPv6 scheme to the front" },
    });

    std::cout << "Evaluating token parsing:" << std::endl;
    {
        auto const p = etdc::parse_channel_preference("udt6,udt4,srt");
        const bool shape_ok =
            p.size()==3 &&
            p[0].protocol=="udt" && p[0].family==etdc::pref_family::v6 &&
            p[1].protocol=="udt" && p[1].family==etdc::pref_family::v4 &&
            p[2].protocol=="srt" && p[2].family==etdc::pref_family::any;
        if( shape_ok ) {
            std::cout << "  parsed 'udt6,udt4,srt' into 3 tokens with expected (proto,family)" << std::endl;
        } else {
            std::cout << "FAIL: 'udt6,udt4,srt' parsed into an unexpected token shape" << std::endl;
            ok = false;
        }
    }

    std::cout << "Evaluating rejection of malformed preferences:" << std::endl;
    ok &= expect_reject("");          // empty spec
    ok &= expect_reject("srtt");      // unknown protocol
    ok &= expect_reject("foo");       // unknown protocol
    ok &= expect_reject("udt5");      // bad family suffix
    ok &= expect_reject("4");         // suffix without protocol
    ok &= expect_reject("srt,");      // trailing comma -> empty token
    ok &= expect_reject(",srt");      // leading comma -> empty token
    ok &= expect_reject("udt,,srt");  // embedded empty token

    if( ok ) {
        std::cout << "OK" << std::endl;
        return 0;
    }
    std::cout << "NOT OK" << std::endl;
    return 1;
}
