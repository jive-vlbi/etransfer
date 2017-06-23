// We use UUIDs to keep track of individual transfers
#ifndef ETDC_UUID_H
#define ETDC_UUID_H

#include <etdc_assert.h>

// C++ headers
#include <mutex>
#include <string>
#include <random>
#include <algorithm>
#include <functional>

namespace etdc {
    class uuid_type     : public std::string {
        public:
            // We cannot have default uuids!
            uuid_type()   = delete;

            template <typename X, typename... Args>
            explicit uuid_type(X&& x, Args&&... args): std::string(std::forward<X>(x), std::forward<Args>(args)...) {
                // After construction, assert that we're non-empty
                ETDCASSERT(this->empty()==false, "UUID cannot be empty");
            }

            // Generate a new UUID
            static uuid_type mk( void ) {
                // In-class statics are a bugger. Jeebus.
                static const std::string                                     __m_chars{ "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ012345679" };
                static std::mutex                                            __m_random_lock{};
                static std::random_device                                    __m_random_device{};
                static std::default_random_engine                            __m_random_engine{ __m_random_device() };
                static std::uniform_int_distribution<std::string::size_type> __m_uniform_sizes{15, 20};
                static std::uniform_int_distribution<std::string::size_type> __m_uniform_chars{0, __m_chars.size()};
                static auto                                                  __m_sizegen = std::bind(__m_uniform_sizes, __m_random_engine);
                static auto                                                  __m_chargen = [&]() { return __m_chars[__m_uniform_chars(__m_random_engine)]; };

                // Non-static, automatic variables
                std::string                 uuid;
                std::lock_guard<std::mutex> mScopedLock( __m_random_lock );
                
                std::generate_n(std::back_inserter(uuid), __m_sizegen(), __m_chargen);
                return uuid_type(uuid);
            }
    };
}// namespace etdc

#endif
