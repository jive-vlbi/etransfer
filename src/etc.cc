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

// Make sure our zignal handlert has C-linkage
extern "C" {
    void dummy_signal_handler(int) { }
}

struct socketoptions_type {

    socketoptions_type():
        bufSize{ 32*1024*1024 }, MTU{ 1500 }
    {}

    size_t        bufSize;
    unsigned int  MTU;
#if 0
    template <typename... Args>
    auto mk_etdserver(Args&&... args) -> decltype(::mk_etdserver()) {
        return ::mk_etdserver(std::forward<Args>(args)..., etdc::udt_mss{ MTU }, etdc::udt_rcvbuf{ bufSize },
                            etdc::udt_sndbuf{ bufSize }, etdc::so_rcvbuf{ bufSize }, etdc::so_sndbuf{ bufSize });
    }
    template <typename... Args>
    auto mk_etdproxy(Args&&... args) -> decltype(::mk_etdserver()) {
        return ::mk_etdproxy(std::forward<Args>(args)..., etdc::udt_mss{ MTU }, etdc::udt_rcvbuf{ bufSize },
                           etdc::udt_sndbuf{ bufSize }, etdc::so_rcvbuf{ bufSize }, etdc::so_sndbuf{ bufSize });
    }
#endif
};


enum display_format { continental, imperial };


int main(int argc, char const*const*const argv) {
    // First things first: block ALL signals
    etdc::BlockAll         ba;
    // Let's set up the command line parsing
    int                    message_level = 0;
    display_format         display( imperial );
#if 0
    socketoptions_type     sockopts{};
#endif
    etdc::openmode_type    mode{ etdc::openmode_type::New };
    AP::ArgumentParser     cmd( AP::version( buildinfo() ),
                                AP::docstring("'ftp' like etransfer client program.\n"
                                              "This is to be used with etransfer daemon (etd) for "
                                              "high speed file/directory transfers or it can be used "
                                              "to list the contents of a remote directory, if the remote "
                                              "etransfer daemon allows your credentials to do so."),
                                AP::docstring("Remote URLs are formatted as ((tcp|udt)[6]://)[user@]host[#port]/path\n"
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

    // verbosity
    cmd.add( AP::store_false(), AP::short_name('v'), AP::long_name("verbose"),
             AP::at_most(1), AP::docstring("Verbose output for each file transferred") );

    // display format
    cmd.addXOR(
        AP::option(AP::long_name("imperial"), AP::store_const_into(imperial, display),
                   AP::docstring(std::string("Use imperial (American/English) formatting for number representation")+(display == imperial ? " (default)" : ""))),
        AP::option(AP::long_name("continental"), AP::store_const_into(continental, display),
                   AP::docstring(std::string("Use continental (European) formatting for number representation")+(display == continental ? " (default)" : ""))) );

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
#if 0
    // Allow user to set network related options
    cmd.add( AP::store_into(sockopts.MTU), AP::long_name("mss"),
             AP::minimum_value((unsigned int)64), AP::maximum_value((unsigned int)65536), // UDP datagram limits
             AP::docstring(std::string("Set UDT maximum segment size. Not honoured if data channel is TCP. Default ")+etdc::repr(sockopts.MTU)) );
    cmd.add( AP::store_into(sockopts.bufSize), AP::long_name("buffer"),
             AP::docstring(std::string("Set send/receive buffer size. Default ")+etdc::repr(sockopts.bufSize)) );
#endif
    // Flag wether or not to wait
    //cmd.add(AP::store_true(), AP::short_name('b'), AP::docstring("Do not exit but do a blocking read instead"));

    // OK Let's check that mother
    cmd.parse(argc, argv);

    // Set message level based on command line value (or default)
    etdc::dbglev_fn( message_level );

    // The size of the list of URLs is a proxy wether to list or not; a
    // list of length one is only accepted if '--list URL' was given
    etdc::UnBlock                   s({SIGINT});
    etdc::install_handler(dummy_signal_handler, {SIGINT});

    const bool                        verbose = cmd.get<bool>("verbose");
    etdc::etd_state                   localState{};
    std::vector<etdc::etd_server_ptr> servers;

    // We must transform the URL(s) into ETDServerInterface* 
    std::transform(std::begin(urls), std::end(urls), std::back_inserter(servers),
                   [&](url_type const& url) {
                        return url.isLocal ? ::mk_etdserver(std::ref(localState)) : ::mk_etdproxy(url.protocol, url.host, url.port);
                    });

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
        *ptr = mk_sockname(get_protocol(*ptr), etdc::host_type(std::regex_replace(get_host(*ptr), rxWildCard, dstHost)), get_port(*ptr));

    // Before processing all file(s) we already know if we're going to push or pull
    std::function<etdc::xfer_result(etdc::uuid_type const&, etdc::uuid_type&, off_t, etdc::dataaddrlist_type const&)> fn;
    namespace ph = std::placeholders;
    fn = (push ?
          std::bind(&etdc::ETDServerInterface::sendFile, servers[0].get(), ph::_1, ph::_2, ph::_3, ph::_4) :
          std::bind(&etdc::ETDServerInterface::getFile,  servers[1].get(), ph::_1, ph::_2, ph::_3, ph::_4));

    // Loop over all files to do ...
    using unique_result = std::unique_ptr<etdc::result_type>;
    auto        fmtByte = (display == continental ? 
                            etdc::mk_to_string<decltype(etdc::xfer_result::__m_BytesTransferred)>(std::fixed, etdc::continental) :
                            etdc::mk_to_string<decltype(etdc::xfer_result::__m_BytesTransferred)>(std::fixed, etdc::imperial) );
    auto        fmt1000 = (display == continental ? 
                            etdc::mk_formatter<decltype(etdc::xfer_result::__m_BytesTransferred)>("iB", etdc::continental) :
                            etdc::mk_formatter<decltype(etdc::xfer_result::__m_BytesTransferred)>("iB", etdc::imperial) );
    auto        fmtRate = (display == continental ?
                            etdc::mk_formatter<double>("Bps", etdc::thousand(1024), std::fixed, etdc::continental, std::setprecision(2)) :
                            etdc::mk_formatter<double>("Bps", etdc::thousand(1024), std::fixed, etdc::imperial, std::setprecision(2)) );
    auto        fmtTime = (display == continental ? 
                            etdc::mk_formatter<double>("s", std::setprecision(3), etdc::continental) :
                            etdc::mk_formatter<double>("s", std::setprecision(3), etdc::imperial) );
    const int 	lvl( verbose ? -1 : 9 );

    for(auto const& file: files2do) {
        // Skip directories
        if( file[file.size()-1]=='/' )
            continue;
        // We must keep these outside the try/catch such that we can clean up?
        unique_result      srcResult, dstResult;
        std::exception_ptr eptr;
        try {
            auto const outputFN = mkOutputPath(file);
            ETDCDEBUG(lvl, (push ? "PUSH" : "PULL" ) << " " << mode << " " << file << " -> " << outputFN << std::endl);
            dstResult = std::move( unique_result(new etdc::result_type(servers[1]->requestFileWrite(outputFN, mode))) );
            auto nByte = etdc::get_filepos(*dstResult);

            if( mode!=etdc::openmode_type::SkipExisting || nByte==0 ) {
                srcResult      = std::move(  unique_result(new etdc::result_type(servers[0]->requestFileRead(file, nByte))) );
                auto nByteToGo = etdc::get_filepos(*srcResult);

                if( nByteToGo>0 ) {
                    etdc::xfer_result result( fn(etdc::get_uuid(*srcResult), etdc::get_uuid(*dstResult), nByteToGo, dataChannels) );
                    auto const        dt = result.__m_DeltaT.count();
                    std::cout << (result.__m_Finished ? "" : "Un") << "succesfully transferred " << fmt1000(result.__m_BytesTransferred) << " (" << fmtByte(result.__m_BytesTransferred) << " bytes) in " << fmtTime(dt) << " seconds "
                              << "[" << fmtRate( dt>0 ? ((double)result.__m_BytesTransferred)/dt : 0.0) << "]" << std::endl;
                } else
                    ETDCDEBUG(lvl, "Destination is complete or is larger than source file" << std::endl);
            }
        }
        catch( ... ) {
            eptr = std::current_exception();
        }
        if( dstResult )
            servers[1]->removeUUID( etdc::get_uuid(*dstResult) );
        if( srcResult )
            servers[0]->removeUUID( etdc::get_uuid(*srcResult) );
        if( eptr )
            std::rethrow_exception(eptr);
    }
    return 0;
}


