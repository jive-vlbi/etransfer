// etransfer daemon state object (shared)
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
#ifndef ETDC_ETD_STATE_H
#define ETDC_ETD_STATE_H

// Own headers
#include <etdc_fd.h>
#include <etdc_uuid.h>
#include <etdc_thread.h>
#include <utilities.h>
#include <etdc_stringutil.h>

// Standard C++ headers
#include <map>
#include <list>
#include <mutex>
#include <memory>
#include <thread>
#include <utility>
#include <iostream>
#include <exception>
#include <algorithm>
#include <functional>
#include <condition_variable>


namespace etdc {

    // When requesting file access we want to restrict the options to this
    enum class openmode_type : int {
        // New: target file may not exist yet
        New          = O_WRONLY | O_CREAT | O_EXCL,
        // OverWrite: create if not exist, truncate if it does
        OverWrite    = O_WRONLY | O_TRUNC | O_CREAT,
        // Append: create if not exist, append to if it does
        Resume       = O_WRONLY | O_CREAT | O_APPEND,
        // Read: ... (your guess)
        Read         = O_RDONLY,
        // SkipExisting: (bits are complement of Resume) 
        //    creates if not exists, open for appending (which we won't) if
        //    it does
        SkipExisting = ~(O_WRONLY | O_CREAT | O_APPEND) 
    };


    static const std::map<openmode_type, std::string> om2string{ 
        {openmode_type::New,    "New"},    {openmode_type::OverWrite, "OverWrite"},
        {openmode_type::Resume, "Resume"}, {openmode_type::Read,      "Read"},
        {openmode_type::SkipExisting, "SkipExisting"} };

    template <typename... Traits>
    std::basic_ostream<Traits...>& operator<<(std::basic_ostream<Traits...>& os, openmode_type const& om) {
        auto const ptr = om2string.find( om );
        return os << (ptr==std::end(om2string) ? "<invalid openmode_type>" : ptr->second);
    }

    template <typename... Traits>
    std::basic_istream<Traits...>& operator>>(std::basic_istream<Traits...>& is, openmode_type& om) {
        std::string  om_s;
        // read string from input stream
        is >> om_s;
        // and check if it's something we recognize
        auto const ptr = std::find_if(std::begin(om2string), std::end(om2string),
                                      std::bind([&](std::string const& s) { return etdc::stricmp(om_s, s); },
                                                std::bind(etdc::snd_type(), std::placeholders::_1)));
        return (ptr==std::end(om2string) ? om = static_cast<openmode_type>(-1) : om = ptr->first), is;
    }


    // We keep per-transfer properties in here
    struct transferprops_type {
        std::string                 path;
        etdc::etdc_fdptr            fd, data_fd;
        const openmode_type         openMode;
        std::mutex                  xfer_lock;
        std::atomic<bool>           cancelled;

        // we cannot be copied or default constructed! (because of our unique_ptr)
        transferprops_type()                          = delete;

        transferprops_type(etdc::etdc_fdptr efd, std::string const& p, openmode_type om):
            path(p), fd(efd), openMode(om)
        { cancelled.store( false ); }
    }; 

    using cancel_fn         = std::function<void(void)>;
    using cancellist_type   = std::list<cancel_fn>;
    using scoped_lock       = std::lock_guard<std::mutex>;
    using threadlist_type   = std::list<std::thread>;
    using dataaddrlist_type = std::list<etdc::sockname_type>;
    using transfermap_type  = std::map<etdc::uuid_type, std::unique_ptr<transferprops_type>>;

    // Keep global server state
    struct etd_state {
        size_t                  bufSize{ 32*1024*1024 };
        std::mutex              lock;
        unsigned int            n_threads;
        etdc::mss_type          udtMSS{ 0/*1500*/ };
        etdc::max_bw_type       udtMaxBW{ 0/*-1*/ };
        cancellist_type         cancellations;
        transfermap_type        transfers;
        std::atomic<bool>       cancelled;
        dataaddrlist_type       dataaddrs;
        std::condition_variable condition;

        etd_state() : n_threads{ 0 }, cancelled{ false }
        {}


        // To prevent deadlock we first construct the thread 
        // and after the fact, grab a lock and modify the shared state
        template <typename F, typename... Args>
        void add_thread(F&& f, Args&&... args) {
            std::unique_lock<std::mutex> lk( lock );
            // by using etdc::thread we make sure that the started thread 
            // has all signals blocked
            if( !std::atomic_load(&cancelled) ) {
                etdc::thread(etd_state_thread_s(), std::ref(const_cast<etd_state&>(*this)), std::forward<F>(f), std::forward<Args>(args)...).detach();
                n_threads++;
            }
            condition.notify_all();
        }

        ~etd_state() {
            ETDCDEBUG(4, "~etd_state/need to wait for " << n_threads << " threads" << std::endl);
            std::unique_lock<std::mutex>  lk( lock );
            // Wait for n_threads to reach 0
            condition.wait(lk, [this](){ return n_threads==0; } );
        }

        private:
            // This struct will act as templated function call functor.
            // We wrap the *actual* thread function inside this function,
            // which will catch the exceptions and handle the bookkeeping
            // no matter how the actual function chooses to exit.
            struct etd_state_thread_s {
                template <typename Callable, typename... Args>
                void operator()(etd_state& shared_state, Callable&& callable, Args&&... args) const {
                    std::exception_ptr eptr;
                    // Attempt to execute the function with the arguments
                    // Catch any exceptions and then automatically decrement the
                    // thread counter
                    try {
                        std::forward<Callable>(callable)( std::forward<Args>(args)... );
                    }
                    catch( ... ) {
                        // Capture exception
                        eptr = std::current_exception();
                    }
                    // Do bookkeeping - the thread is about to exit
                    std::unique_lock<std::mutex> lk( shared_state.lock );
                    shared_state.n_threads--;
                    shared_state.condition.notify_all();

                    // And retrow if necessary
                    if( eptr )
                        std::rethrow_exception(eptr);
                }
            };
    };
}

#endif
