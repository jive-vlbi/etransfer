#include <iostream>
#include <string>
#include <sstream>
#include <vector>

#include <etd_acl.h>
#include <etdc_assert.h>

namespace {
    struct test_case {
        std::string path;
        bool        expect_read;
        bool        expect_write;
    };

    bool run_acl_check(std::string const& yaml, std::vector<test_case> const& cases) {
        std::istringstream yaml_stream(yaml);
        etdc::ACL acl(yaml_stream);

        bool all_ok = true;
        std::cout << "Evaluating read paths:" << std::endl;
        for(auto const& tc : cases) {
            const bool allowed = acl.allowRead(tc.path);
            if( allowed!=tc.expect_read ) {
                std::cout << "FAIL: path '" << tc.path << "' expected "
                          << (tc.expect_read ? "allow" : "deny")
                          << " but got " << (allowed ? "allow" : "deny") << std::endl;
                all_ok = false;
            } else {
                std::cout << "  path '" << tc.path << "' -> "
                          << (allowed ? "allow" : "deny") << std::endl;
            }
        }
        std::cout << "Evaluating write paths:" << std::endl;
        for(auto const& tc : cases) {
            const bool allowed = acl.allowWrite(tc.path);
            if( allowed!=tc.expect_write ) {
                std::cout << "FAIL: write path '" << tc.path << "' expected "
                          << (tc.expect_write ? "allow" : "deny")
                          << " but got " << (allowed ? "allow" : "deny") << std::endl;
                all_ok = false;
            } else {
                std::cout << "  write path '" << tc.path << "' -> "
                          << (allowed ? "allow" : "deny") << std::endl;
            }
        }
        return all_ok;
    }
}

int main() {
    const std::string acl_yaml =
        "read:\n"
        "  default:\n"
        "    deny: \"/\"\n"
        "  allow:\n"
        "    - \"/data/**\"\n"
        "    - \"/tmp/*/file\"\n"
        "write:\n"
        "  default:\n"
        "    deny: \"/\"\n"
        "  allow:\n"
        "    - \"/data/**\"\n";

    const std::vector<test_case> cases = {
        { "/data", true, true },
        { "/data/test", true, true },
        { "/data/test/gs/foobar.vdif", true, true },
        { "/data/foo/bar/baz/qux", true, true },
        { "/tmp/foo/file", true, false },
        { "/tmp/foo/bar/file", false, false },
        { "/other/file", false, false }
    };

    std::cout << "ACL YAML configuration:\n" << acl_yaml << std::endl;
    bool ok = run_acl_check(acl_yaml, cases);

    try {
        const std::string bad_yaml =
            "read:\n"
            "  default:\n"
            "    deny: \"/\"\n"
            "  allow:\n"
            "    - \"/data/**/invalid\"\n";
        std::istringstream bad_stream(bad_yaml);
        etdc::ACL invalid_acl(bad_stream);
        std::cout << "FAIL: invalid pattern did not raise" << std::endl;
        ok = false;
    } catch(etdc::assertion_error const& e) {
        std::cout << "Caught expected error: " << e.what() << std::endl;
    }

    if( ok ) {
        std::cout << "OK" << std::endl;
        return 0;
    }

    std::cout << "NOT OK" << std::endl;
    return 1;
}
