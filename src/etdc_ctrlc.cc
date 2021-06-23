#include <etdc_ctrlc.h>
#include <etdc_assert.h>
#include <etdc_debug.h>
#include <map>
#include <list>
#include <mutex>

namespace etdc {

    // we need some stuff to do & protect our bookkeeping
    namespace ctrlc_detail {

        // An action is combination of function to call and a boolean
        // indicating auto-cleanup-after-handling or not
        using Action    = std::pair<ControlCAction, bool>;
        using ActionMap = std::map<ActionId, Action>;

        static std::mutex   __mtx{};
        static ActionMap    __action_map{};
        static ActionId     __hdl = 0;
    }

    ActionHandle registerCtrlCAction( ControlCAction cca ) {
        std::lock_guard<std::mutex> lk{ ctrlc_detail::__mtx };
        ActionId                    hdl{ ctrlc_detail::__hdl };

        ETDCASSERT( ctrlc_detail::__action_map.insert( std::make_pair(hdl, std::make_pair(cca, false)) ).second,
                    "Failed to insert control-c action into mapping?!" );
        ETDCDEBUG(5, "Inserted control-c action #" << hdl << std::endl);
        ctrlc_detail::__hdl++;
        return ActionHandle{hdl, false};
    } 

    ActionHandle registerAutoCtrlCAction( ControlCAction cca ) {
        std::lock_guard<std::mutex> lk{ ctrlc_detail::__mtx };
        ActionId                    hdl{ ctrlc_detail::__hdl };

        ETDCASSERT( ctrlc_detail::__action_map.insert( std::make_pair(hdl, std::make_pair(cca, true)) ).second,
                    "Failed to insert control-c action into mapping?!" );
        ETDCDEBUG(5, "Inserted auto cleanup control-c action #" << hdl << std::endl);
        ctrlc_detail::__hdl++;
        return ActionHandle{hdl, true};
    } 

    void unregisterCtrlCAction( ActionHandle ah ) {
        std::lock_guard<std::mutex> lk{ ctrlc_detail::__mtx };
        auto                        actionPointer = ctrlc_detail::__action_map.find( ah.first );

        ETDCASSERT( ah.second==true || actionPointer != ctrlc_detail::__action_map.end(),
                    "Failed to find entry for control-c action #" << ah.first );
        if( actionPointer!=ctrlc_detail::__action_map.end() ) {
            ctrlc_detail::__action_map.erase( actionPointer );
            ETDCDEBUG(5, "Removed control-c action #" << actionPointer->first << std::endl);
        }
        return;
    }

#if 0
    // The 
    void unregisterAutoCtrlCAction( ActionHandle ah ) {
        std::lock_guard<std::mutex> lk{ ctrlc_detail::__mtx };
        auto                        actionPointer = ctrlc_detail::__action_map.find( ah );

        ETDCASSERT( actionPointer != ctrlc_detail::__action_map.end(),
                    "Failed to find entry for control-c action #" << ah );
        ctrlc_detail::__action_map.erase( actionPointer );
        ETDCDEBUG(5, "Removed auto control-c action #" << actionPointer->first << std::endl);
        return;
    }
#endif

    ScopedAction::ScopedAction( ControlCAction cca ):
        ah( registerAutoCtrlCAction(cca) )
    {}

    ScopedAction::~ScopedAction() {
        // if the signal fired the handler has alread
        unregisterCtrlCAction( ah );
    }

    void handleActions( int s ) {
        std::lock_guard<std::mutex> lk{ ctrlc_detail::__mtx };
        ctrlc_detail::ActionMap::iterator             p;
        std::list<ctrlc_detail::ActionMap::iterator>  toRemove;

        ETDCDEBUG(5, "handleActions(" << s << ")" << std::endl);

        // Execute each registered action, catching exceptions
        for( p = ctrlc_detail::__action_map.begin(); p!= ctrlc_detail::__action_map.end(); p++ ) {
            try {
                ETDCDEBUG(5, "handleActions(" << s << ")/action handle#" << p->first << std::endl);
                p->second.first( s );
                if( p->second.second )
                    toRemove.push_back( p );
            }
            catch( std::exception const& e ) {
                ETDCDEBUG(-1, "handleActions[sig=" << s << " action#" << p->first << "]: " << e.what() << std::endl);
            }
            catch( ... ) {
                ETDCDEBUG(-1, "handleActions[sig=" << s << " action#" << p->first << "]: unknown exception" << std::endl);
            }
        }
        // handled all of them, can erase whole map!
        // XXX TODO maybe there could be actions that persist after
        //          handling?
        for( auto pp : toRemove )
            ctrlc_detail::__action_map.erase( pp );
    }
}
