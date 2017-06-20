// macros for debugging + function(s) for levels and redirecting to syslog
// Copyright (C) 2007-2017 Harro Verkouter
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
// Author:  Harro Verkouter - verkouter@jive.nl
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#ifndef ETDC_DEBUG_H
#define ETDC_DEBUG_H

#include <mutex>
#include <atomic>
#include <string>
#include <memory>
#include <sstream>
#include <iostream>
#include <streambuf>
#include <functional>

#include <ctime>
#include <cstdio>
#include <syslog.h>
#include <sys/time.h>

#ifdef __GNUC__
#define ETDCDBG_FUNC "[" << __PRETTY_FUNCTION__ << "] "
#else
#define ETDCDBG_FUNC ""
#endif

// The ETDCDEBUG(...) macro is defined at the end of this file - it is the
// vehicle for most output


// Usage of the code below for redirecting a basic_ostream to syslog.
//
//  etdc::redirect_to_syslog(ostream& os, std::string const& ident, ...) 
//
// redirects the output of the stream to syslog with ident as
// identification for the lifetime of the returned object - behind the
// scenes it returns a std::unique_ptr who does the majik; after the object
// goes out of scope, the old state of the stream is restored.
//
// The 'redirect_to_syslog(...)' function call may take more arguments, which
// will be forwarded to the syslog_streambuf constructor.
//
// Thus:
//
//  void foo( ... ) {
//      // assume std:cerr is in some state; let's say standard state:
//      // output to terminal
//      std::cerr << "to terminal" << std::endl;
//      // Introduce some scoping
//      {
//          // here we save the current state of std::cerr and change it
//          // to make any output go to syslog. Capture the returnvalue for
//          // its lifetime determines for how long the output will be
//          // redirected to syslog
//          auto redir = std::move( etdc::redirect_to_syslog(std::cerr, ...) );
//      
//          std::cerr << "to syslog" << std::endl;
//          // 'redir' goes out of scope here and puts back old state of std::cerr
//      }
//      // so this output goes to the terminal once more
//      std::cerr << "and back to terminal" << std::endl;
//  }
//
//
//  If you want to conditionally redirect a stream's output to syslog the
//  scoped variable must be declared outside the called functions.
//  There is a helper: "etdc::empty_streamsaver_for_stream(ostream&)"
//  which can later be reset to an actual saved stream state:
//
//
// int main(int, char const*const*const argv) {
//    // Create an empty streamsaver for the stream to be redirected
//    auto oldBuf = etdc::empty_streamsaver_for_stream(std::cerr);
//
//    ...
//
//    // Now we can (conditionally) redirect the stream to syslog:
//    // NOTE: the returnvalue of 'etdc::redirect_to_syslog(...)' HAS
//    //       to be std::move'd into the saving variable
//    if( condition )
//      oldBuf = std::move( etdc::redirect_to_syslog(std::cerr, argv[0]) );
//
//    // (rest of program)
//    while( foo(...) ) 
//       bar(...); 
//
//    // here 'oldbuf' goes out of scope and if it contained a saved stream
//    // state it will put it back, otherwise that's a no-op
//    return 0;
// }
//
namespace etdc {

    namespace detail {
        static std::mutex       __m_iolock{};
        // if msglevel<=dbglevel it is printed
        static std::atomic<int> __m_dbglev{1};
        // if dbglevel>=fnthres_val level => functionnames are printed in DEBUG()
        static std::atomic<int> __m_fnthres{5};

        static std::string timestamp( void ) {
            char           buff[32];
            struct tm      raw_tm;
            struct timeval raw_t1m3;

            ::gettimeofday(&raw_t1m3, NULL);
            ::gmtime_r(&raw_t1m3.tv_sec, &raw_tm);
            ::strftime( buff, sizeof(buff), "%Y-%m-%d %H:%M:%S", &raw_tm );
            ::snprintf( buff + 19, sizeof(buff)-19, ".%02ld: ", (long int)(raw_t1m3.tv_usec / 10000) );
            return buff;
        }
        // stupid trick to shut up compiler warning-cum-error if you include
        // etdc_debug.h but do not use it: you'd get an "unused-function"
        // warning for the static timestamp() function. So by triggering it
        // in here we circumvent that :D
        static const std::string what_time_was_it{ timestamp() };
    } // namespace detail 

    // get current debuglevel
    inline int dbglev_fn( void ) {
        return detail::__m_dbglev.load(std::memory_order_acquire);
    }

    // set current level to 'n', returns previous level
    inline int dbglev_fn( int n ) {
        return std::atomic_exchange_explicit(&detail::__m_dbglev, n, std::memory_order_release);
    }

    // Similar for function name printing threshold
    inline int fnthres_fn( void ) {
        return detail::__m_fnthres.load(std::memory_order_acquire);
    }

    // set current level to 'n', returns previous level
    inline int fnthres_fn( int n ) {
        return std::atomic_exchange_explicit(&detail::__m_fnthres, n, std::memory_order_release);
    }


    namespace detail {
        // Adapted by HV from code & discussions in
        // https://stackoverflow.com/questions/2638654/redirect-c-stdclog-to-syslog-on-unix
        //
        // NOTE: this is not a MT safe implementation. The user is
        //       responsible to make sure that multiple threads to not output to
        //       the same stream at the same time.
        //       The ETDC_DEBUG() macro below does that already so it's safe
        //       to redirect that to syslog using this code
        template <typename... Props>
        class syslog_streambuf:
            public std::basic_streambuf<Props...>
        {
            public:
                // Two defaulted arguments means there /is/ a single-argument
                // c'tor, thus we'd better add 'explicit'
                // See below why we copy the 'ident' argument into a member variable
                explicit syslog_streambuf(std::string const& ident, int logopt = LOG_PID, int facility = LOG_USER):
                    std::basic_streambuf<Props...>(), __m_ident( ident )
                {
                    ::openlog(__m_ident.c_str(), logopt, facility);
                }

                virtual ~syslog_streambuf( void ) noexcept {
                    ::closelog();
                }

            protected:
                using int_type    = typename std::basic_streambuf<Props...>::int_type;
                using traits_type = typename std::basic_streambuf<Props...>::traits_type;

                virtual int sync( void ) {
                    if( !__m_buf.empty() ) {
                        ::syslog(etdc::dbglev_fn()/*__m_priority*/, "%s", __m_buf.c_str());
                        __m_buf.erase();
                        //priority_ = LOG_DEBUG; // default to debug for each message
                    }
                    return 0;
                }
                virtual int_type overflow( int_type ch = traits_type::eof() ) {
                    if(traits_type::eq_int_type(ch, traits_type::eof()))
                        this->sync();
                    else
                        __m_buf += traits_type::to_char_type(ch);
                    return ch;
                }

            private:
                // At this moment we don't honour PRIO just yet - until I figure
                // out how to do that threadsafe! [thinking of per-thread prio +
                // string buffer]
                std::string        __m_buf;

                // Loonix docs say:
                // "The  argument  ident  in the call of openlog() is
                // probably stored as-is. hus, if the string it points to
                // is changed, syslog() may start prepending the changed
                // string, and if the string it points to ceases to exist,
                // the results are undefined.  Most portable is to use a
                // string constant."
                // So we don't use string constant but something close
                // enough to that
                const std::string  __m_ident;
        };


        // Turns out that if we leave the rdbuf() of e.g. std::cerr pointing
        // to a refcounted instance of syslog_streambuf to ensure that the
        // object is properly deleted, on Loonix one gets a SIGSEGV - after
        // the refcounted streambuf goes out of scope (return from main())
        // the object gets deleted (this is OK) but std::cerr then still
        // holds a pointer to the deleted memory and someone calls "flush"
        // on the stream.
        // Short story long: we'd better put back the old streambuf (or
        // replace with nullptr?) if we go out of scope
        // 
        // Our attack vector will be an object that will rebuf a stream
        // such that we can rebuf any stream correctly
        template <typename... Props>
        struct streamsaver_type {
            using ostream_type   = std::basic_ostream<Props...>;
            using streambuf_type = std::basic_streambuf<Props...>;
            using streambuf_ptr  = std::unique_ptr<streambuf_type>;

            streamsaver_type(ostream_type& osref, streambuf_ptr streambuf):
                __m_osref( osref ), __m_streambuf( std::move(streambuf) )
            { 
                __m_oldstreambuf = __m_osref.get().rdbuf( __m_streambuf.get() );
            }

            ~streamsaver_type() {
                __m_osref.get().rdbuf( __m_oldstreambuf );
            }

            private:
                std::reference_wrapper<ostream_type>  __m_osref;
                streambuf_ptr                         __m_streambuf;        
                streambuf_type*                       __m_oldstreambuf; // kept as raw pointer
        };

    } // namespace detail 

    // Helper function to create an empty streamsaver holder
    template <typename... Props,
              typename PTRType = std::unique_ptr<detail::streamsaver_type<Props...>>>
    auto empty_streamsaver_for_stream(std::basic_ostream<Props...>&) -> PTRType {
        return PTRType();
    }

    // main function template - instrument it so that anything sent to
    // stream 'os' ends up in the syslog
    template <typename... Props, typename... Args,
              typename SBType  = detail::syslog_streambuf<Props...>,
              typename SSType  = detail::streamsaver_type<Props...>,
              typename PTRType = std::unique_ptr<SSType>>
    auto redirect_to_syslog(std::basic_ostream<Props...>& os, Args&&... args) -> PTRType {
        return PTRType( new SSType(os, typename SSType::streambuf_ptr(new SBType(std::forward<Args>(args)...))) );
    }
} // namespace etdc


// Prepare the debugstring in a local variable.
// We do that so the amount of time spent holding the lock
// is minimal.
//
// NOTE: ETDC_DEBUG() macro outputs its messaged to std::cerr
//
// NOTE: ETDC_DEBUG() macro is thread-safe and requires no
//       (extra) locking on the stream it is outputting to
//
// NOTE: Using the etdc::redirect_stream_to_syslog() it is possible
//       to, well, redirect std::cerr to syslog. So all messages
//       printed using ETDC_DEBUG() then end up in the syslog 
//
// NOTE: the __m_dbglev atomic is loaded *twice* w/o locking
//       so it would be possible for another thread to change
//       the dbglev between the two loads but that's just bummer
//
#define ETDCDEBUG(a, b) \
    do {\
        if( a<=etdc::detail::__m_dbglev.load(std::memory_order_acquire) ) {\
            std::ostringstream OsS_ZyP;\
            /* could introduce flag for printing time stamp? */ \
            OsS_ZyP << etdc::detail::timestamp();\
            if( etdc::detail::__m_dbglev.load(std::memory_order_acquire)>=etdc::detail::__m_fnthres.load(std::memory_order_acquire) ) \
                OsS_ZyP << ETDCDBG_FUNC; \
            OsS_ZyP << b;\
            std::lock_guard<std::mutex> OsZyp_Lck(etdc::detail::__m_iolock);\
            std::cerr << OsS_ZyP.str();\
        }\
    } while( 0 );

#if 0
#define ETDC_DEBUG(a, b) \
    do {\
        if( a<=etdc::detail::__m_dbglev.load(std::memory_order_acquire) ) {\
            std::ostringstream OsS_ZyP;\
            char t1m3_buff3r[32];\
            struct tm      raw_tm; \
            struct timeval raw_t1m3; \
            ::gettimeofday(&raw_t1m3, NULL); \
            ::gmtime_r(&raw_t1m3.tv_sec, &raw_tm); \
            ::strftime( t1m3_buff3r, sizeof(t1m3_buff3r), "%Y-%m-%d %H:%M:%S", &raw_tm ); \
            ::snprintf( t1m3_buff3r + 19, sizeof(t1m3_buff3r)-19, ".%02ld: ", (long int)(raw_t1m3.tv_usec / 10000) ); \
            OsS_ZyP << t1m3_buff3r;\
            if( etdc::detail::__m_dbglev.load(std::memory_order_acquire)>=etdc::detail::__m_fnthres.load(std::memory_order_acquire) ) \
                OsS_ZyP << ETDCDBG_FUNC; \
            OsS_ZyP << b;\
            std::lock_guard<std::mutex> OsZyp_Lck(etdc::detail::__m_iolock);\
            std::cerr << OsS_ZyP.str();\
        }\
    } while( 0 );
#endif


#endif
