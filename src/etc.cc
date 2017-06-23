// etransfer client program/etdc=etransfer daemon + client
#include <version.h>
#include <etdc_fd.h>
#include <etdc_thread.h>
#include <etdc_etd_state.h>
#include <etdc_etdserver.h>
#include <etdc_stringutil.h>
#include <etdc_streamutil.h>
#include <argparse.h>

// C++ standard headers
#include <map>
#include <thread>
#include <string>
#include <vector>
#include <iterator>
#include <iostream>
#include <functional>

using namespace std;
namespace AP = argparse;

// The client may support local URLs by just using "/path/to/file"
// so the whole "protocol://[user@]host[:port]" prefix must be optional
//
//
// Note: the digits under the '(' are the submatch indices of that group
static const std::regex rxURL{
    /* remote prefix is optional! */
    "("
//   1
    /* protocol */
    "((tcp|udt)6?)://"
//   23 
    /* optional user@ prefix */
    "(([a-z0-9]+)@)?" 
//   45
    /* non-optional host name or IPv6 colon 'coloned hex' (with optional interface suffix) in literal []'s*/
    "([-a-z0-9\\.]+|\\[[:0-9a-f]+(/[0-9]{1,3})?(%[a-z0-9\\.]+)?\\])" 
//   6                           7             8
    /* port number - maybe default? */
    "(:([0-9]+))?"
//   9 10
    /* remote prefix is optional!*/
    ")?"
//
    /* optional path following non-optional slash */
    "(/.*)"
//   11 
    , std::regex_constants::ECMAScript | std::regex_constants::icase
};

// We convert into this type
struct url_type {
    // URL components - see the regex above
    etdc::protocol_type protocol;
    std::string         user;
    etdc::host_type     host;
    etdc::port_type     port;
    std::string         path;
    bool                isLocal;
};

struct str2url_type:
    // we pretend to be a converter!
    public AP::detail::conversion_t {

    // to be a converter we must have "void (<target type>&, std::string const&) const"
    // The string is guaranteed to match the regex above :-)
    void operator()(url_type& url, std::string const& s) const {
        // We're going to repeat the matching: we need the submatches now.
        // The cmdline has already verified the match so we can do this
        // unchecked.
        std::match_results<std::string::const_iterator> m;

        std::regex_match(s, m, rxURL);
        // path HAS to be there
        url.path = m[11];

        // If local path, then we're done
        if( (url.isLocal=(m[1].length()==0)) )
            return;

        // Not local: extract+convert the matched groups
        url.protocol = etdc::protocol_type(m[2]);
        url.user     = m[5];
        url.host     = etdc::host_type( str2url_type::unbracket(m[6]) );
        url.port     = (m[10].length() ? port(m[10]) : port(4004));
    }

    // the host name may be surrounded by '[' ... ']' for a literal
    // "coloned hex" IPv6 address
    static std::string unbracket(std::string const& h) {
        static const std::regex rxBracket("\\[([:0-9a-f]+(/[0-9]{1,3})?(%[a-z0-9]+)?)\\]",
                                          std::regex_constants::ECMAScript | std::regex_constants::icase);
        return std::regex_replace(h, rxBracket, "$1");
    }
};
template <class CharT, class Traits>
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, url_type const& url) {
    os << (url.isLocal ? "PATH: " : "URL: ");
    if( !url.isLocal )
        os << url.protocol << "://"
           << (url.user.empty() ? "" : url.user+"@")
           << url.host 
           << ((url.port == etdc::any_port) ? "" : std::string(":")+etdc::repr(url.port));
    return os << url.path;
}

HUMANREADABLE(url_type, "URL")
HUMANREADABLE(etdc::openmode_type, "file copy mode")

// Make sure our zignal handlert has C-linkage
extern "C" {
    void dummy_signal_handler(int) { }
}


int main(int argc, char const*const*const argv) {
    // First things first: block ALL signals
    etdc::BlockAll         ba;
    // Let's set up the command line parsing
    int                    message_level = 0;
    etdc::openmode_type    mode{ etdc::openmode_type::New };
    AP::ArgumentParser     cmd( AP::version( buildinfo() ),
                                AP::docstring("'ftp' like etransfer client program.\n"
                                              "This is to be used with etransfer daemon (etd) for "
                                              "high speed file/directory transfers or it can be used "
                                              "to list the contents of a remote directory, if the remote "
                                              "etransfer daemon allows your credentials to do so."),
                                AP::docstring("Remote URLs are formatted as (tcp|udt)[6]://[user@]host[:port]/path\n"
                                              "Paths on the local machine are specified just as /<path> (i.e. absolute path)") );
    // The URLs from the command line
    unsigned int           nLocal = 0;
    std::vector<url_type>  urls;

    // What does our command line look like?
    //
    // <prog> [-h] [--help] [--version]
    //        [-m <int>] { [--list SRC] | SRC DST }
    //
    cmd.add( AP::long_name("help"), AP::print_help(),
             AP::docstring("Print full help and exit succesfully") );
    cmd.add( AP::short_name('h'), AP::print_usage(),
             AP::docstring("Print short usage and exit succesfully") );
    cmd.add( AP::long_name("version"), AP::print_version(),
             AP::docstring("Print version and exit succesfully") );

    // message level: higher = more verbose
    cmd.add( AP::store_into(message_level), AP::short_name('m'),
             AP::maximum_value(5), AP::minimum_value(-1), AP::at_most(1),
             AP::docstring("Message level - higher = more output") );

    // User can choose between:
    //  * the target file(s) may not exist [default]
    //  * the target file(s) may or may not exits but will be truncated if
    //    they do [basically retransmit everything, "overwrite"]
    //  * the target file(s) may or may not exist, exisiting files will be
    //    appended to [resume]
    cmd.addXOR(
            AP::option(AP::store_const_into(etdc::openmode_type::OverWrite, mode), AP::long_name("overwrite"),
                       AP::docstring("Existing target file(s) will be overwritten (default: target file(s) may not exist)"),
                       AP::at_most(1)),
            AP::option(AP::store_const_into(etdc::openmode_type::Resume, mode), AP::long_name("resume"),
                       AP::docstring("Existing target file(s) will be appended to (default: target file(s) may not exist)"),
                       AP::at_most(1))
        );

    cmd.add(AP::long_name("mode"), AP::at_most(1), AP::store_value<etdc::openmode_type>(),
            AP::set_default(etdc::openmode_type::New),
            AP::is_member_of({etdc::openmode_type::New, etdc::openmode_type::OverWrite, etdc::openmode_type::Resume, etdc::openmode_type::SkipExisting}),
            AP::docstring("Set file copy mode"),
            AP::convert([](std::string const& s) { std::istringstream iss(s); etdc::openmode_type om; iss >> om; return om; }));

    // Let the command line parser decide the validity - "--list URL" or "URL URL"
    // are mutually exclusive and IF they are present, the constraint(s) of
    // the one that is present are enforced.
    // Also: user(s) MUST enter at least one of these
    // Also: we add extra constraints that --list can only do remote URLs
    //       (otherwise it is better to use "ls") as well as that no more
    //       than one local url may 
    cmd.addXOR(
        AP::required(),
        AP::option(AP::long_name("list"), AP::collect_into(urls), AP::match(rxURL), AP::at_most(1), str2url_type(),
                   AP::constrain([](url_type const& url) { return !url.isLocal; }, "Can only list remote URLs"),
                   AP::docstring("Request to list the contents of URL")),
        AP::option(AP::collect_into(urls), AP::exactly(2), str2url_type(), AP::match(rxURL),
                   AP::constrain([&](url_type const& url) { if( url.isLocal ) nLocal++; return nLocal<2; }, "At most one local PATH can be given"),
                   AP::docstring("SRC and DST URL/PATH"))
        );

    // Flag wether or not to wait
    cmd.add(AP::store_true(), AP::short_name('b'), AP::docstring("Do not exit but do a blocking read instead"));

    // OK Let's check that mother
    cmd.parse(argc, argv);

    // Set message level based on command line value (or default)
    etdc::dbglev_fn( message_level );

    // The size of the list of URLs is a proxy wether to list or not; a
    // list of length one is only accepted if '--list URL' was given
    etdc::UnBlock                   s({SIGINT});
    etdc::install_handler(dummy_signal_handler, {SIGINT});

    etdc::etd_state                 localState{};
    std::list<etdc::etd_server_ptr> servers;

    // We must transform the URL(s) into ETDServerInterface* 
    std::transform(std::begin(urls), std::end(urls), std::back_inserter(servers),
                   [&](url_type const& url) {
                        return url.isLocal ? mk_etdserver(std::ref(localState)) : mk_etdproxy(url.protocol, url.host, url.port);
                    });
    if( servers.size()==1 )
        for(auto const& p: (*servers.begin())->listPath(urls.begin()->path, false))
            std::cout << p << std::endl;
#if 0
    for(const auto& u: urls)
        std::cout << (urls.size()==1 ? "LIST: " : "") << u << std::endl;

    // Before we go into potential blocking syscalls, enable killing by
    // unblocking a signal + installing dummy signal handler
    etdc::UnBlock                   s({SIGINT});
    etdc::install_handler(dummy_signal_handler, {SIGINT});

    auto pClnt = mk_client(urls[0].protocol, urls[0].host, urls[0].port);
    cout << "connected to " << pClnt->getpeername(pClnt->__m_fd) << " [local " << pClnt->getsockname(pClnt->__m_fd) << "]" << endl;
    const auto data = "012345";
    pClnt->write(pClnt->__m_fd, data, sizeof(data));
    cout << "wrote " << sizeof(data) << " bytes" << endl;

    if( cmd.get<bool>("b") ) {
        char buf[128];
        cout << "entering blocking read" << endl;
        pClnt->read(pClnt->__m_fd, buf, sizeof(buf));
    }
#endif
    return 0;
}


