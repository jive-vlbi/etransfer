// etransfer daemon state object (shared)
#include <list>
#include <mutex>
#include <functional>

namespace etdc {

    using cancel_fn       = std::function<void(void)>;
    using cancellist_type = std::list<cancel_fn>;
    using scoped_lock     = std::lock_guard<std::mutex>;

    // Keep global server state
    struct etd_state {
        std::mutex      lock;
        cancellist_type cancellations;
    };
}
