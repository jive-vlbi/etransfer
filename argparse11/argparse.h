// This is the toplevel include file. Including this should be enough.
#ifndef ARGPARSE11_ARGPARSE_H
#define ARGPARSE11_ARGPARSE_H

#include <argparse_cmdlineoption.h>
#include <map>
#include <set>
#include <list>
#include <string>
#include <algorithm>
#include <functional>

#include <cstdlib>
#include <cerrno>
#include <climits>
#include <libgen.h>


namespace argparse {

    namespace detail {
        // support stuff
        struct lt_cmdlineoption {
            // The command line option's __m_names have already been sorted for us
            // we sort the command line options by longest name, alphabetically
            bool operator()(CmdLineOptionPtr const& l, CmdLineOptionPtr const r) const {
                if( l->__m_names.empty() || r->__m_names.empty() )
                    fatal_error(std::cerr, "no names found whilst comparing cmdlineoptions for ",
                                           (l->__m_names.empty() ? "left " : ""), (r->__m_names.empty() ? "right " : ""),
                                           "side of the comparison");
                // We /only/ want lexicographical compare on the names
                auto const&  left  = *std::begin(l->__m_names);
                auto const&  right = *std::begin(r->__m_names);

                return std::lexicographical_compare(std::begin(left), std::end(left),
                                                    std::begin(right), std::end(right), case_insensitive_char_cmp());
            }
        };
    } // namespace detail


    /////////////////////////////////////////////////////////////////////////////////////
    //
    //      _Finally_ we get to the actual command line class ...
    //
    /////////////////////////////////////////////////////////////////////////////////////

    class ArgumentParser: public CmdLineBase {
        using version_f = std::function<std::ostream&(std::ostream&)>;

        public:
            // Collect version + docstr(explanation of program)
            template <typename... Props>
            ArgumentParser(Props&&... props):
                __m_parsed( false ), __m_program("<unknown>")
            {
                // Extract all docstrings - they go into the description
                auto allDocstr     = detail::get_all<detail::docstring_t>( std::forward_as_tuple(props...) );
                auto docstrbuilder = std::inserter(__m_description, __m_description.end());

                functools::copy(functools::map(allDocstr, detail::docstr_getter_t()), docstrbuilder);

                // Check if there's a version?
                // We append our no-version object so if the usert didnae
                // specify a version we automatically fall back to that
                auto allVersion    = std::tuple_cat(detail::get_all<detail::version_t>(std::forward_as_tuple(props...)),
                                                    std::make_tuple(detail::NullVersion()));

                static_assert( std::tuple_size<decltype(allVersion)>::value<=2, "You may specify at most one version" );

                // OK now we can generate a function to print the version
                __m_version_f = [=](std::ostream& os) -> std::ostream& { return os << std::get<0>(allVersion); };
            }

            template <typename T>
            bool get(std::string const& opt, T& t) const {
                if( !__m_parsed )
                    fatal_error(std::cerr, "Cannot request value if no command line options have been parsed yet.");

                auto option = __m_option_idx_by_name.find(opt);
                if( option==__m_option_idx_by_name.end() )
                    fatal_error(std::cerr, std::string("No option by the name of '")+opt+"' defined.");
                return option->second->get(t);
            }
            template <typename T>
            T const& get(std::string const& opt) const {
                if( !__m_parsed )
                    fatal_error(std::cerr, "Cannot request value if no command line options have been parsed yet.");

                auto option = __m_option_idx_by_name.find(opt);
                if( option==__m_option_idx_by_name.end() )
                    fatal_error(std::cerr, std::string("No option by the name of '")+opt+"' defined.");
                return option->second->get<T>();
            }

            void parse(int, char const*const*const argv) {
                if( __m_parsed )
                    fatal_error(std::cerr, "Cannot double parse a command line");
                __m_parsed = true;

                // Step 0. Program name
                if( argv ) {
                    char    buf[PATH_MAX+1], *bn;
                    if( ::realpath(*argv, buf)==nullptr )
                        fatal_error(std::cerr, "realpath() fails on '", buf, "' - ", ::strerror(errno));
                    bn = ::basename(buf);
                    if( bn==nullptr )
                        fatal_error(std::cerr, "basename() fails on '", buf, "' - ", ::strerror(errno));
                    __m_program = ::basename(buf);
                }

                // Step 1. Transform into list of strings 
                char const*const*       option( argv ? argv+1 : nullptr);
                std::list<std::string>  options;
                auto                    argptr = std::back_inserter(options);
           
                // NOTE:
                //  could support "<prog> ... options ... -- <verbatim>"
                //  such that everything after a literal '--' gets passed
                //  through verbatim and is made available as "arguments()"
                while( option && *option ) {
                    // starts with "--"?
                    if( ::strncmp(*option, "--", 2)==0 ) {
                        // Is there anything following "--" at all?
                        if( ::strlen(*option)==2 )
                            fatal_error(std::cerr, "Missing option name after --");
                        // Now we can test if it is either:
                        //    --<long>
                        //    --<long>=<value>
                        char const*const equal = ::strchr((*option)+2, '=');
                        if( equal ) {
                            // only accept long names (length > 1)
                            // and non-empty values
                            const std::string opt(*option, equal);
                            const std::string val(equal+1);

                            if( opt.size()<4 )
                                fatal_error(std::cerr, "Only long-opt names are supported with --XY..., parsing `", *option, "'");
                            if( val.empty() )
                                fatal_error(std::cerr, "Empty value after `=' not allowed");
                            *argptr++ = opt;
                            *argptr++ = val;
                        } else {
                            // Must have "--xy[....]" - at least 2 characters in
                            // option name
                            if( ::strlen(*option)<4 )
                                fatal_error(std::cerr, "Only long-opt names are supported with --XY..., parsing `", *option, "'");
                            *argptr++ = std::string(*option);
                        }
                    // starts with "-"?
                    } else if( **option=='-' && ::strlen(*option)>1 ) {
                        // Assume it's a short-name (single character) option.
                        // (Or a collection thereof). Expand a single "-XYZ"
                        // into "-X -Y -Z"
                        char const*  flags = (*option)+1;

                        // Ah. If *flags is not an acceptable short name,
                        // then we pass the whole thing on as argument -
                        // e.g. to support a negative number "-1" ...
                        if( ::isalpha(*flags) ) {
                            // If the first appears to be a flag, then all
                            // of them should be
                            while( *flags )
                                *argptr++ = std::string("-")+*flags++;
                        } else {
                            // OK, something else - don't interpret the
                            // thing as flags so pass on unmodified
                            *argptr++ = std::string(*option);
                        }
                    } else {
                        // Just add verbatim
                        *argptr++ = std::string(*option);
                    }
                    option++;
                }

                // Now go through all the expanded thingamabobs
                detail::CmdLineOptionPtr   previous = nullptr;
                for(auto const& opt: options) {
                    detail::CmdLineOptionPtr   current = nullptr;
                    try {
                        // If previous is non-null it means it was waiting for
                        // an argument, now we have it
                        if( previous ) {
                            previous->processArgument( opt );
                            previous = nullptr;
                            continue;
                        }
                        // If the thing starts with '-' look for the option "-(-)<stuff",
                        // otherwise for the thing with the empty name
                        auto curOpt = ((opt[0]=='-' && ((opt.size()>=2 && ::isalpha(opt[1])) || (opt.size()>=3 && opt[1]=='-'))) ?
                                            // OK, looks like we need to look for cmdline option
                                            __m_option_idx_by_name.find(opt.substr(opt.find_first_not_of('-'))) :
                                            // .. otherwise look for the entry that takes arguments
                                            __m_option_idx_by_name.find(std::string()));

                        if( curOpt==__m_option_idx_by_name.end() ) {
                            this->print_help(true);
                            fatal_error(std::cerr, "\nUnrecognized command line option ", opt);
                        }
                        // Check wether the current option requires an argument
                        if( curOpt->second->__m_requires_argument ) {
                            previous = curOpt->second;
                        } else {
                            current = curOpt->second;
                            current->processArgument( opt );
                        }
                    }
                    catch( std::exception& e ) {
                        if( previous )
                            previous->mini_help(std::cerr, false);
                        else if( current )
                            current->mini_help(std::cerr, false);
                        fatal_error(std::cerr, e.what(), ENDL(std::cerr),
                                      "triggered whilst processing '", opt, "'");
                    }
                    catch( ... ) {
                        fatal_error(std::cerr, "Unknown exception caught", ENDL(std::cerr),
                                      "triggered whilst processing '", opt, "'");
                    }
                }
                // If we end up here with previous non-null there's a missing
                // argument!
                if( previous )
                    fatal_error(std::cerr, "Missing argument to option '", previous->__m_usage, "'");

                // And finally, test all post conditions!
                for(auto const& opt: __m_option_by_alphabet ) {
                    try {
                        opt->__m_postcondition_f( opt->__m_count );
                    }
                    catch( std::exception& e ) {
                        fatal_error(std::cerr, e.what(), ENDL(std::cerr),
                                      "whilst verifying post condition for '", opt->__m_usage, "'");
                    }
                    catch( ... ) {
                        fatal_error(std::cerr, "unknown exception whilst verifying post condition for '", opt->__m_usage, "'");
                    }
                }
            }

            virtual void print_help( bool usage ) const {
                std::ostream_iterator<std::string> printer(std::cout, " ");
                std::ostream_iterator<std::string> lineprinter(std::cout, "\n\t\t");
                std::ostream_iterator<std::string> indent1(std::cout, "\n\t");
                detail::ConstCmdLineOptionPtr      argument = nullptr;

                // First line is always:
                // <program> <USAGE>
                *printer++ = "Usage: ";
                *printer++ = __m_program;

                for(auto const& opt: __m_option_by_alphabet) {
                    if( opt->__m_names.begin()->empty() ) {
                        argument = opt;
                        continue;
                    }
                    *printer++ = opt->__m_usage;
                }
                if( argument )
                    *printer++ = argument->__m_usage;

                // If we're printing help ("long version") we start by
                // printing the description
                if( !usage ) {
                    std::ostream_iterator<std::string> dp(std::cout, "\n");
                    *dp++ = "";
                    detail::maybe_print("\n",  __m_description, dp);

                    if( argument ) {
                        std::cout << std::endl << "positional arguments:" << std::endl;
                        *lineprinter++ = argument->__m_usage;

                        if( !argument->__m_docstring.empty() ) {
                            *printer++ = "\r\t\b";
                            std::copy(std::begin(argument->__m_docstring), std::end(argument->__m_docstring), indent1);
                        }

                        if( !argument->__m_defaults.empty() ) {
                            *printer++ = "\r\tDefault: ";
                            *printer++ = *argument->__m_defaults.begin();
                            *printer++ = "\n";
                        }
                        detail::maybe_print("\r\tConstraints:",  argument->__m_constraints, lineprinter);
                        detail::maybe_print("\r\tRequirements:", argument->__m_requirements, lineprinter);
                    }

                    // And append the detailed help for all the options
                    for(auto const& opt: __m_option_by_alphabet) {
                        // skip the unnamed option (the command's arguments)
                        if( opt==argument )
                            continue;

                        // Print details!
                        std::cout << std::endl;
                        *lineprinter++ = opt->__m_usage;

                        if( !opt->__m_docstring.empty() ) {
                            *printer++ = "\r\t\b";
                            std::copy(std::begin(opt->__m_docstring), std::end(opt->__m_docstring), indent1);
                        }
                        if( !opt->__m_defaults.empty() ) {
                            *printer++ = "\r\tDefault: ";
                            *printer++ = *opt->__m_defaults.begin();
                            *printer++ = "\n";
                        }
                        detail::maybe_print("\r\tConstraints:",  opt->__m_constraints, lineprinter);
                        detail::maybe_print("\r\tRequirements:", opt->__m_requirements, lineprinter);
                        std::cout << "\r";
                    }
                }
                fatal_error<EXIT_SUCCESS>(std::cout, "");
            }
            virtual void print_version( void ) const {
                fatal_error<EXIT_SUCCESS>( __m_version_f(std::cout), "");
            }

            template <typename... Props>
            void add(Props&&... props) {
                if( __m_parsed )
                    fatal_error(std::cerr, "Cannot add command line arguments after having already parsed one.");

                auto new_arg = detail::mk_argument(this, std::forward<Props>(props)...);

                // Verify that none of the names of the new option conflict with
                // already defined names. If the option has no names at all it
                // is an argument. 
                // Note that we are NOT nice here. Any error here doesn't throw
                // an exception but terminates the program.
                if( new_arg->__m_names.empty() )
                    if( !new_arg->__m_names.insert(std::string()).second )
                        fatal_error(std::cerr, "Failed to insert empty string in names for command line argument description");

                for(auto const& nm: new_arg->__m_names )
                    if( __m_option_idx_by_name.find(nm)!=__m_option_idx_by_name.end() )
                        fatal_error(std::cerr, "Duplicate command line", (nm.empty() ? "argument" : ("option '"+nm+"'")));
                // Now that we know that none of the names will clash we can add
                // this option to the set of options, alphabetically sorted by
                // longest name ...
                if( !__m_option_by_alphabet.insert(new_arg).second )
                    fatal_error(std::cerr, "Failed to insert new element into alphabetic set", *new_arg->__m_names.begin());

                // OK register the option under all its names - we've
                // verified that it doesn't clash
                for(auto const& nm: new_arg->__m_names )
                    if( !__m_option_idx_by_name.emplace(nm, new_arg).second )
                        fatal_error(std::cerr, "Failed to insert new element into index by name", *new_arg->__m_names.begin());
            }

        private:
            // Keep the command line options in a number of data structures.
            // Depending on use case - listing the options or finding the
            // correct option - some data structures are better than others.
            // 1. In a simple set<options> sorted alphabetically on the longest
            //    name so printing usage/help is easy
            // 2. in an associative array mapping name -> option such that
            //    irrespective of under how many names an option was registered
            //    we can quickly find it
            using option_idx_by_name = std::map<std::string, detail::CmdLineOptionPtr>;
            using option_by_alphabet = std::set<detail::CmdLineOptionPtr, detail::lt_cmdlineoption>;

            bool                  __m_parsed;
            version_f             __m_version_f;
            std::string           __m_program;
            docstringlist_t       __m_description;
            option_idx_by_name    __m_option_idx_by_name;
            option_by_alphabet    __m_option_by_alphabet;
    };


} //namespace argparse


// Macro to allow user data types to be demangled to a human readable type
// The default for argparse is to give you the actual, demangled, full C++
// type name, which may be less informative/significantly harder to read ...
// The library already has some of the standard types covered (like transforming
// "std::_1::basic_string<char, CharTraits<...>....>" into "string").
//
// The (demangled) type name(s) are typically used for generating the
// documentation - telling the user the actual type expected.
//
// But we don't know about user data types. 
#define HUMANREADABLE(Type, Text) \
    namespace argparse { namespace detail { \
        template <> std::string demangle_f<Type>( void ) { return Text; } \
    }}

#endif
