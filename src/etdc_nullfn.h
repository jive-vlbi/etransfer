// Generate uninitialized function pointers which throw descriptive error if called
//
// Purpose:
//   Normally you can say:
//   std::function<void(int)>   fptr( std::nullptr );
//
//   but then, if you (forget) to initialize fptr to something sensible, the
//   following code:
//
//      fptr(42);
//   
//   will get you an exception like this:
//      std::__1::bad_function_call: std::exception
//
//   w/o telling you where it happened or which functionpointer it was.
//
//   The macro at the end of this file allows you to do this:
//
//   std::function<void(int)>   fptr( nullfn(void(int)) );
//
//   Now, the following code:
//      fptr(42);
//   will throw an error with message like this:
//      "call of uninitialized function: <file>:<line> [or <file2>:<line2> ... ]"
//
//   i.e. it will include the location(s) where the unitialized function
//   was originally initialized so you should be able to tell which function
//   was actually called and take approrpriate action to make sure it is not
//   uninitialized any more.
//
#ifndef ETDC_NULLFN_H
#define ETDC_NULLFN_H

#include <set>
#include <string>
#include <stdexcept>
#include <functional>

#include <utilities.h>

namespace etdc {
    namespace detail {
        // for the raw function pointers we cannot dynamically bind
        // each location to a specific instance [like with lambda's].
        // So we keep a set of initialization points such that
        // in the message we can at least hint the user at where to look
        // for function pointers that are not actually re-initialized to
        // something non-null.
        struct location_type {
            std::string __m_file {};
            int         __m_line {};

            inline location_type(std::string const& f, int l): __m_file(f), __m_line(l) {}
            //template <typename T>
            //location_type(T t, int l): __m_file(std::forward<T>(t), l) {}

            // Can insert into any ostream
            template <class CharT, class Traits>
            friend std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, location_type const& l) {
                return os << l.__m_file << ":" << l.__m_line;
            }
        };

        struct lt_location_type {
            inline bool operator()(location_type const& l, location_type const& r) const {
                if( l.__m_file==r.__m_file )
                    return l.__m_line<r.__m_line;
                return l.__m_file < r.__m_file;
            }
        };
        using locations_type = std::set<location_type, lt_location_type>;
    }

    // This is the type of error that we'll throw upon calling
    struct uninitialized_function_call:
        public std::runtime_error
    {
        uninitialized_function_call(std::string const& s):
            std::runtime_error(std::string("call of uninitialized function: ")+s)
        {}

        using std::runtime_error::what;
    };

    ////////////////////////////////////////////////////////////////////////////////////////
    //
    //    The main template does not define the "::mk()" function
    //    such that code like
    //
    //    nullfn_type<R(Ts...)>  
    //
    //    does not compile. 
    //    Which is just as well, because "R(Ts...)" is just-a-signature, it is not
    //    a real type of which an instance can be constructed.
    //
    ////////////////////////////////////////////////////////////////////////////////////////

    template <typename... Ts>
    struct nullfn_type { };

    // This specialization describes raw function pointers.
    // Because we can only return an address of a static function in this
    // case, we record all initialization points and throw an error
    // containing the list of points where such a pointer was initialized to
    // null.
    template <typename R, typename... Ts>
    struct nullfn_type<R(*)(Ts...)> {
        using type = R (*)(Ts...);

        // Deal with void return type
        template <typename U, typename std::enable_if<std::is_void<U>::value, int>::type = 0>
        static void f(Ts...) {
            std::ostringstream oss;
            oss << etdc::type2str<type>() << " ";
            std::copy(std::begin(__initialization_points), std::end(__initialization_points),
                      std::ostream_iterator<typename decltype(__initialization_points)::value_type>(oss, " or "));
            throw uninitialized_function_call( oss.str() );
            return;
        }

        // Deal with non-void return type
        template <typename U, typename std::enable_if<!std::is_void<U>::value, int>::type = 0>
        static U f(Ts...) {
            std::ostringstream oss;
            oss << etdc::type2str<type>() << " ";
            std::copy(std::begin(__initialization_points), std::end(__initialization_points),
                      std::ostream_iterator<typename decltype(__initialization_points)::value_type>(oss, " or "));
            throw uninitialized_function_call( oss.str() );
            return U{};
        }

        static type mk(char const* f, int l) {
            __initialization_points.emplace(f, l);
            return &nullfn_type<R(*)(Ts...)>::f<R>;
        }

        static detail::locations_type   __initialization_points;
    };
    template <typename R, typename... Ts>
    detail::locations_type nullfn_type<R(*)(Ts...)>::__initialization_points = detail::locations_type{};

    //  specialization for std::function<...> 
    template <typename R, typename... Ts>
    struct nullfn_type<std::function<R(Ts...)>> {
        using type = std::function<R(Ts...)>;
        using self = nullfn_type<type>;

        // Again, need to deal with void/non-void return type
        template <typename U, typename std::enable_if<std::is_void<U>::value, int>::type = 0>
        static type f(detail::location_type location) {
            return [=](Ts...) { 
                std::ostringstream oss;
                oss << etdc::type2str<type>() << " " << location;
                throw uninitialized_function_call( oss.str() );
                return;
            };
        }
        template <typename U, typename std::enable_if<!std::is_void<U>::value, int>::type = 0>
        static type f(detail::location_type location) {
            return [=](Ts...) { 
                std::ostringstream oss;
                oss << etdc::type2str<type>() << " " << location;
                throw uninitialized_function_call( oss.str() );
                return U{};
            };
        }
        static type mk(char const* f, int l) {
            return self::f<R>(detail::location_type(f, l));
        }
    };

} // Namespace etdc

#define nullfn(signature) etdc::nullfn_type<signature>::mk(__FILE__, __LINE__)

#endif
