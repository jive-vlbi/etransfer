// etransfer client program of the etdc=etransfer daemon + client
// Copyright (C) 2007-2016 Harro Verkouter
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
// 
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE.  See the GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
// 
// Author:  Harro Verkouter - verkouter@jive.eu
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#include <version.h>
#include <etdc_fd.h>
#include <etdc_thread.h>
#include <etdc_etd_state.h>
#include <etdc_etdserver.h>
#include <etdc_stringutil.h>
#include <etdc_streamutil.h>
#include <etdc_sciprint.h>
#include <argparse.h>

// C++ standard headers
#include <map>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <iterator>
#include <iostream>
#include <functional>

using namespace std;
namespace AP = argparse;

// The client may support local URLs by just using "/path/to/file"
//
// Better shtick to what ppl understand:
//  [[(tcp|udt)6?://][user@]host[#port]/]path
//
static const std::regex rxURL{
    /* remote prefix is optional! */
    "("
//   1
    /* protocol */
    "(((tcp|udt)6?):\\/\\/)?"
//   234 
    /* optional user@ prefix */
    "(([a-z0-9]+)@)?" 
//   56
    /* non-optional host name or IPv6 colon 'coloned hex' (with optional interface suffix) in literal []'s*/
    "([-a-zA-Z0-9_\\.]+|\\[[:0-9a-fA-F]+(/[0-9]{1,3})?(%[a-zA-Z0-9\\.]+)?\\])" 
//   7                                  8             9
    /* port number - maybe default? */
    "(#([0-9]+))?"
//   1011
    /* remote prefix is optional!*/
    ":)?"
    /* path is whatever's left */
    "(.+)"
//   12
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
        url.path = m[12];

        // If local path, then we're done
        if( (url.isLocal=(m[1].length()==0)) )
            return;

        // Not local: extract+convert the matched groups
        url.protocol = (m[3].length() ? etdc::protocol_type(m[3]) : etdc::protocol_type("tcp"));
        url.user     = m[6];
        url.host     = etdc::host_type( etdc::unbracket(m[7].str()) );
        url.port     = (m[11].length() ? port(m[11]) : port(4004));
    }

};
template <class CharT, class Traits>
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, url_type const& url) {
    os << (url.isLocal ? "PATH: " : "URL: ");
    if( !url.isLocal )
        os << url.protocol << ":"
           << (url.user.empty() ? "" : url.user+"@")
           << url.host 
           << ((url.port == etdc::any_port) ? "" : std::string(":")+etdc::repr(url.port))
           << "://";
    return os << url.path;
}

HUMANREADABLE(url_type, "URL")
HUMANREADABLE(etdc::openmode_type, "file copy mode")
HUMANREADABLE(std::chrono::duration<float>, "duration (s)")

// Make sure our zignal handlert has C-linkage
extern "C" {
    // We need to be able to kick e.g. the main thread out of blocking
    // systemcalls after closing file descriptors behind it's back.
    // So this dummy handler can be installed to have the signal be
    // "handled" by NOT the system, and profit from its sideeffects
    void dummy_signal_handler(int) { }
}

// This is supposed to execute in a separate thread and promises to deliver
// the signal number that was raised
using signallist_type = std::vector<int>;
using unique_result   = std::unique_ptr<etdc::result_type>;

// A thread that just sits there to waiting for any of the signals listed to happen and if it
// does takes affirmative action to cancel any transfers and "kill" the
// indicated thread using the KillSignal when done.
#define KILLMAINSIGNAL SIGUSR1

template <int KillSignal>
static void signal_thread( signallist_type const& sigs, pthread_t tid,
                           etdc::etd_state& state, std::vector<etdc::etd_server_ptr>& servers,
                           unique_result (&results)[2] ) {
    int       received;
    sigset_t  sset;

    // Before we actually unblock the signals, we prepare the sigset_t for
    // signals we'll wait for
    // Note: on some platforms sigemptyset() is a macro, not a fn call
    //       so "::sigemptyset()" don't compile, unfortunately :-(
    //       And likewise for sigaddset & friends
    sigemptyset(&sset);
    for(auto s: sigs)
        sigaddset(&sset, s);

    ETDCDEBUG(4, "sigwaiterthread: enter wait phase" << endl);
    // ... and wait for any of them to happen
    ::sigwait(&sset, &received);
    ETDCDEBUG(4, "sigwaiterthread: got signal " << received << endl);

    // Do the magic on the etransfer state
    std::atomic_store(&state.cancelled, true);

    // Grab the lock on the pointers 
    // so we can modify stuff
//    etdc::scoped_lock   lk( state.lock );

    // Loop over all local transfers, if any, and close the data file descriptor
	for(auto xferptr = std::begin(state.transfers); xferptr!=std::end(state.transfers); xferptr++ ) {
		if( xferptr->second->data_fd ) {
			ETDCDEBUG(0, "sigwaiterthread: Closing " << xferptr->second->data_fd->getsockname( xferptr->second->data_fd->__m_fd ) << std::endl);
			xferptr->second->data_fd->close( xferptr->second->data_fd->__m_fd );
		}
	}
    // (try to) break down from back to front
    // note: we MUST TRY ALL OF THEM
    // so we cannot put them all in a single try-catch;
    // a failure to close the first one does not imply
    // that we cannot close the second one.
    // And since the functions throw on wonky we must catch them separately
    try {
        if( results[1]  ) {
            auto uuid = etdc::get_uuid(*results[1] );
            ETDCDEBUG(4, "sigwaiterthread: removing DST uuid  " << uuid << std::endl);
            servers[1]->cancel( uuid );
        }
    }
    catch( ... ) { }
    try {
        if( results[0]  ) {
            auto uuid = etdc::get_uuid(*results[0] );
            ETDCDEBUG(4, "sigwaiterthread: removing SRC uuid  " << uuid << std::endl);
            servers[0]->cancel( uuid );
        }
    }
    catch( ... ) { }

    // Now signal the main thread - blocking functions must be kicked so
    // they can drop out of themselves with e.g. invalid file descriptor
    ::pthread_kill(tid, KillSignal);
    ETDCDEBUG(2, "sigwaiterthread: done." << std::endl);
}

using unique_result = std::unique_ptr<etdc::result_type>;
template <int KillSignal>
static void signal_thread_new(signallist_type const& sigs, pthread_t tid,
                              std::atomic<bool>& cancelled, std::mutex& mtx, std::vector<etdc::etd_server_ptr>& servers,
                              unique_result (&results)[2]) {
    int       received;
    sigset_t  sset;

    // Before we actually unblock the signals, we prepare the sigset_t for
    // signals we'll wait for
    // Note: on some platforms sigemptyset() is a macro, not a fn call
    //       so "::sigemptyset()" don't compile, unfortunately :-(
    //       And likewise for sigaddset & friends
    sigemptyset(&sset);
    for(auto s: sigs)
        sigaddset(&sset, s);

    ETDCDEBUG(4, "sigwaiterthread: enter wait phase" << endl);
    // ... and wait for any of them to happen
    ::sigwait(&sset, &received);
    ETDCDEBUG(4, "sigwaiterthread: got signal " << received << endl);

    // Do the magic on the etransfer state
    std::atomic_store(&cancelled, true);

    // Grab the lock on the pointers 
    // so we can modify stuff
    etdc::scoped_lock   lk( mtx );

    // (try to) break down from back to front
    // note: we MUST TRY ALL OF THEM
    // so we cannot put them all in a single try-catch;
    // a failure to close the first one does not imply
    // that we cannot close the second one.
    // And since the functions throw on wonky we must catch them separately
    try {
        if( results[1]  ) {
            auto uuid = etdc::get_uuid(*results[1] );
            ETDCDEBUG(4, "sigwaiterthread: removing DST uuid  " << uuid << std::endl);
            servers[1]->removeUUID( uuid );
            // after the UUID's been removed no need keeping the result around
            results[1].reset( nullptr );
        }
    }
    catch( ... ) { }
    try {
        if( results[0]  ) {
            auto uuid = etdc::get_uuid(*results[0] );
            ETDCDEBUG(4, "sigwaiterthread: removing SRC uuid  " << uuid << std::endl);
            servers[0]->removeUUID( etdc::get_uuid(*results[0] ) );
            // after the UUID's been removed no need keeping the result around
            results[0].reset( nullptr );
        }
    }
    catch( ... ) { }

    // Now signal the main thread - blocking functions must be kicked so
    // they can drop out of themselves with e.g. invalid file descriptor
    ::pthread_kill(tid, KillSignal);
    ETDCDEBUG(2, "sigwaiterthread: done." << std::endl);
}

struct socketoptions_type {

    socketoptions_type():
        bufSize{ 32*1024*1024 }, MTU{ 1500 }
    {}

    size_t        bufSize;
    unsigned int  MTU;
};

// Namespace local to this to wrap mk_etdserver and make it retry if
// protocolVersion is not supported
namespace etc {

    template <typename... Args>
    auto mk_etdproxy(Args&&... args) -> decltype( ::mk_etdserver(std::forward<Args>(args)...) ) {
        // If the initial connection already fails we "pass on" the exception
        auto rv = ::mk_etdproxy( std::forward<Args>(args)... );
        try {
            // the real trial is to execute this one. If this fails the
            // remote end hung up becaus it didn't support the protocol
            // version command i.e. version 0
            rv->protocolVersion();
        }
        catch( ... ) {
            // Oh crap ... reconnect and set the protocol version manually to 0
            rv = ::mk_etdproxy( std::forward<Args>(args)... );
            // And we should make sure that we only set it once - i.e. that
            // the previous "supported protocol version" is not yet set
            ETDCASSERT( rv->set_protocolVersion( 0 ) == etdc::ETDServerInterface::unknownProtocolVersion,
                        "The proxy had its protocol version already set?!" );
        }
        return rv;
    }
}


enum display_format { continental, imperial };


int main(int argc, char const*const*const argv) {
    // First things first: block ALL signals
    etdc::BlockAll              ba;
    etdc::etd_state             localState{};
    // Let's set up the command line parsing
    int                          message_level = 0;
    unsigned int                 maxFileRetry{ 2 }, nFileRetry{ 0 };
    std::chrono::duration<float> retryDelay{ 10 };
    display_format               display( imperial );
    etdc::openmode_type          mode{ etdc::openmode_type::New };
    etdc::numretry_type          connRetry{ 2 };
    etdc::retrydelay_type        connDelay{ 5 };

    AP::ArgumentParser     cmd( AP::version( buildinfo() ),
                                AP::docstring("'ftp' like etransfer client program.\n"
                                              "This is to be used with etransfer daemon (etd) for "
                                              "high speed file/directory transfers or it can be used "
                                              "to list the contents of a remote directory, if the remote "
                                              "etransfer daemon allows your credentials to do so."),
                                AP::docstring("Remote URLs are formatted as\n\t[[tcp|udt][6]://][user@]host[#port]:/path\n"
                                              "Paths on the local machine are specified just as /<path> (i.e. absolute path)"),
                                AP::docstring("The syntax on the remote URLs is slightly more complicated than e.g. scp(1) but that is "
                                              "because this client can trigger remote daemon => remote daemon transfers."),
                                AP::docstring("For each remote daemon it must be able to completely specify how to reach it; "
                                              "TCP/IPv4 might the source and UDT/IPv6 might be the destination daemon's address family")  );
    // The URLs from the command line
    unsigned int           nLocal = 0;
    std::vector<url_type>  urls;

    // What does our command line look like?
    //
    // <prog> [-h] [--help] [--version] [--max-retry N] [--retry-delay Y]
    //        [-m <int>] { [--list SRC] | SRC DST }
    //        [--imperial|--continental]
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

    // verbosity
    cmd.add( AP::store_true(), AP::short_name('v'), AP::long_name("verbose"),
             AP::at_most(1), AP::docstring("Enable verbose output for each file transferred") );

    // display format
    cmd.addXOR(
        AP::option(AP::long_name("imperial"), AP::store_const_into(imperial, display),
                   AP::docstring(std::string("Use imperial (American/English) formatting for number representation")+(display == imperial ? " (default)" : ""))),
        AP::option(AP::long_name("continental"), AP::store_const_into(continental, display),
                   AP::docstring(std::string("Use continental (European) formatting for number representation")+(display == continental ? " (default)" : ""))) );

    // How many times to retry file (so total # of tries is N+1) and how
    // long to wait between retries
    cmd.add( AP::long_name("max-retry"), AP::store_into(maxFileRetry),AP::at_most(1),
             AP::docstring("Retry the file transfer this many times, so total number of attempts is N+1") );
    cmd.add( AP::long_name("retry-delay"), AP::store_into(retryDelay), AP::at_most(1),
             AP::docstring("How many seconds to wait between file retries"),
             AP::constrain([](std::chrono::duration<float> const& v) { return v.count()>= 0; }, "duration should be >= 0s"),
             AP::convert([](std::string const& s) { return std::chrono::duration<float>(std::stof(s)); }) );

    // For connections we have separate settings
    cmd.add( AP::long_name("max-conn-retry"), AP::store_into(etdc::untag(connRetry)),AP::at_most(1),
             AP::docstring("Retry to connect this many times, so total number of attempts is N+1") );
    cmd.add( AP::long_name("retry-conn-delay"), AP::store_into(etdc::untag(connDelay)), AP::at_most(1),
             AP::docstring("How many seconds to wait between connection retries"),
             AP::constrain([](std::chrono::duration<float> const& v) { return v.count()>= 0; }, "duration should be >= 0s"),
             AP::convert([](std::string const& s) { return std::chrono::duration<float>(std::stof(s)); }) );

    // User can choose between:
    //  * the target file(s) may not exist [default]
    //  * the target file(s) may or may not exits but will be truncated if
    //    they do [basically retransmit everything, "overwrite"]
    //  * the target file(s) may or may not exist, exisiting files will be
    //    appended to [resume]
    cmd.addXOR(
            AP::option(AP::store_const_into(etdc::openmode_type::OverWrite, mode), AP::long_name("overwrite"),
                        //AP::docstring("Existing target file(s) will be overwritten (default: target file(s) may not exist)"),
                        AP::docstring("Existing target file(s) will be overwritten"),
                        AP::at_most(1)),
            AP::option(AP::store_const_into(etdc::openmode_type::Resume, mode), AP::long_name("resume"),
                        //AP::docstring("Existing target file(s) will be appended to (default: target file(s) may not exist)"),
                        AP::docstring("Existing target file(s) will be appended to, if the source file is larger"),
                        AP::at_most(1)),
            AP::option(AP::store_const_into(etdc::openmode_type::Resume, mode), AP::long_name("skipexisting"),
                        //AP::docstring("Existing target file(s) will be skipped (default: target file(s) may not exist)"),
                        AP::docstring("Existing target file(s) will be skipped"),
                        AP::at_most(1)),
            AP::option(AP::long_name("mode"), AP::at_most(1), AP::store_into(mode),
                        AP::is_member_of({etdc::openmode_type::New, etdc::openmode_type::OverWrite,
                                      etdc::openmode_type::Resume, etdc::openmode_type::SkipExisting}),
                        AP::docstring(std::string("Set file copy mode, default=")+etdc::repr(mode)),
                        AP::convert([](std::string const& s) { std::istringstream iss(s); etdc::openmode_type om; iss >> om; return om; }))
        );


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

    // Allow user to set network related options

    // UDT parameters
    cmd.add( AP::store_into(untag(localState.udtMSS)), AP::long_name("udt-mss"), AP::at_most(1), 
             AP::minimum_value(64), AP::maximum_value(64*1024), // UDP datagram limits
             AP::docstring(std::string("Set UDT maximum segment size. Not honoured if data channel is TCP or doing remote-to-remote transfers. Default ")+etdc::repr(untag(localState.udtMSS))) );

    cmd.add( AP::store_into(untag(localState.udtMaxBW)), AP::long_name("udt-bw"), AP::at_most(1),
             AP::convert([](std::string const& s) { return untag(max_bw(s)); }),
             AP::constrain([](long long int v) { return v==-1 || v>0; }, "-1 (Inf) or > 0 for set rate"),
             AP::docstring(std::string("Set UDT maximum bandwidth. Not honoured if data channel is TCP or doing remote-to-remote transfers. Default ")+etdc::repr(untag(localState.udtMaxBW))) );

    cmd.add( AP::store_into(localState.bufSize), AP::long_name("buffer"),
             AP::docstring(std::string("Set send/receive buffer size. Default ")+etdc::repr(localState.bufSize)) );
    // Flag wether or not to wait
    //cmd.add(AP::store_true(), AP::short_name('b'), AP::docstring("Do not exit but do a blocking read instead"));

    // OK Let's check that mother
    cmd.parse(argc, argv);

    // Set message level based on command line value (or default)
    etdc::dbglev_fn( message_level );

    // The size of the list of URLs is a proxy wether to list or not; a
    // list of length one is only accepted if '--list URL' was given
    const bool                        verbose = cmd.get<bool>("verbose");
    std::vector<etdc::etd_server_ptr> servers;

    // Unblock the signal that can be used to wake us out of blocking system calls
    // and install an empty handler such that the default handler doesn't
    // kill us but we can benefit from observing the side effects of
    // handling the signal by someone else (the sigwaiter thread, to be
    // precise)
    etdc::UnBlock                     s({KILLMAINSIGNAL});
    etdc::install_handler(dummy_signal_handler, {KILLMAINSIGNAL});

    // We must transform the URL(s) into ETDServerInterface* 
    std::transform(std::begin(urls), std::end(urls), std::back_inserter(servers),
                   [&](url_type const& url) {
                        return url.isLocal ? ::mk_etdserver(std::ref(localState)) : etc::mk_etdproxy(url.protocol, url.host, url.port, connRetry, connDelay);
                    });


    std::cout << "This client supports protocol version " << etdc::ETDServerInterface::currentProtocolVersion << std::endl;
    for(const auto srv: servers) {
        std::cout << "Server protocol version: " << srv->protocolVersion() << std::endl;
    }

    // Get the list of files to transfer (or to list if servers.size()==1)
    static const auto isDir = [](std::string const& str) { return !str.empty() && str[str.size()-1]=='/'; };
    const auto        remoteList = servers[0]->listPath(urls[0].path, false);

    if( servers.size()==1 ) {
        for(auto const& p: remoteList)
            std::cout << p << std::endl;
        return 0;
    }

    // OK we have two end points. Do a bit more validation
    ETDCASSERT(urls[1].path.find('*')==std::string::npos && urls[1].path.find('?')==std::string::npos,
               "Destination path may not contain wildcards");

    // If there is >1 files to transfer and the destination is not a directory thats an error
    std::list<std::string> files2do;

    std::copy_if(std::begin(remoteList), std::end(remoteList), std::back_inserter(files2do),
                 [](std::string const& pth) { return !isDir(pth); });

    ETDCASSERT(files2do.empty()==false, "Your path '" << urls[0].path << "' did not match any file(s) to transfer");
    if( files2do.size()>1 )
        ETDCASSERT(isDir(urls[1].path) || urls[1].path=="/dev/null", "Cannot copy " << files2do.size() << " files to the same destination file");

    // Compute output path
    const std::string dstPath      = urls[1].path;
    const bool        dstIsDir     = isDir(dstPath);
    auto const        mkOutputPath = [&](std::string const& in) { return dstIsDir ? dstPath+etdc::detail::basename(in) : dstPath; };

    // Decide on wether to push or pull based on who has a data channel addr.
    // If the destination is a remote daemon it has at least one data channel
    // and then we push data to it
    // If the destination has no data channel, it means that we're copying *into* this
    // client so we don't have a daemon running so we ask the built-in 'daemon' to 
    // pull the file from the source
    bool                    push{ true };
    etdc::host_type         dstHost{ urls[1].host };
    etdc::dataaddrlist_type dataChannels( servers[1]->dataChannelAddr() );

    if( dataChannels.empty() ) {
        push         = false;
        dstHost      = urls[0].host;
        dataChannels = servers[0]->dataChannelAddr();
    }

    // In the data channels, we must replace any of the wildcard IPs with a real host name
    std::regex  rxWildCard("^(::|0.0.0.0)$");
    for(auto ptr=dataChannels.begin(); ptr!=dataChannels.end(); ptr++)
        update_sockname(*ptr, etdc::host_type(std::regex_replace(get_host(*ptr), rxWildCard, dstHost)));

    // Before processing all file(s) we already know if we're going to push or pull
    std::function<etdc::xfer_result(etdc::uuid_type const&, etdc::uuid_type&, off_t, etdc::dataaddrlist_type const&)> fn;
    namespace ph = std::placeholders;
    fn = (push ?
          std::bind(&etdc::ETDServerInterface::sendFile, servers[0].get(), ph::_1, ph::_2, ph::_3, ph::_4) :
          std::bind(&etdc::ETDServerInterface::getFile,  servers[1].get(), ph::_1, ph::_2, ph::_3, ph::_4));

    // Loop over all files to do ...
    auto        fmtByte = (display == continental ? 
                            etdc::mk_to_string<decltype(etdc::xfer_result::__m_BytesTransferred)>(std::fixed, etdc::continental) :
                            etdc::mk_to_string<decltype(etdc::xfer_result::__m_BytesTransferred)>(std::fixed, etdc::imperial) );
    auto        fmt1000 = (display == continental ? 
                            etdc::mk_formatter<double>("iB", etdc::continental, std::setprecision(2)) :
                            etdc::mk_formatter<double>("iB", etdc::imperial, std::setprecision(2)) );
    auto        fmtRate = (display == continental ?
                            etdc::mk_formatter<double>("Bps", etdc::thousand(1024), std::fixed, etdc::continental, std::setprecision(2)) :
                            etdc::mk_formatter<double>("Bps", etdc::thousand(1024), std::fixed, etdc::imperial, std::setprecision(2)) );
    auto        fmtTime = (display == continental ? 
                            etdc::mk_formatter<double>("s", std::setprecision(4), etdc::continental):
                            etdc::mk_formatter<double>("s", std::setprecision(4), etdc::imperial) );
    const int 	lvl( verbose ? -1 : 9 );

    // Enable killing by signal ^C
    unique_result      results[2];

    etdc::thread(&signal_thread<KILLMAINSIGNAL>, signallist_type{{SIGINT, SIGSEGV, SIGTERM, SIGHUP}}, ::pthread_self(),
                 std::ref(localState), std::ref(servers), std::ref(results)).detach();

    for(auto const& file: files2do) {
        // Were we cancelled?
        if( localState.cancelled.load() )
            break;

        // Skip directories
        if( file[file.size()-1]=='/' )
            continue;

        // Keep these out of the while loop
        bool               finished{ false };
        const unsigned int retryCountAtStart = nFileRetry;
        std::exception_ptr eptr;

        // Did someone say Cancel? Or did we reach maximum number of retries?
        while( !std::atomic_load(&localState.cancelled) && !finished && nFileRetry<maxFileRetry ) {
            // Just checked that we weren't cancelled and if we're actually
            // retrying a file we should sleep (new file => don't sleep)
            // also make sure we reset current exception already
            if( retryCountAtStart < nFileRetry ) {
                ETDCDEBUG(4, "Retry #" << nFileRetry+1 << " (#" << (nFileRetry-retryCountAtStart)+1 << " for this file), go to sleep for " <<
                             retryDelay.count() << "s" << std::endl);
                std::this_thread::sleep_for( retryDelay );
            }

//std::cerr << "Entering into try/catch block" << std::endl;
            try {
                auto const outputFN = mkOutputPath(file);
                ETDCDEBUG(lvl, (push ? "PUSH" : "PULL" ) << " " << mode << " " << file << " -> " << outputFN << std::endl);
                unique_result dstResult( new etdc::result_type(servers[1]->requestFileWrite(outputFN, mode)) );
                {
//std::cerr << "moving dstResult to results[1]" << std::endl;
                    etdc::scoped_lock lk( localState.lock );
                    results[1].reset( dstResult.release() );
                }
                auto nByte = etdc::get_filepos( *results[1] );
//std::cerr << "done that, nByte=" << nByte << std::endl;

                if( mode!=etdc::openmode_type::SkipExisting || nByte==0 ) {
//std::cerr << "mode!=SkipExisting or there are bytes to transfer" << std::endl;
                    unique_result srcResult( new etdc::result_type(servers[0]->requestFileRead(file, nByte)) );
//std::cerr << "moving srcResult to results[0]" << std::endl;
                    {
                        etdc::scoped_lock lk( localState.lock );
                        results[0].reset( srcResult.release() );
                    }
                    auto nByteToGo = etdc::get_filepos( *results[0] );
//std::cerr << "done that, nByteToGo=" << nByteToGo << std::endl;

                    if( nByteToGo>0 ) {
//std::cerr << "ACTUALLY CALLING TRANSFER FN" << std::endl;
                        etdc::xfer_result result( fn(etdc::get_uuid(*results[0]), etdc::get_uuid(*results[1]), nByteToGo, dataChannels) );
                        auto const        dt = result.__m_DeltaT.count();
                        std::cout << (result.__m_Finished && std::atomic_load(&localState.cancelled)==false ? "" : "Un") << "finished; succesfully transferred "
                                  << fmt1000(result.__m_BytesTransferred)
                                  << " (" << fmtByte(result.__m_BytesTransferred) << " bytes) in "
                                  << fmtTime(dt) << " "
                                  << "[" << fmtRate( dt>0 ? ((double)result.__m_BytesTransferred)/dt : 0.0) << "]"
                                  << std::endl;
                        finished = result.__m_Finished;
                        if( !finished )
                            std::cout << "--> Reason: " << result.__m_Reason << std::endl;
                    } else {
                        ETDCDEBUG(lvl, "Destination is complete or is larger than source file" << std::endl);
                        finished = true;
                    }
                }
            }
            catch( const std::exception& e ) {
                ETDCDEBUG(3, "Got exception: " << e.what() << std::endl);
                eptr = std::current_exception();
            }
            catch( ... ) {
                eptr = std::current_exception();
                ETDCDEBUG(3, "Got unknown exception: " << std::endl);
            } 
//std::cerr << "And we're outside the try/catch block" << std::endl;
            // ..->removeUUID() may throw, but we really must try to do them
            // both, so even if the first one threw we must still try to remove
            // the 2nd one as well, and neither should have the program be
            // terminated
            try {
                if( results[1] )
                    servers[1]->removeUUID( etdc::get_uuid(*results[1]) );
            }
            catch( ... ) {}

            try {
                if( results[0] )
                    servers[0]->removeUUID( etdc::get_uuid(*results[0]) );
            }
            catch( ... ) {}
            // If we didn't finish, we must retry
            if( !finished )
                nFileRetry++;
            if( nFileRetry>maxFileRetry && eptr )
                std::rethrow_exception( eptr );
        }
    }
    return (std::atomic_load(&localState.cancelled) == true ? 1 : 0);
}
