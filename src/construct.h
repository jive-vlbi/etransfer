// Simple type-based member updater and Python-style "key=value" member updater
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
//
//  Example:
//
//     // Struct holding a collection of datums - no 'state'
//     // e.g. some default settings
//     struct X {
//          int    ifield {};
//          string sfield {} ;
//          float  ffield {};
//     };
//
//     // For illustration purposes this one only allows updating the string-typed field
//     static const auto updater = etdc::construct<X>{ &X::sfield };
//
//     // API function where user may give any number of arguments
//     template <typename... Args>
//     void do_something_useful(Args... args) {
//          X   defaults{}; // assume default c'tor sets suitable defaults
//          // Allow the user to override any number of them. In this case,
//          // because 'updater' is only primed with the 'string based field'
//          // the user may supply a std::string as (one of) the arguments
//          // and it will update the field in 'defaults'
//          updater(defaults, std::forward<Args>(args)...);
//
//          // Either outputs the default given to sfield or the value passed in by the user
//          std::cout << "sfield=" << defaults.sfield << std::endl;
//          return;
//      }
//
//   The obvious problem is that this only looks at the type - so what if X has e.g. two string-typed fields?
//   For those cases please use the  - which allows for (Python-style) "key=value" style
//   of member updating
#ifndef ETDC_CONSTRUCT_H
#define ETDC_CONSTRUCT_H

#include <map>
#include <memory>
#include <typeinfo>
#include <typeindex>
#include <stdexcept>

#include <utilities.h>

namespace etdc {
    namespace detailS {
        // We want to keep the pointer-to-member values in a type-safe object.
        // That will be the "memptr_holder". Its only data member will be the pointer-to-member.
        // By deriving it from the virtual empty base class, we can can store pointer-to-emptybase
        // in a container and actually store pointer-to-memptr_holder in it.
        struct empty_base { 
            virtual ~empty_base() {};
        };
        using memptrptr_type = std::shared_ptr<empty_base>;

        template <typename T, typename Type>
        struct memptr_holder: public empty_base {
            memptr_holder(Type (T::*p)): __ptr( p ) {}
            Type T::*__ptr;
        };
    }

    // Missing key policies
    struct MissingKeyIsOk {
        template <typename KeyType>
        void operator()( KeyType* ) const { }
    };
    struct MissingKeyIsNotOk {
        template <typename KeyType>
        void operator()( KeyType* ) const {
            throw std::runtime_error( std::string("Missing key for type = ")+etdc::type2str<KeyType>() );
        }
    };

    template <typename Class, typename MissingKeyPolicy = MissingKeyIsNotOk>
    struct construct {
        // compiler generated default and assignment should be just fine, now that we store shared_ptr's
        // in our data member [which have well defined copy+assignment semantics]

        // Our constructor can take any number of arguments
        template <typename... Args>
        construct(Args&&... args) {
            this->build_map(std::forward<Args>(args)...);
        }

        template <typename... Ts>
        Class mk(Ts... ts) const {
            Class   object{};
            this->operator()(object, std::forward<Ts>(ts)...);
            return object;
        }

        // Base case for operator() - no more arguments to process
        unsigned int operator()(Class&) const { return 0; }

        // Strip off one type and update it if we find an entry for it
        template <typename T, typename... Args>
        unsigned int operator()(Class& object, T const& value, Args&&... args) const {
            auto         entry = __m_tpmap.find( key_fn<T>() );
            unsigned int n = 0;

            if( entry==__m_tpmap.end() )
                MissingKeyPolicy()((T*)0);
            else 
                // Olreit. Update that mother!
                object.*(dynamic_cast<detailS::memptr_holder<Class,T>*>(entry->second.get())->__ptr) = value, n=1;
            // Move on to nxt
            return n + this->operator()(object, std::forward<Args>(args)...);
        }
                
        private:
            using typemap_type = std::map<std::type_index, detailS::memptrptr_type>;
            typemap_type __m_tpmap;

            template <typename T>
            static auto key_fn( void ) -> std::type_index { return std::type_index(typeid(T)); }

            // base case for map building - no more parameters
            void build_map(void) { }

            // Add another type to the map
            template <typename T, typename... Args>
            void build_map(T (Class::*member), Args... args) {
                auto entry = __m_tpmap.emplace(key_fn<T>(), std::make_shared<detailS::memptr_holder<Class,T>>(member));
                if( !entry.second )
                    throw std::logic_error( std::string("construct: double insert of type ")+etdc::type2str<T>() );
                // And process the next type
                this->build_map(std::forward<Args>(args)...);
            }
    };

} // namespace etdc

#endif
