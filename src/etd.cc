// etransfer server program/etdc=etransfer daemon + client
#include <version.h>
#include <etdc_fd.h>
#include <reentrant.h>
#include <etdc_thread.h>
#include <etdc_debug.h>
#include <etdc_etd_state.h>
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

// Some handy shorthands
using namespace std;
namespace AP = argparse;

using fdlist_type     = std::list<etdc::etdc_fdptr>;
using signallist_type = std::vector<int>;

// Introduce a readable overload for a fdptr
HUMANREADABLE(etdc::etdc_fdptr, "address")

struct string2socket_type:
    // we pretend to be a converter!
    public AP::detail::conversion_t {

    // We let the command line parser validate the format on the command
    // line for us, such that we already know the string adheres to this
    // format (grmbl - 'static constexpr std::string fmt = "...." don't work
    // so we must initialize outside the definition FFS)
    static const std::string fmt;

    // to be a converter we must have "void (<target type>&, std::string const&) const"
    // The string is guaranteed to match the regex above :-)
    void operator()(etdc::etdc_fdptr& fd, std::string const& s) const {
        std::vector<std::string>    proto;
        std::vector<std::string>    addr;

        etdc::string_split(s,        '/', std::back_inserter(proto), false);
        etdc::string_split(proto[1], '@', std::back_inserter(addr),  false);

        fd = mk_server(proto[0], port(addr[0]), etdc::udt_rcvbuf{2*1024*1024},
                       etdc::host_type(addr.size()>1 ? addr[1] : std::string()),
                       etdc::so_rcvbuf{2*1024}, etdc::blocking_type{true});
        return;
    }
};
const std::string string2socket_type::fmt{"(tcp|udt)6?/[0-9]+(@[-\\.a-zA-Z0-9]+)?"};


////////////////////////////////////////////////////////////////////////////////////
//
// Forward declarations &cet
//
////////////////////////////////////////////////////////////////////////////////////
template <int> void command_server_thread(etdc::etdc_fdptr fd, etdc::etd_state&);
template <int> void command_client_thread(etdc::etdc_fdptr fd, etdc::etd_state&);

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

    ETDCDEBUG(-1, "sigwaiterthread: enter wait phase" << endl);
    // ... and wait for any of them to happen
    ::sigwait(&sset, &received);
    ETDCDEBUG(-1, "sigwaiterthread: got signal " << received << endl);
    promise.set_value( received );
}


int main(int argc, char const*const*const argv) {
    // First things first: block ALL signals
    etdc::BlockAll      ba;
    // Let's set up the command line parsing
    int                 message_level = 0;
    fdlist_type         commandServers, dataServers;
    AP::ArgumentParser  cmd( AP::version( buildinfo() ),
                             AP::docstring("'ftp' like etransfer server daemon, to be used with etransfer client for "
                                           "high speed file/directory transfers.") );

    // What does our command line look like?
    //
    // <prog> --control <address> --data <address>
    //        --acl <access control list>
    //        [-h] [--help] [--version]
    //        [-m <int>]
    //
    // <address> = [udt|tcp]:<port>[:<local IP>]
    //             (if <local IP> not given, listen on all interfaces)

    cmd.add( AP::long_name("help"), AP::print_help(),
             AP::docstring("Print full help and exit succesfully") );
    cmd.add( AP::short_name('h'), AP::print_usage(),
             AP::docstring("Print short usage and exit succesfully") );
    cmd.add( AP::long_name("version"), AP::print_version(),
             AP::docstring("Print version and exit succesfully") );

    // message level: higher = more verbose
    cmd.add( AP::store_into(message_level), AP::short_name('m'),
             AP::maximum_value(5), AP::minimum_value(-1),
             AP::docstring("Message level - higher = more output") );

    // command servers; we require at least one of 'm
    cmd.add( AP::collect_into(commandServers), AP::long_name("command"),
             // Make the system automatically convert the address string into a real sokkit
             string2socket_type(),
             // Constraints on the number + form of the argument
             AP::at_least(1), AP::match(string2socket_type::fmt),
             // And some useful info
             AP::docstring("Listen on this(these) address(es) for incoming client control connections. "
                           "At least the protocol and port number must be specified. A local "
                           "interface address may be given (in host or ip format) to restrict "
                           "where clients can connect to. The default is to listen on all interfaces.") );

    // data servers; we require at least one of those
    cmd.add( AP::collect_into(dataServers), AP::long_name("data"),
             // Make the system automatically convert the address string into a real sokkit
             string2socket_type(),
             // Constraints on the number + form of the argument
             AP::at_least(1), AP::match(string2socket_type::fmt),
             // And some useful info
             AP::docstring("Listen on this(these) address(es) for incoming client data connections. "
                           "At least the protocol and port number must be specified. A local "
                           "interface address may be given (in host or ip format) to restrict "
                           "where clients can connect to. The default is to listen on all interfaces.") );

    // OK Let's check that mother
    cmd.parse(argc, argv);

    // Set message level based on command line value (or default)
    etdc::dbglev_fn( message_level );

    // Fire up the signal thread. 
    std::promise<int>   killSigPromise;
    std::future<int>    killSigFuture = killSigPromise.get_future();

    etdc::thread(signal_thread, signallist_type{{SIGHUP, SIGINT, SIGTERM, SIGSEGV}}, std::ref(killSigPromise)).detach();

    // Start threads for the command servers
    etdc::etd_state         serverState;
    std::list<std::thread>  tids;

    for(auto&& srv: commandServers)
        // etdc::thread starts thread with all sigs blocked!
        tids.emplace_back(etdc::thread(&command_server_thread<SIGUSR1>, srv, std::ref(serverState)));

    // Now just wait ..
    killSigFuture.wait();
    try {
        ETDCDEBUG(-1, "main: terminating because of signal#" << killSigFuture.get() << endl);
    }
    catch( std::exception const& e ) {
        ETDCDEBUG(-1, "main: Caught exception " << e.what() << endl);
    }

    // OK. Process all cancellations and wait for them threads to finish
    etdc::scoped_lock  lk(serverState.lock);

    for(auto const& cancel: serverState.cancellations)
        cancel();

    // Now wait for all of them to finish?
    ETDCDEBUG(3, "main: joining server threads" << endl);
    for(auto&& tid: tids) {
        tid.join();
        ETDCDEBUG(4, "     * joined" << endl);
    }
    ETDCDEBUG(1, "main: terminating." << endl);
    return 0;
}


static std::map<std::string, std::function<void(etdc::etdc_fdptr, std::string const&)>>
   dbgMap = {{"udt", [](etdc::etdc_fdptr pSok, std::string const& s) {
                            etdc::udt_rcvbuf  rcv;
                            etdc::getsockopt(pSok->__m_fd, rcv);
                            ETDCDEBUG(1, s << "/UDT rcvbuf = " << rcv << endl);
                        }},
              {"udt6", [](etdc::etdc_fdptr pSok, std::string const& s) {
                            etdc::udt_rcvbuf  rcv;
                            etdc::getsockopt(pSok->__m_fd, rcv);
                            ETDCDEBUG(1, s << "/UDT6 rcvbuf = " << rcv << endl);
                        }},
              {"tcp", [](etdc::etdc_fdptr pSok, std::string const& s) {
                            etdc::so_rcvbuf  rcv;
                            etdc::getsockopt(pSok->__m_fd, rcv);
                            ETDCDEBUG(1, s << "/TCP rcvbuf = " << rcv << endl);
                        }},
              {"tcp6", [](etdc::etdc_fdptr pSok, std::string const& s) {
                            etdc::so_rcvbuf  rcv;
                            etdc::ipv6_only  ipv6;
                            etdc::getsockopt(pSok->__m_fd, rcv, ipv6);
                            ETDCDEBUG(1, s << "/TCP6 rcvbuf = " << rcv << ", ipv6 only = " << ipv6 << endl);
                        }}
            };


template <int KillSignal>
void command_server_thread(etdc::etdc_fdptr pServer, etdc::etd_state& shared_state) {
    // First things first: push ourselves on the list of cancellations
    // But we'll unblock a signal for that such that we can let the
    // cancellation function send a signal to us :D
    pthread_t                       thisThread = ::pthread_self();
    etdc::UnBlock                   s({KillSignal});
    std::atomic<bool>               beingCancelled{ false };
    std::list<std::thread>          tids;
    etdc::cancellist_type::iterator ourCancellation;

    etdc::install_handler(dummy_signal_handler, {KillSignal});

    {
        // used scoped lock to add ourselves to the list of cancellations
        etdc::scoped_lock lk(shared_state.lock);
        ourCancellation = shared_state.cancellations.insert( shared_state.cancellations.end(),
                                                 [&](void) {
                                                    ETDCDEBUG(2, "Cancellation fn/signalling thread for fd=" << pServer->__m_fd << std::endl);
                                                    // Inform the thread it is being cancelled
                                                    std::atomic_store_explicit(&beingCancelled, true, std::memory_order_release);
                                                    pServer->close(pServer->__m_fd);
                                                    ::pthread_kill(thisThread, KillSignal); }
                                                );
    }

    try {
        // Now we can get on with our lives
        auto socknm =  pServer->getsockname(pServer->__m_fd);
        ETDCDEBUG(2, "etd: server is-at " << socknm << endl);
        dbgMap[get_protocol(socknm)](pServer, "server"); 

        // Start accepting clients!
        while( true ) {
            auto clnt = pServer->accept(pServer->__m_fd);
            if( !clnt )
                throw std::runtime_error("No incoming client?!");

            // And start a client thread for the incoming connection
            tids.emplace_back(etdc::thread(&command_client_thread<SIGUSR2>, clnt, std::ref(shared_state)));
        }

    }
    catch( std::exception const& e ) {
        ETDCDEBUG(-1, "command server thread got exception: " << e.what() << std::endl);
    }
    catch( ... ) {
        ETDCDEBUG(-1, "command server thread got unknown exception" << std::endl);
    }

    // Now wait for all spawned clients to finish?
    ETDCDEBUG(3, "command_server_thread[" << thisThread << "]: joining client threads" << endl);
    for(auto&& tid: tids) {
        tid.join();
        ETDCDEBUG(4, "   [" << thisThread << "]: joined!" << endl);
    }

    ETDCDEBUG(1, "command server thread terminating" << endl);
    // Deregister our cancellation - only if we weren't being cancelled.
    // We only do that if we decided ourselves to call it a day
    const bool cancelled = std::atomic_load_explicit(&beingCancelled, std::memory_order_acquire);

    if( !cancelled ) {
        etdc::scoped_lock  lk(shared_state.lock);
        shared_state.cancellations.erase( ourCancellation );
    }
    return;
}

template <int KillSignal>
void command_client_thread(etdc::etdc_fdptr clnt, etdc::etd_state& shared_state) {
    pthread_t                       thisThread = ::pthread_self();
    etdc::UnBlock                   s({KillSignal});
    std::atomic<bool>               beingCancelled{ false };
    etdc::cancellist_type::iterator ourCancellation;

    etdc::install_handler(dummy_signal_handler, {KillSignal});

    {
        // used scoped lock to add ourselves to the list of cancellations
        etdc::scoped_lock lk(shared_state.lock);
        ourCancellation = shared_state.cancellations.insert( shared_state.cancellations.end(),
                                                 [&](void) {
                                                    ETDCDEBUG(2, "Cancellation fn/signalling client thread for fd=" << clnt->__m_fd << std::endl);
                                                    // Inform the thread it is being cancelled
                                                    std::atomic_store_explicit(&beingCancelled, true, std::memory_order_release);
                                                    clnt->close(clnt->__m_fd);
                                                    ::pthread_kill(thisThread, KillSignal); }
                                                );
    }

    try {
        auto peernm = clnt->getpeername(clnt->__m_fd);
        ETDCDEBUG(2, "Incoming from " << peernm << " [local " << clnt->getsockname(clnt->__m_fd) << "]" << endl);

        dbgMap[get_protocol(peernm)](clnt, "client"); 
        
        char    buf[1024];
        ssize_t n = 0, r;
        while( (r = clnt->read(clnt->__m_fd, buf, sizeof(buf)))>0 )
            n += r, cout << "." << flush;
        ETDCDEBUG(3, "OK - client sent " << n << " bytes" << endl);
    }
    catch( std::exception const& e ) {
        ETDCDEBUG(-1, "command client thread got exception: " << e.what() << std::endl);
    }
    catch( ... ) {
        ETDCDEBUG(-1, "command client thread got unknown exception" << std::endl);
    }

    ETDCDEBUG(1, "command client thread terminating" << endl);

    // Deregister our cancellation - only if we weren't being cancelled.
    // We only do that if we decided ourselves to call it a day
    const bool cancelled = std::atomic_load_explicit(&beingCancelled, std::memory_order_acquire);

    if( !cancelled ) {
        etdc::scoped_lock  lk(shared_state.lock);
        shared_state.cancellations.erase( ourCancellation );
    }
    return;
}

#if 0
auto pServer = mk_server(proto, port(8008), etdc::udt_rcvbuf{2*1024*1024}, etdc::so_rcvbuf{2*1024}, etdc::blocking_type{true});
#endif


#if 0
#include <etdc_signal.h>
#include <etdc_thread.h>
//#include <keywordargs.h>
#include <stdkeys.h>

namespace stdkeys = etdc::stdkeys;

#include <list>
#include <utility>
#include <iostream>
#endif
