// etransfer daemon state object (shared)
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
#include <algorithm>
#include <functional>


namespace etdc {

    namespace gory_detail {

        //using os_flag_type  = decltype(O_RDONLY);

#ifdef O_LARGEFILE
    #define LARGEFILEFLAG O_LARGEFILE
#else
    #define LARGEFILEFLAG (decltype(O_RDONLY)(0))
#endif

        //constexpr os_flag_type large_file = 0;
        //constexpr os_flag_type used_flags = O_RDONLY | O_CREAT | O_EXCL | O_WRONLY | O_APPEND | O_TRUNC | large_file;

        // Now we need to invent a bit that is not one of the flags we
        // actually use
        //static os_flag_type find_free_flag(os_flag_type in_use) {
        //const auto IGNORE_EXISTING = std::max({O_RDONLY, O_CREAT, O_EXCL, O_WRONLY, O_APPEND, O_TRUNC, LARGEFILEFLAG}) * 2;
    }

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
        etdc::etdc_fdptr            fd;
        const openmode_type         openMode;
        std::unique_ptr<std::mutex> lockPtr;

        // we cannot be copied or default constructed! (because of our unique_ptr)
        transferprops_type()                          = delete;
        transferprops_type(transferprops_type const&) = delete;

        // We could be moved, which we accomplish by swapping our default
        // constructed elements with other's non-default ones, we assume
        transferprops_type(transferprops_type&& other): openMode( other.openMode ) {
            std::swap(path,    other.path);
            std::swap(fd,      other.fd);
            std::swap(lockPtr, other.lockPtr);
        }

        transferprops_type(etdc::etdc_fdptr efd, std::string const& p, openmode_type om):
            path(p), fd(efd), openMode(om), lockPtr(new std::mutex())
        {}
    }; 

    using cancel_fn         = std::function<void(void)>;
    using cancellist_type   = std::list<cancel_fn>;
    using scoped_lock       = std::lock_guard<std::mutex>;
    using threadlist_type   = std::list<std::thread>;
    using dataaddrlist_type = std::list<etdc::sockname_type>;
    using transfermap_type  = std::map<etdc::uuid_type, transferprops_type>;

    // Keep global server state
    struct etd_state {
        std::mutex           lock;
        cancellist_type      cancellations;
        threadlist_type      threads;
        transfermap_type     transfers;
        std::atomic<bool>    cancelled;
        dataaddrlist_type    dataaddrs;

        etd_state() : cancelled{ false }
        {}

        // To prevent deadlock we first construct the thread 
        // and after the fact, grab a lock and modify the shared state
        template <typename... Args>
        void add_thread(Args&&... args) {
            scoped_lock lk( lock );
            // by using etdc::thread we make sure that the started thread 
            // has all signals blocked
            threads.emplace_back(etdc::thread(std::forward<Args>(args)...));
        }
    };
}

#endif
