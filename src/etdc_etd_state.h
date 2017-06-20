// etransfer daemon state object (shared)
#ifndef ETDC_ETD_STATE_H
#define ETDC_ETD_STATE_H

// Own headers
#include <etdc_fd.h>
#include <etdc_uuid.h>
#include <etdc_thread.h>

// Standard C++ headers
#include <map>
#include <list>
#include <mutex>
#include <memory>
#include <thread>
#include <utility>
#include <functional>


namespace etdc {

    // When requesting file access we want to restrict the options to this
    enum class openmode_type : int {
        omNew    = O_WRONLY | O_CREAT | O_EXCL,   // new-may-not-exist
        omWrite  = O_WRONLY | O_TRUNC | O_CREAT,  // may-or-may-not-exist, will truncate
        omAppend = O_WRONLY | O_CREAT | O_APPEND, // may-or-may-not-exist, if exist append at end
        omRead   = O_RDONLY
    };


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
