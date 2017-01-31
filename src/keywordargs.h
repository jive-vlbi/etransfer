// Utility to support Python like keyword-args style updating of a struct's members
#ifndef ETDC_KEYWORDARGS_H
#define ETDC_KEYWORDARGS_H
//
//  Main idea:
//     keep a mapping of key to pointer-to-member-of-X.
//
//     Upon request, process a number of KeyValue()'s and an instance of X, say
//     "x" to update "x"'s member(s) based on the key/values given in a
//     variable template argument list.
//
//     The simplest is to derive your X from "KeywordDict<X>" (see below) because
//     that enables the global function template "update(x, KV...)" (see
//     belower)
//
//     Another option is to create a static mapping of key to pointer-to-member
//     and then (manually) call it's update() member function to update an
//     instance of X based upon arguments passed to update().
//
//  //////////////////           Method 1      //////////////////////
//
//  #include <keywordargs.h>
//
//  // This struct will keep a number of simple settings
//  struct S: public etdc::KeywordDict<S> {
//      unsigned int mtu, bufSize;
//
//      S(unsigned int defMTU=1500, unsigned int defBuf=128*1024) :
//          // here we define the mapping of key -> where to store a value.
//          // Note that the Key does not have to be string!
//          KeywordDict( mk_kv("mtu", &S::mtu), mk_kv(2, &S::bufSize) )
//          // Can set defaults
//          mtu( defMTU ), bufSize( defBuf )
//      {}
//  };
//
//  // By defining foo() like below, foo() can now take an arbitrary number
//  // of key-value arguments and we'll update
//  template <typename... Args>
//  void foo( Args... args ) {
//      // Create default settings
//      S   settings;
//      // Request them to be updated from whatever the user passed in
//      update(settings, std::forward<Args>(args)...);
//
//      // Use the current settings:
//      std::cout << "MTU=" << settings.mtu << ", bufSize=" << settings.bufSize << std::endl;
//  }
//
//  // User can now use foo() like such:
//  ...
//  foo( mk_kv("mtu", 9000) );
//  foo( key("mtu")=9000, 3_key=1024 );  // with extra key() and user-defined-literal functionality
//  ..
//
//  foo( mk_kv(3, 4*1024*1024), mk_kv("mtu", 4470) );
//
//
//  //////////////////           Method 2      //////////////////////
//
//  Alternatively you can do:
//
//  static const KeywordDict<S> updatert{ mk_kv("mtu", &S::MTU), mk_kv(2, &S::bufSize) };
//
//  template <typename... Args>
//  void foo( Args... args ) {
//      // Create default settings
//      S   settings;
//      // Request them to be updated from whatever the user passed in
//      updatert.update(settings, std::forward<Args>(args)...);
//      ...
//  }
//

// Own includes
#include <streamutil.h>   // for repr()

// C++ headers
#include <map>
#include <typeindex>
#include <type_traits>
#include <stdexcept>

namespace etdc {
    // For easy exception message generation
    using etdc::repr;

    template <typename Key, typename Value>
    using KeyValue = std::pair<Key, Value>;

    template <typename Class, typename Value>
    struct MemberPointer {
        typedef Value Class::*Member;
        MemberPointer(Member ptr):
            memberPointer(ptr)
        {}

        void operator()(Class& object, Value const&& value) const {
            (object.*memberPointer) = value;
        }
        Member  memberPointer;
    };


    template <typename Class>
    struct KeywordDict {
        // Keep mapping of key-type => key-value => memberptr?
        // key-type gives  void* (*)(void*)
        typedef std::map<std::type_index, void*> typemap_type;

        // typemap_type keymap;
        //
        // keymap[ typeid(Key) ] = map<Key, typemap_type>
        // keymap[ typeid(Key) ] [ key ] = typemap_type
        // keymap[ typeid(Key) ] [ key ] [ typeid(Value) ] (== MemberPointer)

        template <typename... Args>
        KeywordDict(Args&&... args) {
            //cout << "KeywordDict: constructing!" << endl;
            this->build_map(std::forward<Args>(args)...);
        }

        // End case of the 'recursion'
        unsigned int update(Class&) const {
            return 0;
        }

        // Update object's member pointed to by kv[0] to kv[1]
        // (if such an entry exists in our key->member mapping) 
        template <typename K, typename V, typename... Args>
        //unsigned int update(Class& object, KeyValue<K, V>&& kv, Args... args) const {
        unsigned int update(Class& object, KeyValue<K, V> const& kv, Args... args) const {
            // At this point we know the type of the mapping that we expect: map<K, typemap_type>
            using keymap_type =  std::map<K, memberdetails_type>;

            unsigned int                 n = 0;
            const std::type_index        keyt( std::type_index(typeid(K)) );
            typemap_type::const_iterator keytypeptr = typemap.find( keyt );

            if( keytypeptr!=typemap.end() ) {
                // OK keytypeptr now points to pair<KeyType, memberdetails_type*>
                auto keymapptr   = static_cast<keymap_type const*>(keytypeptr->second);

                // Attempt to find the keyvalue
                auto keyentryptr = keymapptr->find( kv.first );

                if( keyentryptr!=keymapptr->end() ) {
                    // kentryptr->second == memberdetails_type; ".first" of that
                    // must match type of value
                    memberdetails_type const&  md( keyentryptr->second );

                    // Is it of the correct type? 
                    if( md.first == std::type_index(typeid(V)) )
                        (object.*((static_cast<MemberPointer<Class, V>*>(md.second))->memberPointer)) = kv.second, n=1;
                    else {
                        throw std::logic_error(std::string("Value type ")+typeid(V).name() + 
                                               " did not match expectations for key "+repr(kv.first));
                    }
                } else {
                    throw std::logic_error("No location found for key "+repr(kv.first));
                }
            } else {
                throw std::logic_error("No key type registered for "+repr(kv.first) );
            }
            // And process the next entry
            return n + this->update(object, std::forward<Args>(args)...);
        }
                

        private:
            // memberdetails currently simply stored as a std::pair
            using memberdetails_type = std::pair<std::type_index, void*>;

            // Our only data member - the actual typemap
            typemap_type  typemap;

            // base case of the 'recursion' - this is where it ends
            void build_map(void) { }


            // We expect KeyValue(key, valuetype (Class::*MemberPtr))
            // Only enable if V is assignable and also that key type is NOT 'char const*' because
            // those are compared by address rather than by string content!!!!
            template <typename K, typename V, typename... Args>
            typename std::enable_if<std::is_assignable<typename std::add_lvalue_reference<V>::type, V>::value &&
                                    !std::is_same<K, char const*>::value>::type
              build_map(KeyValue<K, V (Class::*)>&& kv, Args... args) {
                // At this point we know the type of the mapping that we expect: map<K, typemap_type>
                using keymap_type =  std::map<K, memberdetails_type>;

                const std::type_index   keyt( std::type_index(typeid(K)) );
                typemap_type::iterator  keytypeptr = typemap.find( keyt );

                if( keytypeptr==typemap.end() ) {
                    auto insres = typemap.insert( make_pair(keyt, static_cast<void*>(new keymap_type())) );
                    if( !insres.second )
                        throw std::logic_error( std::string("Failed to insert entry for Key type ")+typeid(K).name() );
                    keytypeptr = insres.first;
                }
                // OK keytypeptr now points to pair<KeyType, memberdetails_type*>
                auto keymapptr = static_cast<keymap_type*>(keytypeptr->second);

                // Attempt to insert an entry for the current key value. If that
                // fails there was already a definition for that key in the mapping
                memberdetails_type md{std::type_index(typeid(V)), static_cast<void*>(new MemberPointer<Class, V>(kv.second))};

                if( !keymapptr->insert(make_pair(kv.first, md)).second )
                    throw std::logic_error("Duplicate entry for key value "+repr(kv.first));

                // And process the next entry
                this->build_map(std::forward<Args>(args)...);
            }
    };

    // Extra helper Key class that supports assignment and delivers a KeyValue upon such :D
    template <typename K>
    struct Key {
        // Only explicit construction/copy c'tor
        explicit Key( K const& k )         : key( k ) {}
        explicit Key( Key<K> const& other) : key( other.key ) {}

        // We do support the move c'tor - but that don't have to be explicit for there cannot be confusion
        Key( Key<K>&& other ): key( std::move(other.key) ) {}

        // We must not allow the compilert to come up with an implicit assignment
        Key<K> const& operator=( Key<K> const& other ) {
            if( this!=&other )
                this->key = other.key;
            return *this;
        }

        // Magic!
        // (note: support special version for automatic 'char const*' => std::string conversion
        KeyValue<K, std::string> operator=(char const* v) const {
            return std::make_pair(key, std::string(v));
        }
        // More magic!
        template <typename V>
        KeyValue<K,V> operator=(V const& v) const {
            return KeyValue<K,V>(key, v);
        }

        const K   key;
    };


} // namespace etdc

///////////////////////////////////////////////////////////////////////////////////////////////
//
//  In the global namespace live only a few useful functions to not pollute it 
//
///////////////////////////////////////////////////////////////////////////////////////////////

// Must be able to easily construct key/value pairs

// generic template which excludes 'const char*'
template <typename Key, typename Value>
typename std::enable_if<!std::is_same<char const*,Key>::value, etdc::KeyValue<Key,Value>>::type
mk_kv(Key const& k, Value const& v) {
    return etdc::KeyValue<Key, Value>(k, v);
}

// special version for 'const char*' - such that it gets converted to std::string
template <typename Value>
etdc::KeyValue<std::string, Value>
mk_kv(char const* k, Value const& v) {
    return etdc::KeyValue<std::string, Value>(std::string(k), v);
}

// User-defined literal - the "_key" suffix
etdc::Key<unsigned long long int> operator "" _key(unsigned long long int i) {
    return etdc::Key<unsigned long long int>(i);
}

// again autmatic 'char const*' => std::string conversion
etdc::Key<std::string> operator "" _key(char const* s, size_t n) {
    return etdc::Key<std::string>(std::string(s, n));
}

// Functional form (include 'char const*' => std::string conversion ...)
etdc::Key<std::string> key(char const* k) {
    return etdc::Key<std::string>(std::string(k));
}

template <typename T>
etdc::Key<T> key(T const& k) {
    return etdc::Key<T>(k);
}

// Global update function - only works if Class was derived from KeywordDict
template <typename Class, typename... Args>
typename std::enable_if<std::is_base_of<typename etdc::KeywordDict<Class>, Class>::value, unsigned int>::type
update(Class& object, Args... args) {
    return object.update(object, std::forward<Args>(args)...);
}

#endif // ETDC_KEYWORDWARGS_H
