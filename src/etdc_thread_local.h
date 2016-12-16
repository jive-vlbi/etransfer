// Bastard Apple devs don't do 'thread_local' on "old" systems? FFS!
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
#ifndef ETDC_THREAD_LOCAL_H
#define ETDC_THREAD_LOCAL_H

#include <map>
#include <thread>
#include <mutex>
#include <utility>
#include <type_traits>

namespace etdc {

    // provide clunky thread-local lookalike iso having it properly
    // supported by the system. FFS.
    template <typename T>
    struct thrd_local {
        // Assert this at compile time - gives a mildly decent reason
        // why we're rejecting instantiating an instance for this
        // particular type 'T' if the compiler decides to reject it
        static_assert( std::is_copy_constructible<T>::value, "Your thingamabob isn't copy-constructible" );

        public:
            // Only support this c'tor if 'T''s are default constructible
            template <typename Type = T>
            thrd_local(typename std::enable_if<std::is_default_constructible<Type>::value>::type* = nullptr): __m_init{} {}

            // This c'tor takes at least one argument and any number of
            // extra things (if not at least one argument it'd be the
            // default c'tor, for that one, see above)
            template <typename X, typename... Args>
            thrd_local(X&& x, Args&&... args) :
                __m_init(std::forward<X>(x), std::forward<Args>(args)...)
            {}

            // delete the c'tors &cet that don't make sense for this type
            thrd_local( thrd_local<T> const& )                   = delete;
            thrd_local( thrd_local<T>&& )                        = delete;
            thrd_local<T> const& operator=(thrd_local<T> const&) = delete;

            operator T const&( void ) const {
                return this->get();
            }

            T& get( void ) const {
                auto       tid = std::this_thread::get_id();
                ScopedLock sl( const_cast<MutexType&>(__m_mutex) );
                auto       ptr = __m_instance_map.find( tid );

                if( ptr==__m_instance_map.end() )
                    // insert fresh instance
                    ptr = __m_instance_map.insert( std::make_pair(tid, T{__m_init}) ).first;
                return ptr->second;
            }

            T& operator=(T const& t) {
                return this->get()=t;
            }

        private:
            using MutexType  = std::mutex;
            using ScopedLock = std::lock_guard<MutexType>;
            const T                              __m_init;
            MutexType                            __m_mutex;
            mutable std::map<std::thread::id, T> __m_instance_map;
    };
}

#endif // ETDC_THREAD_LOCAL_H
