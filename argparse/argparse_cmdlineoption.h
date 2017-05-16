// Deal with a single command line option
#ifndef ARGPARSE_CMDLINEOPTION_H
#define ARGPARSE_CMDLINEOPTION_H

#include <argparse_basics.h>
#include <argparse_actions.h>

#include <memory>
#include <iterator>
#include <iostream>
#include <algorithm>
#include <functional>

namespace argparse { namespace detail {

    ///////////////////////////////////////////////////////////////////////////////////////////////
    //                            the command line objects
    ///////////////////////////////////////////////////////////////////////////////////////////////

    template <typename C, typename Iter>
    void maybe_print(std::string const& topic, C const& c, Iter& iter) {
        if( !c.empty() )
            std::copy(std::begin(c), std::end(c), *iter++ = topic);
    }

    // forward-declarartions so's we can have pointers-to
    struct CmdLineOptionIF;
    template <typename T> struct CmdLineOptionStorage;

    using CmdLineOptionPtr      = std::shared_ptr<CmdLineOptionIF>;
    using ConstCmdLineOptionPtr = std::shared_ptr<CmdLineOptionIF const>;

    // Non-templated baseclass
    struct CmdLineOptionIF {
        using condition_f = std::function<void(unsigned int)>;

        friend class argparse::ArgumentParser;

        template <typename... Props>
        friend CmdLineOptionPtr mk_argument(CmdLineBase*, Props&&...);

        CmdLineOptionIF(): 
            __m_requires_argument( false ),
            __m_count( 0 ),
            __m_precondition_f( nullptr ), __m_postcondition_f( nullptr )
        {}

        virtual std::string docstring( void ) const {
            return std::string();
        }

        // Make sure we get a non-const lvalue reference
        // we don't want r-value refs
        template <typename T>
        bool get(T& t) const {
            // See if we can fulfill the request
            using theType                               = typename std::decay<T>::type;
            CmdLineOptionStorage<theType> const* upcast = dynamic_cast<CmdLineOptionStorage<theType> const*>(this);

            if( !upcast )
                throw std::runtime_error("Bad cast - requested option type is not actual type");
            return upcast->get(t);
        }
        template <typename T>
        T const& get( void ) const {
            // See if we can fulfill the request
            using theType                               = typename std::decay<T>::type;
            CmdLineOptionStorage<theType> const* upcast = dynamic_cast<CmdLineOptionStorage<theType> const*>(this);

            if( !upcast )
                throw std::runtime_error("Bad cast - requested option type is not actual type");
            return upcast->get();
        }

        // Process an actual command line argument
        virtual void processArgument(std::string const&) = 0;
        
        virtual      ~CmdLineOptionIF() {}

        // We could protect these members but that wouldn't be a lot of use
        bool             __m_requires_argument;
        bool             __m_required;
        // pre-format the option's name(s), required/optional and, if applicable, type of argument:
        std::string      __m_usage; 
        unsigned int     __m_count;
        namecollection_t __m_names;
        docstringlist_t  __m_docstring;
        docstringlist_t  __m_defaults;
        docstringlist_t  __m_constraints;
        docstringlist_t  __m_requirements;

        // Print the help for this option
        void mini_help(std::ostream& os, bool usage) const {
            os << __m_usage << std::endl;
            if( !usage ) {
                std::ostream_iterator<std::string> lineprinter(std::cout, "\n\t");
                // Print any documentation
                maybe_print("\r\t", __m_docstring,    lineprinter);
                // Print only one default, if there's any
                if( __m_defaults.size() )
                    os << "\r\tdefault: " << *__m_defaults.begin() << std::endl;
                // list constraints and requirements
                maybe_print("\r\t", __m_constraints,  lineprinter);
                maybe_print("\r\t", __m_requirements, lineprinter);
            }
            os << "\n";
        }

        protected:
            // If an option has a default then it can override
            // this one to set it
            virtual void set_default( void ) { }

            condition_f __m_precondition_f;
            condition_f __m_postcondition_f;
        private:
    };


    // We have to discriminate between two stages:
    //   1. storing what was collected from the command line
    //   2. processing the individual bits from the command line
    // and they don't need to be equal ...
    //
    // To wit, the "collect" action collectes elements of type X 
    // and stores them in type Y.
    // Some actions ignore the value on the command line (Y==ignore)
    // but still store some value of type X.
    // Sometimes X == Y, for the simple store_value<Y> actions
    //
    // Default should apply to X, constraints to Y?
    // (But suppose action = store const and user set default and constraints,
    //  then Y==ignore then still need to apply constraint to default ...)
    //
    //  So build a hierarchy:
    //
    //   CmdLineOptionIF => template CmdLineOptionStorage<> => template <> CmdLineOption
    //
    //   CmdLineOptionStorage<> stores values of type Y
    //
    //   CmdLineOption<>        processes values of type X and
    //                          store them in base-class CmdLineOptionStorage<Y>
    //
    template <typename StoredType>
    struct CmdLineOptionStorage: CmdLineOptionIF, value_holder<typename std::decay<StoredType>::type> {
        using type          = typename std::decay<StoredType>::type;
        using holder_type   = value_holder<type>;
        using default_f     = std::function<void(void)>;

        CmdLineOptionStorage():
            __m_default_f( nullptr )
        {}

        bool get(type& t) const {
            if( __m_count==0 ) {
                if( __m_default_f )
                    __m_default_f();
            }
            if( this->__m_count )
                t = this->holder_type::__m_value;
            return this->__m_count>0;
        }
        type const& get( void ) const {
            if( __m_count==0 ) {
                if( __m_default_f )
                    __m_default_f();
            }
            // not specified and no default? cannot get the thing!
            if( this->__m_count==0 )
                throw std::runtime_error("get(): Option was not set on the command line and no default was specified.");
            return this->holder_type::__m_value;
        }

        default_f   __m_default_f;
    };

    template <typename ElementType, typename StoredType>
    struct CmdLineOption: CmdLineOptionStorage<typename std::decay<StoredType>::type> {

        using type          = typename std::decay<ElementType>::type;
        using holder_type   = CmdLineOptionStorage<typename std::decay<StoredType>::type>;
        using held_type     = typename holder_type::type;
        using constraint_f  = Constraint<type>;
        using process_arg_f = std::function<void(held_type&, std::string const&)>;

        CmdLineOption():
            __m_constraint_f( nullptr ),
            __m_process_arg_f( nullptr )
        {}

        void processArgument(std::string const& v) {
            this->CmdLineOptionIF::__m_precondition_f( this->CmdLineOptionIF::__m_count );
            // Request the action to do it's thing
            __m_process_arg_f(this->holder_type::__m_value, v);
            // Chalk up another entry of this command line option
            this->CmdLineOptionIF::__m_count++;
        }

        constraint_f   __m_constraint_f;
        process_arg_f  __m_process_arg_f;
    };

    // A functor to extract the name from any type
    struct name_getter_t {
        template <typename U>
        std::string operator()(U const& u) const {
            return name_getter_t::to_str(u.__m_value);
        }

        static std::string to_str(std::string const& s) {
            return s;
        }
        static std::string to_str(char const& c) {
            return std::string(&c, &c+1);
        }
    };

    ///////////////////////////////////////////
    // Depending on wether action ignores its
    // argument or not return the correct
    // processing function
    //////////////////////////////////////////

    template <typename U, typename V>
    struct ignore_both:
        std::integral_constant<bool, std::is_same<typename std::decay<U>::type, typename std::decay<V>::type>::value &&
                                     std::is_same<typename std::decay<U>::type, ignore_t>::value>
    {};
                                       
    template <typename Value>
    struct ignore_both_t {
        using type = std::function<void(Value&, std::string const&)>;

        template <typename Object, typename Action, typename Convert, typename Constrain, typename PreConstrain>
        static type mk(const Object* o, Action const& action, Convert const&, Constrain const&, PreConstrain const&) {
            return [=](Value&, std::string const&) {
                        // these actions ignore everything
                        action(o, std::cref(std::ignore));
                    };
        }
        static constexpr bool usesArgument = false;
    };

    template <typename Value>
    struct ignore_argument_t {
        using type = std::function<void(Value&, std::string const&)>;

        template <typename Object, typename Action, typename Convert, typename Constrain, typename PreConstrain>
        static type mk(const Object*, Action const& action, Convert const&, Constrain const& constrain, PreConstrain const&) {
            return [=](Value& v, std::string const&) {
                        // these actions ingnore the value passed in but rather,
                        // they'll do something with the value stored in them
                        // so we better apply our constraints to that value
                        // before using it
                        constrain(action.__m_value);
                        // passed constraint, now apply
                        action(v, std::cref(std::ignore));
                    };
        }

        static constexpr bool usesArgument = false;
    };

    template <typename Value, typename Element>
    struct use_argument_t {
        using type = std::function<void(Value&, std::string const&)>;

        template <typename Object, typename Action, typename Convert, typename Constrain, typename PreConstrain>
        static type mk(const Object*, Action const& action, Convert const& convert, Constrain const& constrain, PreConstrain const& preconstrain) {
            return [=](Value& v, std::string const& s) {
                        preconstrain(s); 
                        Element tmp;
                        convert(tmp, s);
                        constrain(tmp);
                        action(v, tmp);
                    };
        }
        static constexpr bool usesArgument = true;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    //  Another gruesome thing - this is DAS TEMPLATE
    //  this is what it's all about. Collect the user specified properties
    //  and turn them into a usable command line option object.
    //
    //  Really. This is the function what does it all - enforcing, setting
    //  defaults, constraining, building docstrings etc.
    //
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    template <typename... Props>
    CmdLineOptionPtr mk_argument(CmdLineBase* cmdline, Props&&... props) {
        // get the action!
        auto allAction = get_all<action_t>( std::forward_as_tuple(props...) );
        static_assert( std::tuple_size<decltype(allAction)>::value==1, "You must specify exactly one Action" );

        // Once we know what the action is, we know:
        //    1. the type of the element(argument) it expects
        //    2. the type where to store the element(s)
        auto theAction                  = std::get<0>(allAction);
        using actionType                = decltype(theAction);
        using actionValue               = typename std::decay<typename actionType::type>::type;
        using actionElement             = typename std::decay<typename actionType::element_type>::type;

        // From the amount of names (short and/or long ones) we can infer wether
        // this is a command line option or an argument
        auto allNames                   = get_all<name_t>( std::forward_as_tuple(props...) );
        constexpr bool isArgument       = (std::tuple_size< decltype(allNames) >::value == 0);

        // Remember if the option takes an argument or not (element type == ignore_t)
        // De Morgan: !a && !b == !(a || b)
        constexpr bool requiresArgument = !(isArgument || std::is_same<actionElement, ignore_t>::value);

        // Depending on /that/ we can infer to which type to apply any
        // constraints; it's either applying a constraint o the action's stored
        // type (actionValue) or to the argument to the option (actionArgument)
        // Use the actionElement as type if an argument is required, otherwise
        // use the actionValue /UNLESS/ that is ignore_t in which case we fall
        // back to using the actionElement again
        using actionArgument =
            typename std::conditional<isArgument || requiresArgument,
                                      actionElement,
                                      typename std::conditional<std::is_same<actionValue, ignore_t>::value,
                                                                actionElement,
                                                                actionValue>::type
                                     >::type;

        // Once we know what the action wants to store we can already
        // construct the appropriate command line option:
        auto optionPtr         = std::make_shared< CmdLineOption<actionArgument, actionValue> >();
        auto optionStore       = reinterpret_cast< CmdLineOptionStorage<actionValue>* >(optionPtr.get());
        auto optionIF          = reinterpret_cast< CmdLineOptionIF* >(optionPtr.get());

        // Collect all names and verify they don't collide
        // if no names were given this should be a command line argument
        // i.e. the option /is/ the value
        functools::copy(functools::map(allNames, name_getter_t()),
                        std::inserter(optionIF->__m_names, optionIF->__m_names.end()));

        /////////////////// Pre-Constraint handling //////////////////////////
        //
        // This one operates at the string-representation level on the
        // command line; i.e. before conversion is attempted
        // Here we count the preconstraints and make the function. Later on
        // - when we've decided what the 'actionmaker' is - we can test
        // wether the action actually uses the string on the command line
        // and thus wether it actually makes sense to try to execute such a
        // constraint
        auto allPreconstraints          = get_all<formatcondition>( std::forward_as_tuple(props...) );
        constexpr bool hasPreconstraint = (std::tuple_size< decltype(allPreconstraints) >::value > 0);

        auto DoPreConstrain =
            constraint_maker<formatcondition, std::string, ExecuteConstraints>::mk( optionIF->__m_constraints,
                                                                                    allPreconstraints /*std::forward_as_tuple(props...)*/ );
        auto preconstrain_f = Constraint<std::string>(
                [=](std::string const& value) {
                    DoPreConstrain(value);
                    return true;
                });

        /////////////////// Constraint handling /////////////////////////////
        //
        // Allow for any number of constraints to apply to an element's value
        // We must do this such that we can verify if e.g. a specified 
        // default violates these constraints
        auto DoConstrain =
            constraint_maker<constraint, actionArgument, ExecuteConstraints>::mk( optionIF->__m_constraints,
                                                                                    std::forward_as_tuple(props...) );
        optionPtr->__m_constraint_f = Constraint<actionArgument>(
                [=](actionArgument const& value) {
                    DoConstrain(value);
                    return true;
                });

        /////////////////// Argument count constraints //////////////////////
        //
        // Allow for setting pre- and/or post conditions on the amount of
        // times the option is allowed
        using argcount_t = decltype(optionIF->__m_count);
        auto DoPreCond  = constraint_maker<precondition,  argcount_t, ExecuteConstraints>::mk( optionIF->__m_requirements,
                                                                                                 std::forward_as_tuple(props...) );
        auto DoPostCond = constraint_maker<postcondition, argcount_t, ExecuteConstraints>::mk( optionIF->__m_requirements,
                                                                                                 std::forward_as_tuple(props...) );

        optionIF->__m_precondition_f  = [=](argcount_t v) { DoPreCond(v);  };
        optionIF->__m_postcondition_f = [=](argcount_t v) { DoPostCond(v); };

        // a commandline option/argument is required if there is (at least one)
        // postcondition that fails with a count of 0
        try {
            optionIF->__m_postcondition_f(0);
            optionIF->__m_required = false;
        }
        catch( std::exception const& ) {
            optionIF->__m_required = true;
        }
        // Wether to include ellipsis; out of [0 or 1, 0 or more, 1 or more] 
        // the latter two need to have "..." to indicate "or more"
        // Test wether there is (a) precondition that fails on '1'
        // because if it does then the option is not allowed to be present more
        // than once
        std::string ellipsis;
        try {
            optionIF->__m_precondition_f(1);
            ellipsis = "...";
        }
        catch( std::exception const& ) { }

        /////////////////// Default handling /////////////////////////////
        //
        // Generate a function to set the default value, if one was supplied.
        // Make sure that at most one was given ...
        // Note that the default applies to the action's stored type not the 
        // action's argument type
        auto allDefaults     = get_all<default_t>( std::forward_as_tuple(props...) );
        static_assert( std::tuple_size<decltype(allDefaults)>::value<=1, "You may specify one default at most" );

        // Now filter the ones whose actual default type is not ignore_t, which
        // just indicates that the user wasn't supposed to set a default ...
        auto actualDefaults  = functools::filter_t<is_actual_default::template test>( allDefaults );
        constexpr std::size_t nDefault = std::tuple_size< decltype(actualDefaults) >::value;

        // Allow only defaults to be set where the stored type == argument type?
        static_assert( nDefault==0 || std::is_same<actionArgument, actionValue>::value,
                       "You can only set defaults for options that store values, not collect them" );

        // Verify that any remaining defaults have the correct type
        auto okDefaults      = functools::filter_t<is_ok_default<actionArgument>::template test>( actualDefaults );

        static_assert( std::tuple_size<decltype(okDefaults)>::value==std::tuple_size<decltype(actualDefaults)>::value,
                       "The type of the default is incompatible with the type of the option" );

        // Verify that any defaults that are set do not violate any constraints
        functools::map(okDefaults, default_constrainer_t(), optionPtr->__m_constraint_f);

        // Now we can blindly 'map' the default setter over ALL defaults (well,
        // there's at most one ...). This function can be void(void) because
        // it requires no further inputs than everything we already
        // have available here ... and they don't violate any of the constraints
        optionStore->__m_default_f =
            [=](void) -> void {
                functools::map(okDefaults, default_setter_t(), optionPtr);
            };

        // Capture the defaults
        auto defbuilder = std::inserter(optionIF->__m_defaults, optionIF->__m_defaults.end());
        functools::copy(functools::map(okDefaults, docstr_getter_t()), defbuilder);

        /////////////////// Conversion handling /////////////////////////////
        //
        // Allow users to specify their own converter for string -> element
        // We append the built-in default conversion and take the first
        // element from the tuple, which is guaranteed to exists
        auto allConverters   = std::tuple_cat(get_all<conversion_t>( std::forward_as_tuple(props...) ),
                                              std::make_tuple(std_conversion_t()));

        // The user may specify up to one converter so the total number
        // of converters that we find must be >=1 && <=2
        constexpr std::size_t nConvert = std::tuple_size< decltype(allConverters) >::value;
        static_assert( nConvert>=1 && nConvert<=2,
                       "You may specify at most one user-defined converter");

        // Verify that the converter has an operator of the correct type?
        static_assert( !(isArgument || requiresArgument) ||
                       has_exact_operator<typename std::tuple_element<0, decltype(allConverters)>::type const,
                                          void, actionArgument&, std::string const&>::value,
                       "The converter can not convert to the requested value of the action" );

        // We need to build a function that takes a string and executes the
        // action with the converted string
        optionPtr->__m_requires_argument = requiresArgument;

        // Deal with documentation - we may add to this later
        auto allDocstr     = get_all<docstring_t>( std::forward_as_tuple(props...) );
        auto docstrbuilder = std::inserter(optionIF->__m_docstring, optionIF->__m_docstring.end());

        functools::copy(functools::map(allDocstr, docstr_getter_t()), docstrbuilder);
        // we may add docstr()'d constraints, defaults and whatnots.

        // Now we can pre-format the option's usage
        //  [...]   if not required
        //   -<short name> --long-name [<type>]  
        //      (type only appended if argument required)
        //   + ellipsis to indicate "or more"
        //
        unsigned int       n( 0 );
        std::ostringstream usage;

        // Names are sorted by reverse size
        for(auto const& nm: reversed(optionIF->__m_names))
            usage << (n++ ? " " : "") << (nm.size()==1 ? "-" : "--") << nm;
        if( optionIF->__m_requires_argument || isArgument )
            usage << (n ? " " : "") << "<" << optiontype<actionElement>() << ">" << ellipsis;

        optionIF->__m_usage = usage.str();
        if( !optionIF->__m_required )
            optionIF->__m_usage = "[" + optionIF->__m_usage + "]";

        /////////////////// The actual action builder /////////////////////////////
        //
        // Depending on wether we actually need to look at the (converted)
        // value we choose the correct action maker
        using actionMaker = typename 
            std::conditional<ignore_both<actionValue, actionElement>::value,
                             ignore_both_t<actionValue>, 
                             typename std::conditional<isArgument || requiresArgument,
                                                       use_argument_t<actionValue, actionArgument>,
                                                       ignore_argument_t<actionValue>
                                                      >::type
                            >::type;

        // Time to check wether there are pre-constraints and if it makes
        // sense to enforce those
        static_assert( !hasPreconstraint || actionMaker::usesArgument,
                       "You have specified constraint(s) on the command line string but the action does not use it." );

        // Pass the zeroth element of the converters into the processing
        // function - it's either the user's one or our default one
        optionPtr->__m_process_arg_f = actionMaker::mk(cmdline, theAction, std::get<0>(allConverters),
                                                       optionPtr->__m_constraint_f, preconstrain_f);
        return optionPtr;
    }

} } // namespace argparse, namespace detail

#endif
