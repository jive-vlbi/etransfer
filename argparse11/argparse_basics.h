// We need some classes and types defined before others
#ifndef ARGPARSE11_BASICS_H
#define ARGPARSE11_BASICS_H

#include <argparse_detail.h>

#include <set>
#include <list>
#include <string>
#include <sstream>
#include <exception>
#include <memory>
#include <typeindex>
#include <functional>
#include <type_traits>

#include <cstdlib>

namespace argparse {

    // forward declaration
    class ArgumentParser;


    struct CmdLineBase {
        // Print help, short (true) or long (false) format
        virtual void print_help( bool ) const = 0;
        virtual void print_version( void ) const = 0;

        virtual ~CmdLineBase() {}
    };


    struct constraint_violation: public std::domain_error {
        using std::domain_error::domain_error;
    };

    // The constraint implementation details shouldn't have to be in the
    // argparse namespace
    namespace detail {
        template <typename T>
        using Constraint  = std::function<bool(typename std::decay<T>::type const&)>;

        template <typename T>
        using ConstraintS = std::function<std::string(typename std::decay<T>::type const&)>;

        struct constraint_impl {
            constraint_impl() = delete;

            template <typename F,
                  typename std::enable_if<!std::is_bind_expression<F>::value, int>::type = 0,
                  typename std::enable_if<is_unary_fn< deduce_signature<F> >::value, int>::type = 0,
                  typename Arg = typename std::decay<typename std::tuple_element<0, typename deduce_signature<F>::argument_type>::type>::type,
                  typename Ret = typename std::decay<typename deduce_signature<F>::return_type>::type,
                  typename std::enable_if<std::is_same<Ret, bool>::value, int>::type = 0>
            constraint_impl(std::string const& descr, F f):
                __m_docstr( descr ),
                __m_index( std::type_index(typeid(typename std::decay<F>::type)) ),
                __m_function( new typename std::decay<F>::type(f) )
            {}
            template <typename T,
                      typename RealType = typename std::decay<T>::type>
            std::string constrain(T const& t) const {
                string_repr  stringer{};
                auto         ti = std::type_index(typeid(Constraint<RealType>));

                if( ti!=__m_index )
                    throw std::runtime_error(std::string("Constraint types did not match: expect=")+__m_index.name()+" got="+ti.name());
                if( (*reinterpret_cast<Constraint<RealType>*>(__m_function.get()))( t ) )
                    return std::string();
                // Form error message
                std::ostringstream oss;
                oss << "constraint \"" << __m_docstr << "\" violated by value '" << stringer(t) << "'";
                return oss.str();
            }

            std::string docstr( void ) const {
                return __m_docstr;
            }

            virtual ~constraint_impl() {}

            const std::string           __m_docstr;
            const std::type_index       __m_index;
            const std::shared_ptr<void> __m_function;
        };


        // Sometimes one needs case-insensitive string comparison
        struct case_insensitive_char_cmp {
            bool operator()(char l, char r) const {
                return ::toupper(l) < ::toupper(r);
            }
        };

        // Sort names in descending length and then alphabetically
        template <typename T>
        struct name_sort {
            bool operator()(T const& l, T const& r) const {
                if( l.size()==r.size() )
                    return std::lexicographical_compare(std::begin(l), std::end(l),
                                                        std::begin(r), std::end(r), case_insensitive_char_cmp());
                return l.size() > r.size();
            }
        };

    } // namespace detail

    // The collection of command line option names
    using namecollection_t = std::set<std::string, detail::name_sort<std::string>>;
    using docstringlist_t  = std::list<std::string>;

    // This function template allows one to get the appropriate "std::endl" for
    // a particular stream. The "std::endl" isn't a thing - it's a template.
    // As such "std::endl" by itself cannot be passed as function argument.
    // But using the ENDL(...) function you can - it'll return a reference to
    // the correctly instantiated version of std::endl.
    // (Not so well done on that one c++!)
    //       See: http://stackoverflow.com/a/10016203
    template <typename... Traits>
    auto ENDL(std::basic_ostream<Traits...> const&) -> decltype( &std::endl<Traits...> ) {
        return std::endl<Traits...>;
    }

    // Note: need to use ENDL(...) to pass endl as argument
    //       See above
    template <int exit_code = EXIT_FAILURE, typename... Ts,
              typename std::enable_if<exit_code==EXIT_FAILURE || exit_code==EXIT_SUCCESS, int>::type = 0>
    void fatal_error(std::ostream& os, std::string const& e, Ts&&... ts) {
        char  dummy[] = {(os << e << " ", 'a'), (os << std::forward<Ts>(ts), 'a')..., (os << std::endl, 'a')};
        (void)dummy;
        std::exit( exit_code );
    }

} // namespace argparse {

#endif
