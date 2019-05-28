// etransfer server program/etdc=etransfer daemon + client
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
#include <reentrant.h>
//#include <etdc_thread.h>
#include <etdc_debug.h>
#include <etdc_etd_state.h>
#include <etdc_etdserver.h>
#include <etdc_stringutil.h>
#include <argparse.h>

#include <map>
#include <thread>
#include <string>
#include <vector>
#include <future>
#include <iterator>
#include <iostream>
#include <functional>

// C-stuff
#include <pwd.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
//#include <uuid/uuid.h>
#include <sys/resource.h>


// Some handy shorthands
using namespace std;
namespace AP = argparse;

using fdlist_type     = std::list<etdc::etdc_fdptr>;
using signallist_type = std::vector<int>;


///////////////////////////////////////////////////////////////////////////////
//
// When daemonizing we may need to change to different user id
//
///////////////////////////////////////////////////////////////////////////////
HUMANREADABLE(struct passwd*, "user name")
template <typename... Traits>
std::basic_ostream<Traits...>& operator<<(std::basic_ostream<Traits...>& os, struct passwd const*const usert) {
    return os << ((usert==nullptr) ? "<unknown usert>" : usert->pw_name);
}
void  do_daemonize( void );

///////////////////////////////////////////////////////////////////////////////
//
//  Map protocol to function-that-prints-some-debug-info about the socket
//  for that protocol
//
///////////////////////////////////////////////////////////////////////////////

static std::map<std::string, std::function<void(etdc::etdc_fdptr, std::string const&)>>
dbgMap = {
    {"udt", [](etdc::etdc_fdptr pSok, std::string const& s) {
                                                                etdc::udt_rcvbuf  rcv;
                                                                etdc::udt_sndbuf  snd;
                                                                etdc::udt_linger  linger;
                                                                etdc::getsockopt(pSok->__m_fd, rcv, linger, snd);
                                                                ETDCDEBUG(1, s << "/UDT rcvbuf = " << rcv << " sndbuf = " << snd << " linger=" << untag(linger).l_onoff << ":" << untag(linger).l_linger << endl);
                                                            }},
    {"udt6", [](etdc::etdc_fdptr pSok, std::string const& s) {
                                                                 etdc::udt_rcvbuf  rcv;
                                                                 etdc::udt_sndbuf  snd;
                                                                 etdc::udt_linger  linger;
                                                                 etdc::getsockopt(pSok->__m_fd, rcv, linger);
                                                                 ETDCDEBUG(1, s << "/UDT6 rcvbuf = " << rcv << " sndbuf = " << snd << " linger=" << untag(linger).l_onoff << ":" << untag(linger).l_linger << endl);
                                                             }},
    {"tcp", [](etdc::etdc_fdptr pSok, std::string const& s) {
                                                                etdc::so_rcvbuf  rcv;
                                                                etdc::so_sndbuf  snd;
                                                                etdc::getsockopt(pSok->__m_fd, rcv, snd);
                                                                ETDCDEBUG(1, s << "/TCP rcvbuf = " << rcv << " sndbuf = " << snd << endl);
                                                            }},
    {"tcp6", [](etdc::etdc_fdptr pSok, std::string const& s) {
                                                                 etdc::so_rcvbuf  rcv;
                                                                 etdc::so_sndbuf  snd;
                                                                 etdc::ipv6_only  ipv6;
                                                                 etdc::getsockopt(pSok->__m_fd, rcv, ipv6, snd);
                                                                 ETDCDEBUG(1, s << "/TCP6 rcvbuf = " << rcv << " sndbuf = " << snd << ", ipv6 only = " << ipv6 << endl);
                                                             }}
};

///////////////////////////////////////////////////////////////////////////////
//
//  Transform string on command line into a working server file descriptor
//
///////////////////////////////////////////////////////////////////////////////

// Introduce a readable overload for a fdptr so it'll render human readable
// in the automatically generated help
HUMANREADABLE(etdc::etdc_fdptr, "address")

// Let's make the URL syntax at least somewhat similar to that of the client:
//     protocol://[local address][:port]
//
// In the string below the digits under the '(' are the submatch indices of that group.
static const std::regex rxURL{
    /* protocol */
    "((tcp|udt)6?)://"
//   12 
    /* optional host name or IPv6 'coloned hex' (with optional interface suffix) in literal []'s*/
    "([-a-z0-9\\.]+|\\[[:0-9a-f]+(/[0-9]{1,3})?(%[a-z0-9\\.]+)?\\])?" 
//   3                           4             5
    /* port number - maybe default? */
    "(:([0-9]+))?"
//   6 7
    , std::regex_constants::ECMAScript | std::regex_constants::icase
};


// the host name may be surrounded by '[' ... ']' for a literal
// "coloned hex" IPv6 address
static std::string unbracket(std::string const& h) {
    static const std::regex rxBracket("\\[([:0-9a-f]+(/[0-9]{1,3})?(%[a-z0-9]+)?)\\]",
                                      std::regex_constants::ECMAScript | std::regex_constants::icase);
    return std::regex_replace(h, rxBracket, "$1");
}

struct socketoptions_type {

    socketoptions_type():
        bufSize{ 32*1024*1024 }, MTU{ 1500 }
    {}

    size_t        bufSize;
    unsigned int  MTU;
};


struct string2socket_type_m {
    string2socket_type_m() = delete;
    string2socket_type_m(etdc::port_type defPort, socketoptions_type const& so):
        __m_default_port( defPort ), __m_sockopts( so )
    {}

    // to be a converter we must have "void (<target type>&, std::string const&) const"
    // The string is guaranteed to match the regex above :-)
    etdc::etdc_fdptr operator()(std::string const& s) const {
        // We're going to repeat the matching: we need the submatches now.
        // The cmdline has already verified the match so we can do this
        // unchecked.
        etdc::etdc_fdptr                                fd;
        std::match_results<std::string::const_iterator> m;

        std::regex_match(s, m, rxURL);

        fd = mk_server(etdc::protocol_type(m[1]), etdc::host_type(unbracket(m[3])), // protocol + local addres (if any)
                       (m[7].length() ? port(m[7]) :  __m_default_port), // port
                       etdc::udt_mss{ __m_sockopts.MTU },
                       //etdc::udt_rcvbuf{ __m_sockopts.bufSize }, etdc::udt_sndbuf{ __m_sockopts.bufSize },
                       etdc::so_rcvbuf{ __m_sockopts.bufSize }, etdc::so_sndbuf{ __m_sockopts.bufSize },
                       //etdc::udt_rcvbuf{32*1024*1024}, etdc::udt_sndbuf{32*1024*1024}, etdc::so_rcvbuf{4*1024},  // some socket options
                       etdc::blocking_type{true});

        auto socknm =  fd->getsockname(fd->__m_fd);
        ETDCDEBUG(2, "etd: server is-at " << socknm << endl);
        dbgMap[get_protocol(socknm)](fd, "server"); 
        return fd;
   }

    const etdc::port_type    __m_default_port;
    const socketoptions_type __m_sockopts;
};



////////////////////////////////////////////////////////////////////////////////////
//
// Forward declarations &cet
//
////////////////////////////////////////////////////////////////////////////////////
template <int> void command_server_thread(etdc::etdc_fdptr fd, etdc::etd_state&);
template <int> void data_server_thread(etdc::etdc_fdptr fd, etdc::etd_state&);

// Make sure our zignal handlert has C-linkage
extern "C" {
    void dummy_signal_handler(int) { }
}

// This is supposed to execute in a separate thread and promises to deliver
// the signal number that was raised
void signal_thread(signallist_type const& sigs, std::promise<int>& promise) {
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

    ETDCDEBUG(2, "sigwaiterthread: enter wait phase" << endl);
    // ... and wait for any of them to happen
    ::sigwait(&sset, &received);
    ETDCDEBUG(2, "sigwaiterthread: got signal " << received << endl);
    promise.set_value( received );
}


int main(int argc, char const*const*const argv) {
    // First things first: block ALL signals
    etdc::BlockAll      ba;
    // Let's set up the command line parsing
    int                 message_level = 0;
    socketoptions_type  sockopts{};
    AP::ArgumentParser  cmd( AP::version( buildinfo() ),
                             AP::docstring("'ftp' like etransfer server daemon, to be used with etransfer client for "
                                           "high speed file/directory transfers."),
                             AP::docstring("addresses are given like (tcp|udt)[6]://[local address][:port]\n"
                                           "where:\n"
                                           "    [local address] defaults to all interfaces\n"
                                           "    [port]          defaults to 4004 (command) or 8008 (data)\n"),
                             AP::docstring("IPv6 coloned-hex format is supported for [local address] by "
                                           "enclosing the IPv6 address in square brackets: [fe80::1/64%enp4]") );

    // What does our command line look like?
    //
    // <prog> --control <address> --data <address>
    //        --acl <access control list>
    //        [-h] [--help] [--version]
    //        [-m <int>]
    //        [-f] (foreground)
    //
    // <address> = [udt|tcp]/[<local IP>]/<port>
    //             (if <local IP> not given, listen on all interfaces)

    cmd.add( AP::long_name("help"), AP::print_help(),
             AP::docstring("Print full help and exit succesfully") );
    cmd.add( AP::short_name('h'), AP::print_usage(),
             AP::docstring("Print short usage and exit succesfully") );
    cmd.add( AP::long_name("version"), AP::print_version(),
             AP::docstring("Print version and exit succesfully") );

    // These are mutex: if foregrounding you can't set a run-as user, and if
    //                  you've set a run-as user then you cannot not daemonize
    cmd.addXOR(
        // -f              run in foreground, i.e. do NOT daemonize
        AP::option(AP::short_name('f'), AP::store_true(),
                   AP::docstring("Run in foreground, i.e. do NOT daemonize")),
        // --run-as <USER> run daemon as user <USER>
        AP::option(AP::long_name("run-as"), AP::store_value<struct passwd*>(), AP::at_most(1),
                   AP::docstring("Run daemon under this user name"),
                   // Default = current user
                   AP::set_default( ::getpwuid(::geteuid()) ),
                   // We do not allow unknown user
                   AP::constrain([](struct passwd* ptr) { return ptr!=nullptr;}, "user name must exist on this system"),
                   // And we convert user name to passwd entry
                   AP::convert([](std::string const& username) { return ::getpwnam(username.c_str()); }))
        );

    // message level: higher = more verbose
    cmd.add( AP::store_into(message_level), AP::short_name('m'),
             AP::maximum_value(5), AP::minimum_value(-1), AP::at_most(1),
             AP::docstring("Message level - higher = more output") );

    // Allow user to set network related options
    cmd.add( AP::store_into(sockopts.MTU), AP::long_name("mss"), AP::at_most(1),
             AP::minimum_value((unsigned int)64), AP::maximum_value((unsigned int)65536), // UDP datagram limits
             AP::docstring(std::string("Set UDT maximum segment size. Not honoured if data channel is TCP. Default ")+etdc::repr(sockopts.MTU)) );
    cmd.add( AP::store_into(sockopts.bufSize), AP::long_name("buffer"), AP::at_most(1),
             AP::docstring(std::string("Set send/receive buffer size. Default ")+etdc::repr(sockopts.bufSize)) );

    // command servers; we require at least one of 'm
    cmd.add( AP::collect<std::string>(), AP::long_name("command"),
             // Constraints on the number + form of the argument
             AP::at_least(1), AP::match(rxURL),
             // And some useful info
             AP::docstring("Listen on this(these) address(es) for incoming client control connections") );

    // data servers; we require at least one of those
    cmd.add( AP::collect<std::string>(), AP::long_name("data"),
             // Constraints on the number + form of the argument
             AP::at_least(1), AP::match(rxURL),
             // And some useful info
             AP::docstring("Listen on this(these) address(es) for incoming client data connections") );

    // OK Let's check that mother
    cmd.parse(argc, argv);

    // Set message level based on command line value (or default)
    etdc::dbglev_fn( message_level );

    // To daemonize or not to daemonize, that is the question.
    // If we do, we do that by replacing the streambuf of std::cerr by one
    // that actually stuffs stuff into syslog.
    auto                oldStreamBuf = etdc::empty_streamsaver_for_stream(std::cerr);
    const bool          daemonize    = !cmd.get<bool>("f");

    // Drop privileges + assert that after that we are NOT root!
    // Note: the command line parser has already validated that this is
    //       not a nullptr; even the default gets tested against that
    // Note: setresuid(2) is only available on linux (or glibc)
    //       even though it is the preferred method, I guess we
    //       stick to POSIX setuid(2) 
    struct passwd* run_as_ptr = cmd.get<struct passwd*>("run-as");
 
    ETDCASSERT(::setgid(run_as_ptr->pw_gid)==0, "setgid() failed - " << etdc::strerror(errno));
    ETDCASSERT(::setuid(run_as_ptr->pw_uid)==0, "setuid() failed - " << etdc::strerror(errno));
    ETDCASSERT(::getuid() && ::geteuid() && ::getgid() && ::getegid(),
               "Not all privileges were dropped; some rootage is still left!");

    // Oh dear.
    if( daemonize ) {
        // We replace std::cerr's streambuf so from this moment on all
        // output goes to syslog - we are, after all, daemonizing
        oldStreamBuf = std::move(etdc::redirect_to_syslog(std::cerr, argv[0]));

        do_daemonize();
    }

    // Fire up the signal thread. 
    std::promise<int>   killSigPromise;
    std::future<int>    killSigFuture = killSigPromise.get_future();

    etdc::thread(signal_thread, signallist_type{{SIGHUP, SIGINT, SIGTERM, SIGSEGV}}, std::ref(killSigPromise)).detach();

    // Start threads for the command+data servers
    etdc::etd_state            serverState;
    const string2socket_type_m mk_cmd ( port(4004), sockopts );
    const string2socket_type_m mk_data( port(8008), sockopts );

    // data servers first such that the command servers know which data ports are available
    for(auto&& datasrv: cmd.get<std::list<std::string>>("data")) {
        auto srv = mk_data( datasrv );
        // Append the data server to the list of possible data servers
        serverState.dataaddrs.push_back( srv->getsockname(srv->__m_fd) );
        serverState.add_thread(&data_server_thread<SIGUSR2>, srv, std::ref(serverState));
    }

    for(auto&& cmdsrv: cmd.get<std::list<std::string>>("command"))
        serverState.add_thread(&command_server_thread<SIGUSR1>, mk_cmd(cmdsrv), std::ref(serverState));

    // Now just wait ..
    killSigFuture.wait();
    try {
        ETDCDEBUG(-1, "main: terminating because of signal#" << killSigFuture.get() << endl);
    }
    catch( std::exception const& e ) {
        ETDCDEBUG(-1, "main: Caught exception " << e.what() << endl);
    }
    // Before starting to process cancellations, set the cancel flag
    std::atomic_store(&serverState.cancelled, true);

    for(auto& cancel: serverState.cancellations)
        cancel();

    // Now wait for all of them to finish?
    ETDCDEBUG(1, "main: terminating." << endl);
    return 0;
}



// New approach to command_server: there's just one (1) thread function:
//   1. do blocking accept
//   2. if fd accepted - spawn new thread with self state
//   3. thread falls through to handle accepted client
template <int KillSignal>
void command_server_thread(etdc::etdc_fdptr pServer, etdc::etd_state& shared_state) {
    // First things first: push ourselves on the list of cancellations
    // But we'll unblock a signal for that such that we can let the
    // cancellation function send a signal to us :D
    pthread_t                       thisThread = ::pthread_self();
    etdc::UnBlock                   s({KillSignal});
    etdc::etdc_fdptr                pClient{ pServer };
    etdc::cancellist_type::iterator ourCancellation;

    etdc::install_handler(dummy_signal_handler, {KillSignal});

    {
        // used scoped lock to add ourselves to the list of cancellations
        etdc::scoped_lock lk(shared_state.lock);
        ourCancellation = shared_state.cancellations.insert( shared_state.cancellations.end(),
                 [&](void) {
                    // Atomically load the file descriptor we need to cancel
                    etdc::etdc_fdptr  myFD = std::atomic_load(&pClient);

                    ETDCDEBUG(2, "Cancellation fn/signalling thread for command fd=" << myFD->__m_fd << std::endl);
                    myFD->close(myFD->__m_fd);
                    ::pthread_kill(thisThread, KillSignal); }
               );
    }

    try {
        // Now we can get on with our lives
        if( !std::atomic_load(&shared_state.cancelled) )
            std::atomic_store(&pClient, pServer->accept(pServer->__m_fd));

        // OK we accepted a client. Now spawn a new acceptor - unless we're calling it a day
        if( !std::atomic_load(&shared_state.cancelled) )
            shared_state.add_thread(&command_server_thread<KillSignal>, pServer, std::ref(shared_state));

        if( !pClient )
            throw std::runtime_error("No incoming command client?!");

        // Now we fall through handling the client
        auto peernm = pClient->getpeername(pClient->__m_fd);
        ETDCDEBUG(2, "Incoming COMMAND from " << peernm << " [local " << pClient->getsockname(pClient->__m_fd) << "]" << endl);

        // Command sockets typically do small messages so we set tcp_nodelay
        // (if the protocol is TCP-like that is!)
        if( get_protocol(peernm).find("tcp")!=std::string::npos )
            etdc::setsockopt(pClient->__m_fd, etdc::tcp_nodelay{true});

        dbgMap[get_protocol(peernm)](pClient, "client"); 

        // Fall into ETDServerWrapper
        etdc::ETDServerWrapper(pClient, std::ref(shared_state));
    }
    catch( std::exception const& e ) {
        ETDCDEBUG(1, "command server thread got exception: " << e.what() << std::endl);
    }
    catch( ... ) {
        ETDCDEBUG(1, "command server thread got unknown exception" << std::endl);
    }
    if( !std::atomic_load(&shared_state.cancelled) ) {
        etdc::scoped_lock  lk(shared_state.lock);
        shared_state.cancellations.erase( ourCancellation );
    }
    ETDCDEBUG(1, "command server thread terminated" << endl);
    return;
}

// We repeat for data_server threads
template <int KillSignal>
void data_server_thread(etdc::etdc_fdptr pServer, etdc::etd_state& shared_state) {
    // First things first: push ourselves on the list of cancellations
    // But we'll unblock a signal for that such that we can let the
    // cancellation function send a signal to us :D
    pthread_t                       thisThread = ::pthread_self();
    etdc::UnBlock                   s({KillSignal});
    etdc::etdc_fdptr                pClient{ pServer };
    etdc::cancellist_type::iterator ourCancellation;

    etdc::install_handler(dummy_signal_handler, {KillSignal});

    {
        // used scoped lock to add ourselves to the list of cancellations
        etdc::scoped_lock lk(shared_state.lock);
        ourCancellation = shared_state.cancellations.insert( shared_state.cancellations.end(),
                // cancellation function void(void):
                [&](void) {
                    // Atomically load the shared pointer
                    // http://en.cppreference.com/w/cpp/memory/shared_ptr/atomic
                    etdc::etdc_fdptr  myFD = std::atomic_load(&pClient);

                    ETDCDEBUG(2, "Cancellation fn/signalling thread for data fd=" << myFD->__m_fd << std::endl);
                    myFD->close(myFD->__m_fd);
                    ::pthread_kill(thisThread, KillSignal); }
            );
    }

    try {
        // Now we can get on with our lives - atomically store the client's
        // file descriptor as soon as accept() returns
        if( !std::atomic_load(&shared_state.cancelled) )
            std::atomic_store(&pClient, pServer->accept(pServer->__m_fd));

        // OK we accepted a client. Now spawn a new acceptor - unless we're calling it a day
        if( !std::atomic_load(&shared_state.cancelled) )
            shared_state.add_thread(&data_server_thread<KillSignal>, pServer, std::ref(shared_state));

        if( !pClient )
            throw std::runtime_error("No incoming data client?!");
        // Now we fall through handling the client
        auto peernm = pClient->getpeername(pClient->__m_fd);
        ETDCDEBUG(2, "Incoming DATA from " << peernm << " [local " << pClient->getsockname(pClient->__m_fd) << "]" << endl);

        // This is data connection so let's set a big sokkitbuffer
        // Do not *assert* it - e.g. on Mac OSX (and maybe other *BSDs)
        // asking for SO_RCVBUF > maximum will /fail/ and that's not our
        // intent. We'd *like* to have a bigr 'n bettr SO_RCVBUF if you
        // pretty please with sugar on top. But if we can't have it then
        // that's not an error.
        // Note: for UDT data channels we have already set RCVBUF
        //if( get_protocol(peernm).find("tcp")!=std::string::npos )
        //    etdc::setsockopt(pClient->__m_fd, etdc::so_rcvbuf{32*1024*1024}, etdc::so_sndbuf{32*1024*1024});
#if 0
        else
            // udt
            etdc::setsockopt(pClient->__m_fd, etdc::udt_sndbuf{32*1024*1024}, etdc::udt_rcvbuf{32*1024*1024},
                                              etdc::udp_sndbuf{32*1024*1024}, etdc::udp_rcvbuf{32*1024*1024});
#endif
        dbgMap[get_protocol(peernm)](pClient, "client");
        etdc::ETDDataServer(pClient, std::ref(shared_state));
    }
    catch( std::exception const& e ) {
        ETDCDEBUG(1, "data server thread got exception: " << e.what() << std::endl);
    }
    catch( ... ) {
        ETDCDEBUG(1, "data server thread got unknown exception" << std::endl);
    }
    // Deregister our cancellation - only if we weren't being cancelled.
    if( !std::atomic_load(&shared_state.cancelled) ) {
        etdc::scoped_lock  lk(shared_state.lock);
        shared_state.cancellations.erase( ourCancellation );
    }
    ETDCDEBUG(1, "data server thread terminated" << endl);
    return;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
//
//   Daemonize as per section 13.3 in Stevens' and Rago's
//   "Advanced Programming in the UNIX Environment"
//
//   Mods:
//   1.) do not do the sigaction for SIGHUP - we already made main()
//       terminate on reception of SIGHUP, SIGTERM, SIGSTOP and SIGSEGV
//
///////////////////////////////////////////////////////////////////////////////////////////////////
void do_daemonize( void ) {
    pid_t         pid;
    struct rlimit rl;

    // Clear file creation mask - no need to do any assertions
    ::umask( 0 );

    // Get max. number of file descriptors
    ETDCASSERT(::getrlimit(RLIMIT_NOFILE, &rl)==0, "Failed to get max number of file descriptors - " << etdc::strerror(errno));
    // first fork
    ETDCASSERT((pid=::fork())>=0, "Failed to fork(1) - " << etdc::strerror(errno));
    // parent exits succesfully
    if( pid>0 )
        ::exit(0);
    // child becomes session leader
    ETDCASSERT(::setsid()!=static_cast<pid_t>(-1), "Failed to become session leader - " << etdc::strerror(errno));
    // here Stevens et al ignore SIGHUP which we don't [see Mod 2. above]
    // ...
    // 2nd fork
    ETDCASSERT((pid=::fork())>=0, "Failed to fork(2) - " << etdc::strerror(errno));
    // parent exits succesfully
    if( pid>0 )
        ::exit(0);
    // child continues
    ETDCASSERT(::chdir("/")==0, "Failed to change directory to '/' - " << etdc::strerror(errno));

    // close all file descriptors
    if( rl.rlim_max==RLIM_INFINITY ) {
        const long scopenmax = ::sysconf(_SC_OPEN_MAX);
        // if sysconf returns -1 it could mean either of two things:
        // 1) failure
        // 2) still no known limit
        // either way, we still don't know how many file descriptors to
        // close ...
        rl.rlim_max = (scopenmax==-1) ? 1024 : scopenmax;
    }
    // do not close stderr - we've redirected that to syslog
    for(decltype(rl.rlim_max) i = 0; i<rl.rlim_max; i++)
        if( i!=2 )
            ::close( (int)i );
    // Getting there ...
    // attach stdin, stdout  to /dev/null
    int fd0, fd1;

    fd0 = ::open("/dev/null", O_RDWR);
    fd1 = ::dup(0);
    ETDCASSERT(fd0==0 && fd1==1, "Something went wrong attaching stdin, stdout to devnull");
}
