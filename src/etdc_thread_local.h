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
#include <array>
#include <vector>
#include <algorithm>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <functional>

#include <pthread.h>
#include <string.h>    // for ::strerror_r(3)

namespace etdc {

    // All thread-local variables are identified by a unique key
    // the key is initialized exactly once on first use.
    // Also we store the actual 'destructor' (in plain-old C/pthread meaning
    // of the word) in here such that both single object and array types can
    // share the basic thread-local basics
    namespace detail {
        template <typename T, void(*deleter)(void*)>
        struct tls_basics {
            static pthread_key_t    tls_key;
            static pthread_once_t   tls_once_guard;

            static void init( void ) {
                int  rc;
                char ebuf[128];

                if( (rc=::pthread_key_create(&tls_key, deleter))!=0 ) {
                    if( ::strerror_r(rc, ebuf, sizeof(ebuf)) ) {}
                    std::cerr << "tls_basics_type/failed to create TLS key: " << ebuf << std::endl;
                    throw std::runtime_error(std::string("tls_basics_type/failed to create TLS key: ") + ebuf);
                }
            }
        };
        template <typename T, void(*deleter)(void*)>
        pthread_key_t tls_basics<T, deleter>::tls_key;
        template <typename T, void(*deleter)(void*)>
        pthread_once_t tls_basics<T, deleter>::tls_once_guard;
    }

    // Each tls_object that is created is 'just a template'
    // As soon as a thread requests access to the tls_object, then and only
    // then an instance will be constructed.
    // Each tls_object of type T that is constructed (the template) will get
    // a sequence number such that multiple instances of the same type can
    // be used. This class will only create a thread-specific instance of a
    // prototype if it is accessed by a thread.
    template <typename T, typename...>
    struct tls_object_type {

        typedef tls_object_type<T>                    Self;

        // Type safe deleter
        static void deleter(void* ptr) {
            delete reinterpret_cast<typename Self::managed_objects_type*>(ptr);
        }

        typedef detail::tls_basics<T, &Self::deleter> Basics;

        // Support any c'tor that T supports ...
        template <typename... Ts>
        explicit tls_object_type(Ts... ts):
            // Capture all constructor arguments in the kreator
            __m_kreator( std::bind(&Self::template ensure_tls_instance<T, Ts...>,
                                   Self::prototypeCount++, std::placeholders::_1, std::forward<Ts>(ts)...) )
        {
            ::pthread_once(&Basics::tls_once_guard, Basics::init);
        }

        T* operator->( void )             { return __m_ptr(); }
        T const* operator->( void ) const { return __m_ptr(); }
        T& operator*( void )              { return *__m_ptr(); }
        T const& operator*( void ) const  { return *__m_ptr(); }


        template <typename U>
        T const& operator=(U const& u) {
            T*  tptr = __m_ptr();
            *tptr = T{u};
            return *tptr;
        }

        operator T(void) const {
            return *__m_ptr();
        }

        // No copy c'tor or assignment
        tls_object_type(tls_object_type&&)                 = delete;
        tls_object_type(tls_object_type const&)            = delete;
        tls_object_type& operator=(tls_object_type const&) = delete;

        private:
            // On a per-thread basis, we keep map <prototypeNumber> => <instance>
            using managed_objects_type = std::map<unsigned int, T>;

            // The creator-function should make sure that an instance exists
            // in the passed managed-objects thingamabob and return a
            // pointer to that instance
            using creator_fn = std::function<T*(managed_objects_type*)>;

            // Assign a unique number to each prototype
            static unsigned int   prototypeCount;

            // Our only data member            
            creator_fn            __m_kreator;

            // This member function ensures that a thread-local object store
            // exists and returns a pointer to the desired instance
            T* __m_ptr( void ) const {
                managed_objects_type*   managed_objects;
                // Get an instance of managed_objects_type
                if( (managed_objects=reinterpret_cast<managed_objects_type*>(::pthread_getspecific(Basics::tls_key)))==0 )
                    ::pthread_setspecific(Basics::tls_key, managed_objects = new managed_objects_type());
                // Now get a pointer to the thread-local instance of this prototype
                return __m_kreator(managed_objects);
            }

            // This wrapper ensures that an object with id 'id' exists in
            // the managed-objects store. If it doesn't exist it will insert
            // a fresh entry
            template <typename U, typename... Ts>
            static U* ensure_tls_instance(unsigned int id, managed_objects_type* objStore, Ts... ts) {
                auto ptr = objStore->find(id);
                if( ptr==objStore->end() ) {
                    // Buggrit.
                    auto insres = objStore->insert( std::make_pair(id, U(std::forward<Ts>(ts)...)) );
                    if( !insres.second )
                        throw std::runtime_error("Failed to insert thread-local instance into object store");
                    ptr = insres.first;
                }
                // ptr now is iterator to pair <Key, Value>
                return &ptr->second;
            }
    };

    template <typename T, std::size_t N>
    struct tls_object_type<T[N]> {

        using stored_type  = std::array<T, N>;
        using Self         = tls_object_type<T[N]>;

        // Type safe deleter
        static void deleter(void* ptr) {
            delete reinterpret_cast<Self::managed_objects_type*>(ptr);
        }

        using Basics = detail::tls_basics<T, &Self::deleter>;

        // std::array<> has no constructors
        // so only default constructible or implicit assignable from
        // initializer list
        explicit tls_object_type():
            __m_kreator( std::bind(&Self::ensure_tls_instance, Self::prototypeCount++, std::placeholders::_1) )
        {
            ::pthread_once(&Basics::tls_once_guard, Basics::init);
        }
        // We must copy the initializer list because it is a /very/ shallow
        // object and we have no idea what happens with the underlying
        // storage after this c'tor has executed. So we copy the values into
        // a vector and store that.
        explicit tls_object_type(std::initializer_list<T> il):
            // Capture all constructor arguments in the kreator
            __m_kreator( std::bind(&Self::ensure_tls_instance_il,
                                   Self::prototypeCount++, std::placeholders::_1, std::vector<T>(il)) )
        {
            if( il.size()>N )
                throw std::runtime_error("Attempt to initialize static array with more values than reserved");
            ::pthread_once(&Basics::tls_once_guard, Basics::init);
        }
        typename stored_type::iterator begin( void ) {
            return __m_ptr()->begin();
        }
        typename stored_type::const_iterator begin( void ) const {
            return __m_ptr()->begin();
        }
        typename stored_type::iterator end( void ) {
            return __m_ptr()->end();
        }
        typename stored_type::const_iterator end( void ) const {
            return __m_ptr()->end();
        }

        T& operator[](size_t idx) {
            return __m_ptr()->at(idx);
        }
        T const& operator[](size_t idx) const {
            return __m_ptr()->at(idx);
        }

        // No copy c'tor or assignment
        tls_object_type(tls_object_type&&)                 = delete;
        tls_object_type(tls_object_type const&)            = delete;
        tls_object_type& operator=(tls_object_type const&) = delete;

        private:
            // On a per-thread basis, we keep map <prototypeNumber> => <instance>
            using managed_objects_type = std::map<unsigned int, stored_type>;

            // The creator-function should make sure that an instance exists
            // in the passed managed-objects thingamabob and return a
            // pointer to that instance
            using creator_fn = std::function<stored_type*(managed_objects_type*)>;

            // Assign a unique number to each prototype
            static unsigned int   prototypeCount;

            // Our only data member            
            creator_fn            __m_kreator;

            // This member function ensures that a thread-local object store
            // exists and returns a pointer to the desired instance
            stored_type* __m_ptr( void ) const {
                managed_objects_type*   managed_objects;
                // Get an instance of managed_objects_type
                if( (managed_objects=reinterpret_cast<managed_objects_type*>(::pthread_getspecific(Basics::tls_key)))==0 )
                    ::pthread_setspecific(Basics::tls_key, managed_objects = new managed_objects_type());
                // Now get a pointer to the thread-local instance of this prototype
                return __m_kreator(managed_objects);
            }

            // This wrapper ensures that an object with id 'id' exists in
            // the managed-objects store. If it doesn't exist it will insert
            // a fresh entry
            static stored_type* ensure_tls_instance(unsigned int id, managed_objects_type* objStore) {
                auto ptr = objStore->find(id);
                if( ptr==objStore->end() ) {
                    // Buggrit.
                    auto insres = objStore->emplace( id, stored_type{} );
                    if( !insres.second )
                        throw std::runtime_error("Failed to insert thread-local instance into object store");
                    ptr = insres.first;
                }
                // ptr now is iterator to pair <Key, Value>
                return &ptr->second;
            }
            static stored_type* ensure_tls_instance_il(unsigned int id, managed_objects_type* objStore, std::vector<T> il) {
                auto ptr = objStore->find(id);
                if( ptr==objStore->end() ) {
                    // Because we cannot construct the std::array<> from an
                    // initializer list we must do it the hard way: 'memcpy'
                    auto insres = objStore->emplace( id, stored_type{} );
                    if( !insres.second )
                        throw std::runtime_error("Failed to insert thread-local instance into object store");
                    ptr = insres.first;
                    std::copy(std::begin(il), std::end(il), std::begin(ptr->second));
                }
                // ptr now is iterator to pair <Key, Value>
                return &ptr->second;
            }
    };

    template <typename T, typename... Ts>
    unsigned int tls_object_type<T, Ts...>::prototypeCount = 0;
    template <typename T, size_t N>
    unsigned int tls_object_type<T[N]>::prototypeCount = 0;
}

#endif // ETDC_THREAD_LOCAL_H
