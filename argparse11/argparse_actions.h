// Define the actions and support types/functionality to construct command line options
// Copyright (C) 2017  Harro Verkouter, verkouter@jive.eu
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
#ifndef ARGPARSE11_ACTIONS_H
#define ARGPARSE11_ACTIONS_H

//
//  The building blocks that are exposed to the user for building command
//  line options:
//
//  Names:
//    Command line options may have short/long names. Up to one unnamed
//    option may be present - this will represent the command line arguments
//    (i.e. anything that is not an option).
//
//      short_name(char)      A simple -X command line option
//                            Note: digits are not allowed, otherwise there
//                                  would be no telling negative numbers
//                                  apart from command line options ...
//      long_name(string)     A longer name --XY... command line option
//
//  Actions:
//
//      store_true()          Simple true/false flags. They do what they 
//      store_false()         say on the tin. They automatically provide 
//                            a default which is the opposite of the
//                            action's result
//
//      store_const(T t)      If command line option is present, stores
//                            the value t of type T. May or may not be 
//                            combined with a default; up to you.
//                            Note: "char const*" is silently converted
//                                  to std::string
//      store_const_into(T t, T& v)
//                            If command line option is present, stores
//                            the const t in the variable referenced by v.
//                            This cannot be combined with a default (you
//                            should default-init v yourself).
//                            Note: t = "char const*", v = std::string&
//                                  should work and behave as expected
//
//      store_value<T>()      Converts option argument to type T and stores it
//
//      store_into(T& t)      Converts option argument into type inferred from
//                            the variable which is referenced and stores it into 
//                            the variable which is referenced
//
//      count()               This action allows the user to, well, 
//                            count how often the command line option was
//                            present on the command line ...
//
//      count_into(T& t)      You can refer to a variable where we should
//                            count into. "T" must be an arithmetic type
//                            and will be initialized with 0 automatically
//
//      collect<T>()          Transform the arguments of the option to type T
//                            and collect them, by default, in std::list<T>.
//                            A second, template template, parameter can be given
//                            to use a different container type:
//                              collect<int, std::set>()  will collect ints into
//                              a std::set<int>, surprisingly enough
//
//      collect_into(T& t)    Allow user to specificy a variable to collect
//                            converted values into. Two supported types for
//                            type "T":
//                            1. T is an output_iterator. The type of
//                               value(s) to be collected is inferred from the
//                               output_iterator.
//                               Uses "*t++ = <converted value>"
//
//                            2. T is a container. The type of the value(s)
//                               to be collected is inferred from the
//                               container.
//                               Uses "t.insert(t.end(), <converted value>)"
//
//
//      print_help()          If these action(s) are triggered they print
//      print_usage()         usage (short help) or the full help (long
//                            help).
//
//      print_version()       Guess what. Note that it displays the 
//                            version object that was passed to the
//                            c'tor of the ArgumentParser object, if any.
//                            The code will happily print a version
//                            (nothing, actually) if you've added this
//                            option but not provided a version(...) to the
//                            c'tor.
//
// Constraints:
//    It is possible to make the code (automatically) test + fail loudly if
//    values passed on the command line do not match constraint(s) set on
//    the option's value. The library provides the following built-in
//    constraint makers:
//
//
//      minimum_value(T t)      The converted value must be >= t
//
//      maximum_value(T t)                ..    ..          <= t
//
//      is_member_of({t1, ...}) The converted value must be an element
//                              of the set {t1, t2, ...}
//
//      minimum_size(unsigned)  The value must have a size of at least ...
//      maximum_size(unsigned)  The value must have a size of at most ...
//      exact_size(unsigned)           ... (make a wild guess ...)
//
//
//      constrain(F, string)    F is a callable object with signature:
//                                  ReturnType F(value const&)
//                              where ReturnType must be convertible to
//                              bool. Allows the user to supply a
//                              constraining function with description
//                              "string", to, well, put any constraint
//                              thinkable on a converted option's value.
//
//      match(....)             Shorthand to ease string-valued option
//                              value matching using std::regex. The 
//                              function call arguments are forwarded to 
//                              the constructor of std::regex, eg:
//                                match("[a-zA-Z]+") 
//    
//
// Requirements:
//  How often is the command line option allowed to be present?
//  Most common occurrences:
//    0 or more   ("optional")
//    1 or more   ("required")
//    0 or 1      ("optional")
//
//
//      at_least(unsigned n)    The command line option must be present
//                              at least n times
//
//      at_most(unsigned n)     The command line option may be present
//                              at most n times. The code ensures
//                              that no more than n options are processed;
//                              a fatal error will happen before even
//                              attempting to process the n+1'th occurrence
//
//      exactly(unsigned n)     Your guess.
//
//
// Extra:
//      set_default(T t)      Sets default value t of type T for an option, 
//                            if the option supports setting a default.
//                            Note: "char const*" is silently converted
//                                  to std::string
//
//      docstring(...)        Specify documentation for the program
//                            or the command line option.
//                            Should support any constructor that
//                            std::string supports.
//
//      version(T t)          T must be streamable to std::ostream 
//                            so feel free to give it anything you like
//
//      convert(F)            Allow user defined conversion of std::string
//                            to stored type. F is a callable object with
//                            signature:
//                              <type> F(std::string const&)
//                            The return type of the conversion is deduced
//                            from the signature of F and matched to the
//                            type of the option's action's stored type.
//
//                            The library has built-in conversion for the
//                            standard POD data types, including
//                            std::string.
//                            Using this functionality it is possible to
//                            convert-and-store user defined data types :-)
//
//
//  XOR grouping support:
//
//  In order to support exclusive-or, or mutually exclusive, options,
//  the addXOR(...) function on the command line object must be used.
//
//  That interface function expects >1 command line option objects so
//  something needs to be done to transform ".add(...)" into something that
//  can be passed multiple of these.
//
//  This can be constructed using the following function template:
//
//      option(...)           Inside the option(...) put the actions,
//                            constraints &cet you'd normally give to the 
//                            ".add(...)" command line object interface.
//
//                            So:
//                              .add(short_name('x'), store_true())
//                              .add(long_name('foo'), store_const(42), default(-1))
//
//                            Becomes, if they require to be mutually exclusive:
//                              .addXOR( option(short_name('x'), store_true()),
//                                       option(long_name('foo'), store_const(42), default(-1)),
//                                       /* more options, if desired*/ ... )
//                            Beware that it cannot be guaranteed that all
//                            pre- or postconditions will be honoured; e.g.
//                            an option being non-optional in this mutually
//                            exclusive situation is nonsense. The code will
//                            attempt to detect + complain loudly/bitterly
//                            about this.
//
#include <argparse_functools.h>
#include <argparse_basics.h>

#include <set>
#include <regex>
#include <tuple>
#include <limits>
#include <string>
#include <utility>
#include <functional>

namespace argparse { 
    namespace detail {
        ////////////////////////////////////////////////
        // Stuff that gets passed in by the usert can be
        // any one of these
        ////////////////////////////////////////////////

        struct name_t                {};
        struct action_t              {};
        struct default_t             {};
        struct version_t             {};
        struct docstring_t           {};
        struct conversion_t          {};
        struct constraint_template_t {};

        // There's three different types of constraints
        // 1. generic constraint on the value
        struct constraint: public constraint_impl {
            using constraint_impl::constraint_impl;
        };
        // 2./3. pre- and postcondition(s) on the 
        //       argument count
        struct precondition: public constraint_impl {
            using constraint_impl::constraint_impl;
        };
        struct postcondition: public constraint_impl {
            using constraint_impl::constraint_impl;
        };
        // 4. Precondition on the actual command line argument's string
        //    representation. Especially for pre-verification of arguments
        //    that will be transformed into non-POD types
        //    [e.g. user-defined converters could be simplified if they know
        //    the string that they'll be passed matches some specific
        //    format]
        struct formatcondition: public constraint_impl {
            using constraint_impl::constraint_impl;
        };


        template <>
        std::string demangle_f<argparse::detail::constraint>( void ) {
            return "constraint";
        }
        template <>
        std::string demangle_f<argparse::detail::precondition>( void ) {
            return "precondition";
        }
        template <>
        std::string demangle_f<argparse::detail::postcondition>( void ) {
            return "postcondition";
        }
        template <>
        std::string demangle_f<argparse::detail::formatcondition>( void ) {
            return "format";
        }

        //////////////////////////////////////////////////////////
        //
        //  Predicate to test if a type U actually `provides' the
        //  service T - wether it is an "instance-of".
        //
        //  We use "is_base_of<>" to see if T is derived from U
        //  to test this.
        //
        //////////////////////////////////////////////////////////
        template <typename T>
        struct provides {
            using real_type = typename std::decay<T>::type;
            template <typename U>
            struct test: std::is_base_of<real_type, typename std::decay<U>::type> {};
        };

        //////////////////////////////////////////////////////////
        //
        // Filter all values from a tuple that provide Desired
        //
        //////////////////////////////////////////////////////////
        template <typename Desired, typename T>
        auto get_all(T&& t) ->
            decltype( functools::filter_t<provides<Desired>::template test>(std::forward<T>(t)) ) {
                return functools::filter_t<provides<Desired>::template test>(std::forward<T>(t));
        }

        struct docstr_getter_t {
            docstr_getter_t() {}
            explicit docstr_getter_t(std::string const& pfx):
                __m_prefix(pfx)
            {}
            template <typename U>
            std::string operator()(U const& u) const {
                return __m_prefix+u.docstr();
            }
            template <typename U>
            std::string operator()(U const* u) const {
                return __m_prefix+u->docstr();
            }
            const std::string __m_prefix {};
        };

        //////////////////////////////////////////////////////////////////
        //  A struct holding a default for a value
        //////////////////////////////////////////////////////////////////
        template <typename T>
        struct Default: default_t {
            using type = typename std::decay<T>::type;

            explicit Default(type const& t):
                __m_default(t)
            {}

            std::string docstr( void ) const {
                return string_repr()( __m_default );
            }

            const type __m_default {};

            protected:
                Default() {}
        };

        /////////////////////////////////////////////////////////////////
        // Some predicates on Default objects
        /////////////////////////////////////////////////////////////////

        // if the value is std::ignore it's not defaultable
        struct is_actual_default {
            template <typename T>
            struct test: std::integral_constant<bool, !std::is_same<typename std::decay<decltype(std::declval<T>().__m_default)>::type, ignore_t>::value> {};
        };

        // tests if the type of the default matches the type of the
        // stored value whose' default it is to be
        template <typename T>
        struct is_ok_default {
            // A default object may store its __m_default as const so we test 
            // on the equality of the decayed types - we're only interested
            // that the underlying types actually match
            template <typename U>
            struct test: std::integral_constant<bool, std::is_same<typename std::decay<decltype(std::declval<U>().__m_default)>::type,
                                                                   typename std::decay<T>::type>::value> {};
        };

        // We can now apply "setting the default" over all defaults - it'll be 0
        // or 1 defaults being set ...
        struct default_setter_t {
            template <typename Defaulter, typename Option>
            void operator()(const Defaulter& def, Option& value) const {
                value->__m_value = def.__m_default;
                value->__m_count++; // indicate that the value was initialized
            }
        };

        // This functor actually tries to apply the default to the stored
        // value in order to make sure that the default does not violate
        // any of the constraints that the user may have placed on the
        // value
        struct default_constrainer_t {
            template <typename Defaulter, typename Constraints>
            void operator()(const Defaulter& def, Constraints&& constraints) const {
                try {
                    std::forward<Constraints>(constraints)( def.__m_default );
                }
                catch( std::exception const& e ) {
                    throw constraint_violation(std::string("The default violated a constraint: ")+e.what());
                }
            }
        };

    } // namespace detail

    /////////////////////////////////////////////////////////////////////////////////
    // These are exposed to the user:
    //  .add( ..., set_default(3), ...)
    //  .add( ..., set_default("/mnt/disk*"), ...)
    //
    // The type will be inferred from the argument; "char const*" will be
    // silently transformed to "std::string"
    //
    /////////////////////////////////////////////////////////////////////////////////
    template <typename T>
    auto set_default(T&& t) -> detail::Default<typename std::decay<T>::type> {
        return detail::Default<typename std::decay<T>::type>(std::forward<T>(t));
    }

    auto set_default(char const*const t) -> detail::Default<std::string> {
        return detail::Default<std::string>( std::string(t) );
    }


    /////////////////////// Conversion from string to ... ///////////////
    namespace detail {

#define std_convert(tref, s, fn) \
    do { std::size_t  n; std::string es;\
         try { tref = fn(s, &n); } \
         catch ( std::exception const& e ) { es = e.what(); n = (std::size_t)-1; } \
         if( s.size() && n!=s.size() ) \
            throw std::runtime_error(std::string("Failed to completely convert the value '") + s + "' "+es); \
    } while( false );

        // Converters from std::string => T
        struct std_conversion_t: conversion_t {
            void operator()(int& t, std::string const& s) const {
                std_convert(t, s, std::stoi);
            }
            void operator()(unsigned int& t, std::string const& s) const {
                unsigned long tmp{};
                std_convert(tmp, s, std::stoul);
                if( tmp>std::numeric_limits<unsigned int>::max() ) {
                    std::ostringstream oss;
                    oss << "converted value '" << tmp << "' > unsigned int max";
                    throw std::runtime_error(oss.str());
                }
                t = (unsigned int)tmp;
            }
            void operator()(long& t, std::string const& s) const {
                std_convert(t, s, std::stol);
            }
            void operator()(unsigned long& t, std::string const& s) const {
                std_convert(t, s, std::stoul);
            }
            void operator()(long long& t, std::string const& s) const {
                std_convert(t, s, std::stoll);
            }
            void operator()(unsigned long long& t, std::string const& s) const {
                std_convert(t, s, std::stoull);
            }
            void operator()(float& t, std::string const& s) const {
                std_convert(t, s, std::stof);
            }
            void operator()(double& t, std::string const& s) const {
                std_convert(t, s, std::stod);
            }
            void operator()(std::string& t, std::string const& s) const {
                t = s;
            }
            // Things that we ignore we ignore
            void operator()(ignore_t& , std::string const&) const { }
        };

#undef std_convert

    } // namespace detail


    ///////////////////////////////////////////////////////////////////////////////
    //
    //  Deal with simple flags; true/false command line options - their
    //  value depends just on their presence/absence on the command line.
    //
    //  They automatically provide a default which is the complement of the
    //  action.
    //
    ///////////////////////////////////////////////////////////////////////////////
    namespace detail {
        template <bool Value>
        struct StoreFlag: action_t, Default<bool> {

            // Not all command lines have the stored type equal to the element type 
            using type         = bool;
            using element_type = ignore_t;

            StoreFlag(): Default<bool>(!Value), __m_value(Value) {}

            template <typename U>
            void operator()(type& value, U const&) const {
                value = __m_value;
            }
            const bool __m_value;
        };
    } // namespace detail

    // These are exposed to the user
    using store_true  = detail::StoreFlag<true>;
    using store_false = detail::StoreFlag<false>;

    //////////////////////////////////////////////////////////////
    //
    // If the command line option is present, this action stores
    // a constant into its value. May or may not be combined with 
    // a default - up to you
    //
    //////////////////////////////////////////////////////////////
    namespace detail {
        template <typename T>
        struct StoreConst: action_t {
            // We don't convert types but we do keep 'm
            using type         = typename std::decay<T>::type;
            using element_type = ignore_t;

            StoreConst() = delete;
            StoreConst(type const& t): __m_value(t) {}

            template <typename U>
            void operator()(type& value, U const&) const {
                value = __m_value;
            }
            const T __m_value;
        };

        // When storing into something we don't allow setting a default
        template <typename T>
        struct StoreConstInto: action_t, Default<ignore_t> {
            // We don't convert types but we do keep 'm
            using type         = typename std::decay<T>::type;
            using element_type = ignore_t;
            using my_type      = std::reference_wrapper<type>;

            StoreConstInto() = delete;
            StoreConstInto(type const& t, my_type tr): __m_value(t), __m_ref(tr) {}

            template <typename U>
            void operator()(type&, U const&) const {
                __m_ref.get() = __m_value;
            }
            const T __m_value;
            my_type __m_ref;
        };
    } // namespace detail

    // These are the API functions exposed to the user.
    // char const* is silently converted to std::string
    template <typename T>
    auto store_const(T const& t) -> detail::StoreConst<T> {
        return detail::StoreConst<T>(t);
    }
    auto store_const(char const *const t) -> detail::StoreConst<std::string> {
        return detail::StoreConst<std::string>( std::string(t) );
    }

    template <typename T>
    auto store_const_into(T const& t, T& tref) -> detail::StoreConstInto<T> {
        return detail::StoreConstInto<T>(t, std::ref(tref));
    }
    auto store_const_into(char const *const t, std::string& tref) -> detail::StoreConstInto<std::string> {
        return detail::StoreConstInto<std::string>(std::string(t), tref);
    }

    //////////////////////////////////////////////////////////////////////////
    //
    //  count() allows the user to, well, count how often the command line
    //  option was present.
    //  (e.g. "ssh -vvv" : more 'v's increase verbosity level)
    //
    //  Automatically sets default of 0
    //
    //////////////////////////////////////////////////////////////////////////
    struct count: detail::action_t, detail::Default<unsigned int> {
        using type         = unsigned int;
        using element_type = detail::ignore_t;

        count() : detail::Default<unsigned int>(0u), __m_value(0) {}
        template <typename U>
        void operator()(type& value, U const&) const {
            __m_value = ++value;
        }
        mutable type __m_value;
    };

    //////////////////////////////////////////////////////////////////////////
    //
    // count_into - allow the user to specify a variable to count into
    //
    // The type must be of the arithmetic type and will be initialized
    // with a value of 0 automatically.
    //
    //////////////////////////////////////////////////////////////////////////
    namespace detail {
        template <typename T>
        struct count_into_t: action_t {
            using type         = ignore_t;
            using element_type = ignore_t;
            using my_type      = std::reference_wrapper<typename std::decay<T>::type>;

            count_into_t() = delete;
            explicit count_into_t(my_type t): __m_ref(t) { __m_ref.get() = T(0); }

            template <typename U, typename V>
            void operator()(U& , V const&) const {
                __m_ref.get() = __m_ref.get() + 1;
            }
            my_type __m_ref;
        };
    } // namespace detail

    // these get exposed to the user
    template <typename T,
              typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
    auto count_into(T& t) -> detail::count_into_t<T> {
        return detail::count_into_t<T>( std::ref(t) );
    }

    //////////////////////////////////////////////////////////////////////////
    //
    //  collect<T>()   transform the arguments of the option to type T
    //                 and collect them, by default, in std::list<T>.
    //                 A second, template template, parameter can be given
    //                 to set a different container type:
    //                      collect<int, std::set>()  will collect ints into
    //                      a std::set<int> ...
    //
    //////////////////////////////////////////////////////////////////////////
    // One can set a default for these buggers
    template <typename T, template <typename...> class Container = std::list, typename... Details>
    struct collect: detail::action_t {
        // We store a container-of-T's but the command line arguments
        // get converted to element-of-what-we-store 
        using type         = Container<typename std::decay<T>::type, Details...>;
        using element_type = typename type::value_type;

        // We can only support containers where the value_type == T!
        static_assert(std::is_same<element_type, T>::value,
                      "Can only collect into containers that store elements of the requested type");

        template <typename U>
        void operator()(type& value, U const& u) const {
            value.insert(value.end(), u);
        }
    };

    //////////////////////////////////////////////////////////////////////////
    //
    // collect_into(T& t) -> 
    //
    // Two supported options for T:
    //  1.  T is an output iterator we can store converted values into,
    //      using "*t++ = <converted value>"
    //      The type of the value to be stored/converted is inferred from 
    //      the output iterator
    //
    //  2.  T is a container type, supporting ".insert(iterator, value)"
    //      The type of the value to be stored/converted is inferred from
    //      the container
    //
    //  We don't allow defaults to be set for this one
    //
    //////////////////////////////////////////////////////////////////////////
    namespace detail {
        // Some output iterators have member "::container_type", some don't.
        // If they have it, we should take element type from
        // "::container_type::value_type", otherwise we should just use
        // "::value_type"
        template <typename T>
        struct has_container_type {
            using   no  = char;
            using   yes = unsigned int;

            template <typename U>
            static no  test(U*);

            template <typename U>
            static yes test(typename U::container_type*);

            static const bool value = (sizeof(test<T>(nullptr))==sizeof(yes));
        };

        // The collector which accumulates into an output_iterator
        template <typename T, typename Element>
        struct Collector: action_t, Default<ignore_t> {
            using my_type      = std::reference_wrapper<typename std::decay<T>::type>;
            using type         = ignore_t;
            using element_type = typename std::decay<Element>::type;

            Collector() = delete;
            explicit Collector(my_type t): __m_ref(t) {}

            template <typename U>
            void operator()(type& , U const& u) const {
                *__m_ref.get()++ = u;
            }

            my_type __m_ref;
        };
        // The collector which accumulates into a container directly
        template <typename T>
        struct ContainerCollector: action_t, Default<ignore_t> {
            using my_type      = std::reference_wrapper<typename std::decay<T>::type>;
            using type         = ignore_t;
            using element_type = typename std::decay<typename T::value_type>::type;

            ContainerCollector() = delete;
            explicit ContainerCollector(my_type t): __m_ref(t) {}

            template <typename U>
            void operator()(type& , U const& u) const {
                __m_ref.get().insert(__m_ref.get().end(), u);
            }

            my_type __m_ref;
        };
    } //namespace detail

    // These are exposed to the user

    // Some output iterators have "::container_type" as member typedef - if
    // they do, take the element type from there ...
    template <typename T,
              typename std::enable_if<functools::detail::is_output_iterator<T>::value, int>::type = 0,
              typename std::enable_if<detail::has_container_type<T>::value, int>::type = 0>
    auto collect_into(T& t) -> detail::Collector<T, typename T::container_type::value_type> {
        return detail::Collector<T, typename T::container_type::value_type>(std::ref(t));
    }

    // ... and for those iterators who *don't* have ::container_type, just use the
    // ::value_type as element type
    template <typename T,
              typename std::enable_if<functools::detail::is_output_iterator<T>::value, int>::type = 0,
              typename std::enable_if<!detail::has_container_type<T>::value, int>::type = 0>
    auto collect_into(T& t) -> detail::Collector<T, typename T::value_type> {
        return detail::Collector<T, typename T::value_type>(std::ref(t));
    }

    // Sometimes ppl say they prefer to collect into a container directly
    template <typename T,
              typename std::enable_if<detail::can_insert<T>::value, int>::type = 0>
    auto collect_into(T& t) -> detail::ContainerCollector<T> {
        return detail::ContainerCollector<T>(std::ref(t));
    }

    //////////////////////////////////////////////////////////////////////////
    //
    // store_value<T>()  converts option argument to type T and stores it
    //
    //////////////////////////////////////////////////////////////////////////
    template <typename T>
    struct store_value: detail::action_t {
        // We store the converted value
        using type         = typename std::decay<T>::type;
        using element_type = type;

        template <typename U>
        void operator()(type& value, U const& u) const {
            value = u;
        }
    };


    //////////////////////////////////////////////////////////////////////////
    //
    // store_into(T& t) converts option argument into type inferred from
    // the variable which is referenced
    //
    //////////////////////////////////////////////////////////////////////////
    namespace detail {
        template <typename T>
        struct StoreInto: action_t, Default<ignore_t> {
            using type         = ignore_t;
            using element_type = typename std::decay<T>::type;
            using my_type      = std::reference_wrapper<element_type>;

            StoreInto() = delete;
            explicit StoreInto(my_type t): __m_ref(t) {}

            template <typename U>
            void operator()(type& , U const& u) const {
                __m_ref.get() = u;
            }

            my_type __m_ref;
        };
    } // namespace detail

    // These are exposed to the user
    template <typename T>
    auto store_into(T& t) -> detail::StoreInto<T> {
        return detail::StoreInto<T>(std::ref(t));
    }

    ///////////////////////////////////////////////////////////////////////////////////
    //
    // print_help() and print_usage() are methodcallers - these options
    // do not actually convert or store anything
    //
    ///////////////////////////////////////////////////////////////////////////////////
    namespace detail {
        template <typename Function>
        struct methodcaller_t: action_t {
            using type         = ignore_t;
            using element_type = ignore_t;

            explicit methodcaller_t(Function const& f): __m_value( f ) {}

            template <typename T, typename U>
            void operator()(T t, U const&) const {
                (const_cast<methodcaller_t<Function>*>(this))->__m_value(t);
            }

            Function __m_value;
        };

        template <typename F>
        auto mk_methodcaller(F&& f) -> methodcaller_t< typename std::decay<F>::type > {
            return methodcaller_t< typename std::decay<F>::type >(std::forward<F>(f));
        }
    } // namespace detail

    // These get exposed to the usert
    auto print_usage( void ) ->
        decltype( detail::mk_methodcaller( std::bind(std::mem_fn(&CmdLineBase::print_help), std::placeholders::_1, true)) ) {
        return detail::mk_methodcaller( std::bind(std::mem_fn(&CmdLineBase::print_help), std::placeholders::_1, true));
    }

    auto print_help( void ) ->
        decltype( detail::mk_methodcaller( std::bind(std::mem_fn(&CmdLineBase::print_help), std::placeholders::_1, false)) ) {
        return detail::mk_methodcaller( std::bind(std::mem_fn(&CmdLineBase::print_help), std::placeholders::_1, false));
    }

    auto print_version( void ) ->
        decltype( detail::mk_methodcaller( std::bind(std::mem_fn(&CmdLineBase::print_version), std::placeholders::_1)) ) {
        return detail::mk_methodcaller( std::bind(std::mem_fn(&CmdLineBase::print_version), std::placeholders::_1));
    }


    ///////////////////////////////////////////////////////////////////////////////////
    //
    //  Provide documentation for the program or the command line option
    //
    ///////////////////////////////////////////////////////////////////////////////////
    struct docstring: std::string, detail::docstring_t {
        using std::string::string;
        std::string const& docstr( void ) const {
            return *this;
        }
    };


    ///////////////////////////////////////////////////////////////////////////////////
    //
    //  Constraint machinery
    //
    ///////////////////////////////////////////////////////////////////////////////////
    namespace detail {
        template <typename L, typename Category>
        struct wrap_category: Category {
            using Left = typename std::decay<L>::type;
            using Category::Category;
        };

        // This implements a binary operation OPERATION(value, limit)
        // to test if value meets the constraint set by limit under the
        // operation OPERATION - e.g. "std::less<int>(value, 7)"
        //
        // Constraint factory - can separate left/right types as long
        // as "OP<Left>(left, Right())" is defined ...
        //
        template <typename L, typename Category, template <typename...> class OP,
                  typename Left = typename std::decay<L>::type>
        struct constraint_op: Category {
            using Result = wrap_category<Left, Category>;

            // "::mk(...)  can now be passed more template arguments, if necessary:
            //       constraint_op<...>::template mk<X, Y, Z>(X const&, std::string const&)
            //         which will instantiate OP<X, Y, Z> in stead of the
            //         default "OP<Left>"
            template <typename Right, typename... Rest>
            static Result mk(Right const& right, std::string const& descr = std::string()) {
                return Result(build_string(descr, " ", op2str<OP>(), " ", right),
                              Constraint<Left>([=](Left const& left) { return OP<Left, Rest...>()(left, right); }));
            }
        };

        // Implements a constraint function such that a value is
        // tested using "f(value)" in order to see if value meets
        // the constraints implemented by "f(...)"
        template <typename L, typename Category>
        struct constraint_fn: Category {
            using Left = typename std::decay<L>::type;
            using Self = constraint_fn<L, Category>;

            template <typename F>
            constraint_fn(F&& f, std::string const& descr):
                Category(descr, Constraint<Left>([=](Left const& left) { return f(left); }))
            {}

            template <typename F>
            static Self mk(F&& f, std::string const& descr) {
                return Self(Constraint<Left>([=](Left const& left) { return static_cast<bool>(f(left)); }), descr);
            }
        };

        // Put a constraint on the size (length) of a value
        // (can be std::container, std::string etc, anything that
        //  supports ".size()")
        template <typename Category, template <typename...> class OP>
        struct size_constrain: public constraint_template_t {
            using category = Category;

            template <typename U>
            struct size_constrain_t: Category {
                size_constrain_t(std::size_t limit, std::string const& msg):
                    Category(msg, Constraint<U>([=](U const& u){ return OP<std::size_t>()(u.size(), limit); }))
                {};
            };

            template <typename U>
            size_constrain_t<U> mk( void ) const {
                return size_constrain_t<U>(__m_size, __m_docstr);
            }

            size_constrain(std::size_t n, std::string const& descr="size"):
                __m_size(n), __m_docstr( build_string(descr, " ", op2str<OP>(), " ", __m_size) )
            {}
            const std::size_t __m_size;
            const std::string __m_docstr;
        };
    } // namespace detail

    ///////////////////////////////////////////////////////////////////////////////////////
    //
    // simple constraint makers that are exposed to the user
    //
    ///////////////////////////////////////////////////////////////////////////////////////
    template <typename T>
    auto minimum_value(T const& t) -> typename detail::constraint_op<T, detail::constraint, std::greater_equal>::Result {
        return detail::constraint_op<T, detail::constraint, std::greater_equal>::mk(t, "minimum value");
    }
    template <typename T>
    auto maximum_value(T const& t) -> typename detail::constraint_op<T, detail::constraint, std::greater_equal>::Result {
        return detail::constraint_op<T, detail::constraint, std::less_equal>::mk(t, "maximum value");
    }

    template <typename T>
    auto is_member_of(std::initializer_list<T> il) ->
        typename detail::constraint_op<T, detail::constraint, detail::member_of_t>::Result {
      return detail::constraint_op<T, detail::constraint,
                                   detail::member_of_t>::template mk<std::set<T>, std::set<T>>(std::set<T>(il), "value");
    }

    auto is_member_of(std::initializer_list<char const* const> il) -> 
        typename detail::constraint_op<std::string, detail::constraint, detail::member_of_t>::Result {
      return detail::constraint_op<std::string, detail::constraint,
                                   detail::member_of_t>::template mk<std::set<std::string>, std::set<std::string>>(std::set<std::string>(
                                               std::begin(il), std::end(il)), "value");
    }

    using minimum_size = detail::size_constrain<detail::constraint, std::greater_equal>;
    using maximum_size = detail::size_constrain<detail::constraint, std::less_equal>;
    using exact_size   = detail::size_constrain<detail::constraint, std::equal_to>;


    ///////////////////////////////////////////////////////////////////////////////////////
    //
    // Requirements are pre/post conditions on the amou Pre- and/or post
    // conditons on the amount of times the command line option/argument may
    // be present. The most usual constraints are:
    //    0 or more   ("optional")
    //    1 or more   ("required")
    //    0 or 1      ("optional")
    //
    // which can easily implemented through the two primitives
    // "at_most/at_least":
    //
    //    1. the default, with no constraints given, implies 0 or more
    //    2. at least N implies N or more, thus "1 or more" is "at_least(1)"
    //    3. at most N implies, well, 0 to N, thus "0 or 1" is "at_most(1)"
    //    4. a range N->M can easily be formed by combining "at_least(N), at_most(M)"
    //
    auto at_least(unsigned int n) -> typename detail::constraint_op<unsigned int, detail::postcondition, std::greater_equal>::Result {
        return detail::constraint_op<unsigned int, detail::postcondition, std::greater_equal>::mk(n, "argument count");
    }

    // Note: at_most(n) means that the precondtion has to be "< n" because,
    //       in fact, this could have been implemented as a post condition but 
    //       then the action has already executed. The point of "at_most()"
    //       should be to ensure that the action is /never/ executed more than
    //       n times. So we turn it into a precondition on (n-1) such that the
    //       action may proceed to exactly satisfy the post condition but no more.
    auto at_most(unsigned int n) -> typename detail::constraint_op<unsigned int, detail::precondition, std::less>::Result {
        if( n<1 )
            throw std::logic_error("at_most() with requirement < 1 makes no sense at all.");
        return detail::constraint_op<unsigned int, detail::precondition, std::less>::mk(n, "argument count");
    }

    namespace detail {
        // For potential convenience there is also the exactly N, which could be 
        // made out of the combination "at_least(N), at_most(N)" but that is 
        // less efficient and less intuitive
        template <typename T, template<typename...> class OP>
        auto mk_op(std::string const& descr, T const& r) -> std::pair<std::string, Constraint<T>> {
            return std::make_pair(build_string(descr, " ", op2str<OP>(), " ", r), [=](T const& l) { return OP<T>()(l, r);});
        }

        template <typename L, typename... Category>
        struct wrap_many: Category... {
            using Left = L;
            using Self = wrap_many<L, Category...>;

            // There's 4 stages to building this object
            // they're here in reverse order because the next one calls the previous
            // one.
            //
            // In execution order they are:
            //   1. "mk<Operators>(std::string const& s, Left const& right)"
            //      construct a tuple of pairs of <string, unary-function-on-Left>
            //      the string is the readable description of the operator,
            //      the unary function is an instantiation of Operator<Left> with
            //      right-hand-side fixed to `right`
            //   2. The tuple of pairs is 'zipped' into a tuple<Category...>
            //      by using parameter pack expansion; each Category is
            //      constructed as Category(get<Idx>().first, get<Idx>().second)
            //   3. The tuple of constructed Category objects is fed to the next
            //      maker, which unpacks that tuple again and feeds the unpacked
            //      Category... instances to the actual constructor of Self,
            //      which in turn calls the base-class constructors with those
            //      arguments, in order :-)
            //   I think it has to be done like this because AFAIK the parameter
            //   pack expansion (eg. on "Idx...") can only happen once. So for each
            //   level of unpacking you need to pass tuple+Idx...
            template <typename... Fs,
                      typename std::enable_if<sizeof...(Fs)==sizeof...(Category), int>::type = 0>
            wrap_many(Fs&&... fs):
                Category(std::forward<Fs>(fs))...
            {}

            template <typename Cats, std::size_t... Idx>
            static auto mk3(Cats&& cats, functools::detail::index_sequence<Idx...>) -> Self {
                return Self( std::get<Idx>(std::forward<Cats>(cats))... );
            }
            template <typename Ops, size_t... Idx>
            static auto mk2(Ops&& ops, functools::detail::index_sequence<Idx...> idx) -> Self {
                return Self::mk3(std::tuple<Category...>( {std::get<Idx>(std::forward<Ops>(ops)).first,
                                                          std::get<Idx>(std::forward<Ops>(ops)).second}... ), idx);
            }
            template <template <typename...> class... OP>
            static auto mk(std::string const& descr, Left const& r) -> Self { 
                return Self::mk2(std::make_tuple(mk_op<Left, OP>(descr, r)...), typename functools::mk_sequence<0, sizeof...(OP)>::type());
            }
        };
        
        using exactly_t = wrap_many<unsigned int, precondition, postcondition>;
    } // namespace detail


    auto exactly(unsigned int n) -> detail::exactly_t {
        // Exactly "0" makes as much sense as something that doesn't make sense at all
        if( n<1 )
            throw std::logic_error("exactly() with requirement < 1 makes no sense at all.");
        return detail::exactly_t::mk<std::less, std::equal_to>("argument count", n);
    }

    /////////////////////////////////////////////////////////////////////////////////////////
    //  
    //  Allow registering a function that implements any constraint on the
    //  value (can use a lambda)
    //
    //  The function f has to have signature:
    //      <convertible-to-bool> f(value const&)
    //
    /////////////////////////////////////////////////////////////////////////////////////////
    template <typename T,
              typename std::enable_if<!std::is_bind_expression<T>::value, int>::type = 0,
              typename std::enable_if<detail::is_unary_fn< detail::deduce_signature<T> >::value, int>::type = 0,
              typename Arg = typename std::decay<typename std::tuple_element<0, typename detail::deduce_signature<T>::argument_type>::type>::type,
              typename Ret = typename std::decay< typename detail::deduce_signature<T>::return_type >::type>
    auto constrain(T&& t, std::string const& descr = "lazy user did not supply a description")
        -> detail::constraint_fn<Arg, detail::constraint>  {
            // Whatever the constraint returns, we must be able to convert it to bool
            static_assert( std::is_convertible<Ret, bool>::value,
                           "A constraint's return value must be convertible to bool" );
            return detail::constraint_fn<Arg, detail::constraint>::mk(std::forward<T>(t), descr);
    }

    // And a cooked regex match constraint for string values
    template <typename T, typename... Ts>
    auto match(T&& t, Ts&&... ts) -> detail::constraint_fn<std::string, detail::formatcondition> {
        std::regex    rx( std::forward<T>(t), std::forward<Ts>(ts)... );
        return detail::constraint_fn<std::string, detail::formatcondition>(
                        [=](std::string const& s) { return std::regex_match(s, rx); },
                        std::string("match ")+t);
    }

    
    /////////////////////////////////////////////////////////////////////////////////////////
    //  
    //  Allow user-defined conversion from string to whatever type by
    //  supplying a function F  with signature "<value_type> F(std::string const&)"
    //
    /////////////////////////////////////////////////////////////////////////////////////////
    namespace detail {
        template <typename Ret>
        struct user_conversion_t: conversion_t {
            using type = std::function<Ret(std::string const&)>;

            user_conversion_t() = delete;
            explicit user_conversion_t(type const& t): __m_cvt(t) {}

            void operator()(Ret& r, std::string const& s) const {
                r = __m_cvt(s);
            }
            type __m_cvt;
        };
    }

    // This is exposed to the user
    template <typename T,
              typename std::enable_if<!std::is_bind_expression<T>::value, int>::type = 0,
              typename std::enable_if<detail::is_unary_fn< detail::deduce_signature<T> >::value, int>::type = 0,
              typename Arg = typename std::decay<typename std::tuple_element<0, typename detail::deduce_signature<T>::argument_type>::type>::type,
              typename Ret = typename std::decay< typename detail::deduce_signature<T>::return_type >::type>
    auto convert(T&& t) -> detail::user_conversion_t< Ret > {
        static_assert( std::is_same<Arg, std::string>::value,
                       "A converter's argument type must be std::string" );
        return detail::user_conversion_t<Ret>( std::forward<T>(t) );
    }


    /////////////////////////////////////////////////////////////////////////////////////////
    //
    // Here follow some internal constraints e.g. on the acceptable
    // long/short names of command line options and the constraint
    // machinery.
    //
    /////////////////////////////////////////////////////////////////////////////////////////
    namespace detail {
        struct value_constraint_t: public constraint_impl {
            using constraint_impl::constraint_impl;
        };

        // short name options may only be alpha, at the moment
        struct acceptable_short_name: constraint_fn<char, value_constraint_t> {
            acceptable_short_name():
                constraint_fn<char, value_constraint_t>([](char const& c) { return ::isalpha(c); }, "short name character ::isalpha(...)")
            {}
        };

        // long name options must have a minimum length of 2
        using minimum_size_v = size_constrain<value_constraint_t, std::greater_equal>;
        struct acceptable_long_name: minimum_size_v {
            acceptable_long_name():
                minimum_size_v(2, "long name length")
            {}
        };

        // When dealing with constraints there's two policies:
    
        // 1. just collect all the constraints
        struct CollectConstraints {
            template <typename T>
            static auto handle_result(T const& t) -> T {
                return t;
            }
        };
        // 2. enforce them and throw up in case a constraint fails
        struct ExecuteConstraints {
            template <typename T>
            static auto handle_result(T const& t) -> T {
                if( !t.empty() )
                    throw constraint_violation(t);
                return t;
            }
        };

        // A functor that processes a number of constraints of a particular
        // category according to a specific policy
        template <typename Category, typename Policy>
        struct constrainer {
            template <typename T, typename U>
            auto operator()(T const& t, U const& u) const
                -> decltype( Policy::handle_result(dynamic_cast<Category const&>(t).constrain(u)) ){
                    return Policy::handle_result( dynamic_cast<Category const&>(t).constrain(u) );
            }
        };

        // predicate to filter specific constraint categories
        template <typename Category>
        struct is_category {
            template <typename T>
            struct test: std::is_same<Category, typename T::category> {};
        };

        // If something is a constraint template, it must be instantiated
        // for the actual type of value that is to be constrained
        template <typename Requested>
        struct instantiator {
            // Assume T is derived from constraint_template_t
            // then ask it to instantiate a real constraint for type Requested
            template <typename T>
            auto operator()(T const& t) const -> decltype( t.template mk<Requested>() ) {
                return t.template mk<Requested>();
            }
        };

        // A gruesome template - build a constraint function which 
        // aggregates all constraints of type Category for a value of type
        // Element according to constraint policy Policy ...
        template <typename Category, typename Element, typename Policy>
        struct constraint_maker {
            using element_type = typename std::decay<Element>::type;
            using Self         = constraint_maker<Category, Element, Policy>;

            template <typename U>
            struct incompatible_element: std::integral_constant<bool, !std::is_same<element_type, typename U::Left>::value> {};

            struct typecast {
                template <typename T>
                Category const* operator()(T const& t) const {
                    return dynamic_cast<Category const*>(&t);
                }
            };
            template <typename Props>
            static ConstraintS<element_type> mk(docstringlist_t& docs, Props&& props) {
                // For all direct constraints, assert that the left type of the
                // operand is, in fact, Element
                auto   direct       = get_all<Category>( std::forward<Props>(props) );
                using  incompatible = typename functools::filter_p<Self::incompatible_element, decltype(direct)>::type;

                static_assert( std::tuple_size<incompatible>::value==0,
                               "There is a type mismatch between given constraint(s) and the target type to constrain" );
                // Check if there are any constraint templates - for those we don't
                // have to check wether the Left type matches; we'll /instantiate/
                // them for the correct type :-)
                auto   templates    = get_all<constraint_template_t>( std::forward<Props>(props) );

                // Out of the templates, filter the ones that have the correct category
                auto   remain       = functools::filter_t<is_category<Category>::template test>(templates);
                // Transform those into actual constraints
                auto   instances    = functools::map(remain, instantiator<element_type>());

                // Now that we have all constraints, we can build a function that
                // asserts all of them
                auto   allConstraints = std::tuple_cat(direct, instances);

                // Capture the docstrings, filtering out the ones that are empty
                functools::filter_v(
                            [](std::string const& s){ return !s.empty(); },
                            functools::map(functools::map(allConstraints, typecast()), docstr_getter_t(demangle_f<Category>()+":")),
                            std::inserter(docs, docs.end()) );
                return ConstraintS<element_type>(
                        [=](element_type const& value) {
                            std::ostringstream oss;
                            // Apply all constraints to the value and collect the violations
                            functools::filter_v([](std::string const& s){ return !s.empty(); },
                                                functools::map(allConstraints, constrainer<Category, Policy>(), value),
                                                std::ostream_iterator<std::string>(oss, ", "));
                            return oss.str();
                        });
            }
        };

        // Specialize for ignore_t 
        template <typename Category, typename Policy>
        struct constraint_maker<Category, ignore_t, Policy> {
            template <typename Props>
            static ConstraintS<ignore_t> mk(docstringlist_t&, Props&&) {
                return [](ignore_t const&) { return std::string(); };
            }
        };

    } // namespace detail

    ///////////////////////////////////////////////////////////////////////////////////////////
    //
    // Holding values + command line option names
    //
    ///////////////////////////////////////////////////////////////////////////////////////////
    namespace detail {
        // The value_holder may have compile-time constraints listed in Props...
        template <typename T, typename... Props>
        struct value_holder {
            // default c'tor is present but does not actually initialize
            value_holder() {}
            explicit value_holder(T const& t): __m_value( t ) {
                // Apply the __m_value to all compile-time constraints
                docstringlist_t  tmp;
                auto constraintf = constraint_maker<value_constraint_t, T, ExecuteConstraints>::mk(tmp, std::tuple<Props...>() );
                constraintf( __m_value );
            }
            T  __m_value;
        };
    }

    // Expose the long/short name option name constructors to the usert
    struct long_name: detail::name_t, detail::value_holder<std::string, detail::acceptable_long_name> {
        // we cannot have default c'tor!
        long_name() = delete;
        using value_holder::value_holder;
    };
    struct short_name: detail::name_t, detail::value_holder<char, detail::acceptable_short_name> {
        short_name() = delete;
        using value_holder::value_holder;
    };


    ///////////////////////////////////////////////////////////////////////////////////////////
    //
    // Describe the version of the program
    //
    ///////////////////////////////////////////////////////////////////////////////////////////
    namespace detail {
        //////////////////////////////////////////////////////////////////
        //  A struct holding something that describes the version
        //////////////////////////////////////////////////////////////////
        template <typename T>
        struct Version: version_t, value_holder<T> {
            using value_holder<T>::value_holder;

            Version() = delete;
        };
        template <typename T, typename... Details>
        std::basic_ostream<Details...>& operator<<(std::basic_ostream<Details...>& os, Version<T> const& v) {
            return os << v.value_holder<T>::__m_value;
        }

        struct NullVersion: version_t { };
        template <typename... Details>
        std::basic_ostream<Details...>& operator<<(std::basic_ostream<Details...>& os, NullVersion const&) {
            return os;
        }
    }

    // Constraints on construction a version object is that it
    // must be streamable
    template <typename T, typename Type = typename std::decay<T>::type,
              typename std::enable_if<detail::is_streamable<Type>::value, int>::type = 0>
    auto version(T const& t) -> detail::Version<Type> {
        return detail::Version<Type>(t);
    }

    // specialization for char const* => silently transform to std::string
    auto version(char const* const s) -> detail::Version<std::string> {
        return detail::Version<std::string>(s);
    }


    // Take a number of arguments and turn them into a tuple ...
    // really this is just shorthand for std::make_tuple ... :-)
    template <typename... Props>
    auto option(Props&&... props) -> std::tuple<Props...> {
        return std::make_tuple(std::forward<Props>(props)...);
    }

} // namespace argparse

#endif
