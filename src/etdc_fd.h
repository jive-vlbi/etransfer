// base- and derived classes for wrapping file descriptors
#ifndef ETDC_ETDC_FD_H
#define ETDC_ETDC_FD_H

#include <string>
#include <tuple>
#include <exception>
#include <sstream>

typedef std::tuple<std::string, unsigned short>               ipport_type;
typedef std::tuple<std::string, std::string, unsigned short>  sockname_type;

ipport_type   ipport(std::string const& host, unsigned short port = 0) {
    return ipport_type(host, port);
}
sockname_type sockname(std::string const& proto, std::string const& host, unsigned short port = 0) {
    return sockname_type(proto, host, port);
}

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


// A wrapped file descriptor abstract base class (interface)
class etdc_fd {

    public:
        // We pretend to be just an interface
        etdc_fd() {}
        virtual ~etdc_fd() {}

        // methods that had preferrably be overridden by concrete implementations before them's being called
        // Note that the base class versions, if they get called, throw a not-implemented-here exception
        // This allows derived classes to skip implementing functions as long as they're not being called ...

        virtual sockname_type getsockname( void ) NOTIMPLEMENTED;
        virtual sockname_type getpeername( void ) NOTIMPLEMENTED;

        virtual ssize_t       read(void* /*buf*/, size_t /*n*/) NOTIMPLEMENTED;
        virtual ssize_t       write(const void* /*buf*/, size_t /*n*/) NOTIMPLEMENTED;

        virtual int           seek(off_t /*off*/, int /*whence*/) NOTIMPLEMENTED;
        virtual off_t         tell( void ) NOTIMPLEMENTED;

        virtual off_t         size( void ) NOTIMPLEMENTED;
        virtual int           close( void ) NOTIMPLEMENTED;

        // server-style interface
        virtual int           bind( ipport_type const& ) NOTIMPLEMENTED;
        virtual int           listen( int ) NOTIMPLEMENTED;
        virtual etdc_fd*      accept( void ) NOTIMPLEMENTED;
};
#undef NOTIMPLEMENTED
#undef THISFUNCTION

//////////////////////////////////////////////////////////////////
//
//                  Concrete derived classes
//
//////////////////////////////////////////////////////////////////

class etdc_tcp:
    public etdc_fd
{
    public:
        etdc_tcp() {}
        virtual ~etdc_tcp() {}

};

#endif
