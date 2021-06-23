#ifndef ETDC_CTRLC_H
#define ETDC_CTRLC_H
#include <functional>
#include <cstdint>

namespace etdc {

    // what can be registered?
    // A function returning void and taking an int (the signal number)
    using ControlCAction = std::function<void(int)>;
    using ActionId       = uint64_t;
    using ActionHandle   = std::pair<ActionId, bool>;//uint64_t;

    // User API
    // manual register/unregister; control c action will not be
    // automatically removed after handling due to a signal
    ActionHandle registerCtrlCAction( ControlCAction cca );
    ActionHandle registerAutoCtrlCAction( ControlCAction cca );
    void         unregisterCtrlCAction( ActionHandle ah );
    // these will register the control c action and automatically
    // remove the action after they're handled because of a signal
    //void         unregisterAutoCtrlCAction( ActionHandle ah );

    // Scoped registration
    struct ScopedAction {
        // No default c'tor and also no copy/assignment
        ScopedAction() = delete;
        ScopedAction( ScopedAction const& ) = delete;
        ScopedAction& operator=( ScopedAction const& ) = delete;

        ScopedAction( ControlCAction cca );
        ~ScopedAction();

        private:
            ActionHandle ah;
    };

    // This is what the signal-handler should call when tripped with signal "s"
    void handleActions( int s );

}

#endif
