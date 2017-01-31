// exception + macro to provide an implementation for memberfn that should be overridden and throws if it isn't
#include <string>
#include <sstream>
#include <exception>

// Use as:
//
//  class Base {
//    virtual void you_should_really_overload_this_one(void)            NOTIMPLEMENTED;
//    virtual void you_should_really_overload_this_one_too(int, double) NOTIMPLEMENTED;
//  };
//
//  class Derived : public Base {
//     void you_should_really_overload_this_one( void ) { /* Yes we overload this one! */ }; 
//     /* But we forget to overload the other one */
//  };
//
//  If user does this:
//
//  foo() {
//      Derived d;
//      d.you_should_really_overload_this_one_too(1, 3.14);
//  }
//
//  then a 'not_implemented_exception' is thrown and the message contains
//  useful information about which method (with which signature) is the one that should
//  have been overloaded

class not_implemented_exception:
    public std::exception
{
    public:
        not_implemented_exception(std::string const& what):
            mMessage( std::string("not implemented function: ") + what )
        {}

        virtual const char* what( void ) const noexcept {
            return mMessage.c_str();
        }

        virtual ~not_implemented_exception() {}
    private:
        const std::string   mMessage;
};

#ifdef __GNUC__
#define THISFUNCTION __PRETTY_FUNCTION__
#else
#define THISFUNCTION __func__ << "()"
#endif

#define NOTIMPLEMENTED \
        { std::ostringstream lclOZdreAm;\
          lclOZdreAm << THISFUNCTION << " in " << __FILE__ << ":" << __LINE__;\
          throw ::not_implemented_exception(lclOZdreAm.str());\
        }

