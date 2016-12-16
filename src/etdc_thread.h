// utility classes/functions for dealing with threads
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
#ifndef ETDC_THREAD_H
#define ETDC_THREAD_H

// own includes
#include <etdc_signal.h>

// std c++
#include <thread>
#include <map>

namespace etdc {
    // Wrapper for std::thread(...) that guarantees the thread is being run
    // w/ all signal blocked
    template <typename... Args>
    std::thread thread(Args&&... args) {
        etdc::BlockAll     block_all{};
        return std::thread(std::forward<Args>(args)...);
    }
}

#endif  // ETDC_THREAD_H
