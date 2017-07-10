// The actual etransfer functionality
#ifndef ETDC_ETDSERVER_H
#define ETDC_ETDSERVER_H

#include <tagged.h>
#include <notimplemented.h>
#include <etdc_uuid.h>
#include <etdc_assert.h>
#include <etdc_etd_state.h>

// C++ headers
#include <list>
#include <regex>
#include <string>
#include <memory>
#include <type_traits>

namespace etdc {
    using filelist_type     = std::list<std::string>;
    using result_type       = std::tuple<etdc::uuid_type, off_t>;

    // On some systems off_t is an 'alias' for long long int, on others for
    // long int. So when converting between string and off_t we must choose
    // between std::stoll or std::stol.
    // Note that we probably also could've done "istringstream iss >> off_t"
    // But then we need to check the istream's state afterwards because if
    // the conversion fails it sets the fail/bad bit(s) on the istream.
    // The advantage of stol(l) is that they immediately throw on fishyness.

    // base template that isn't really there
    template <typename T>
    void string2off_t(std::string const&, T&); 

    // Two specializations 
    template <> void string2off_t<long int>     (std::string const&, long int&);
    template <> void string2off_t<long long int>(std::string const&, long long int&);

    // get either the uuid or the off_t out of a result type.
    // The template(s) follow the constness of their argument and return "&" or "const &" to the extracted type
    template <typename T, typename = typename std::enable_if<std::is_same<typename std::decay<T>::type, result_type>::value>::type>
    typename std::conditional<std::is_const<T>::value, const uuid_type&, uuid_type&>::type get_uuid(T t) {
        return std::get<0>(t);
    }
    template <typename T, typename = typename std::enable_if<std::is_same<typename std::decay<T>::type, result_type>::value>::type>
    typename std::conditional<std::is_const<T>::value, const off_t&, off_t&>::type get_filepos(T t) {
        return std::get<1>(t);
    }

    // This is really just an interface, defining the API for the e-transfer thingamabob
    class ETDServerInterface {
        public:
            ETDServerInterface() {}

            // The methods' names are usually quite suggestive as to what they do or intend to trigger
            virtual filelist_type     listPath(std::string const& /*path*/, bool /*allow tilde expansion*/) const = 0;
            // returns (uuid, alreadyhave)
            virtual result_type       requestFileWrite(std::string const& /*file name*/, openmode_type/*open mode*/ )     = 0;
            // returns (uuid, leftover) based on current file size minus what the remote end already has
            virtual result_type       requestFileRead(std::string const& /*file name*/, off_t /*alreadyhave*/)       = 0;
            virtual dataaddrlist_type dataChannelAddr( void ) const = 0;

            // In the sendFile canned sequence:
            //      srcUUID == own UUID [assume: requestFileRead() was issued to this instance]
            //      dstUUID == UUID of the requestFileWrite on the the destination
            //  Then we attempt to connect from here to 'remote' and push 
            virtual bool          sendFile(uuid_type const& /*srcUUID*/, uuid_type const& /*dstUUID*/,
                                           off_t /*todo*/, dataaddrlist_type const& /*remote*/) = 0;
            // In the getFile canned sequence, we are the remote end, thus:
            //      srcUUID == remote UUID [assume: requestFileRead() was issued to that instance]
            //      dstUUID == own UUID of the requestFileWrite
            //  Then we attempt to connect from here to 'remote' and ask them to push
            virtual bool          getFile (uuid_type const& /*srcUUID*/, uuid_type const& /*dstUUID*/,
                                           off_t /*todo*/, dataaddrlist_type const& /*remote*/) = 0;

            virtual bool          removeUUID(etdc::uuid_type const&) = 0;
            virtual std::string   status( void ) const = 0;

            virtual ~ETDServerInterface() {}
    };

    // We can use refcounted pointers to serverinterfaces if we want to
    using etd_server_ptr = std::shared_ptr<ETDServerInterface>;


    //////////////////////////////////////////////////////////////////////
    //
    //  The concrete ETDServer
    //
    //////////////////////////////////////////////////////////////////////
    class ETDServer: public ETDServerInterface {
        public:
            explicit ETDServer(etdc::etd_state& shared_state):
                __m_uuid( etdc::uuid_type::mk() ), __m_shared_state( shared_state )
            { ETDCDEBUG(2, "ETDServer starting, my uuid=" << __m_uuid << std::endl); }

            virtual filelist_type     listPath(std::string const& /*path*/, bool /*allow tilde expansion*/) const;

            virtual result_type       requestFileWrite(std::string const&, openmode_type);
            virtual result_type       requestFileRead(std::string const&,  off_t);
            virtual dataaddrlist_type dataChannelAddr( void ) const;

            // Canned sequence?
            virtual bool          sendFile(uuid_type const& /*srcUUID*/, uuid_type const& /*dstUUID*/,
                                           off_t /*todo*/, dataaddrlist_type const& /*remote*/);
            virtual bool          getFile (uuid_type const& /*srcUUID*/, uuid_type const& /*dstUUID*/,
                                           off_t /*todo*/, dataaddrlist_type const& /*remote*/);

            virtual bool          removeUUID(etdc::uuid_type const&);
            virtual std::string   status( void ) const NOTIMPLEMENTED;

            virtual ~ETDServer();

        private:
            // We operate on shared state
            const etdc::uuid_type                   __m_uuid;
            std::reference_wrapper<etdc::etd_state> __m_shared_state;
    };

    //////////////////////////////////////////////////////////////////////
    //
    // This is a class implementing the ETDServerInterface but
    // actually talks to a remote instance
    //
    //////////////////////////////////////////////////////////////////////
    class ETDProxy: public ETDServerInterface {
        public:
            explicit ETDProxy(etdc::etdc_fdptr conn):
                __m_connection( conn )
            { ETDCASSERT(__m_connection, "The proxy must have a valid connection"); }

            virtual filelist_type     listPath(std::string const& /*path*/, bool /*allow tilde expansion*/) const;

            virtual result_type       requestFileWrite(std::string const&, openmode_type);
            virtual result_type       requestFileRead(std::string const&,  off_t);
            virtual dataaddrlist_type dataChannelAddr( void ) const;

            // Canned sequence?
            virtual bool          sendFile(uuid_type const& /*srcUUID*/, uuid_type const& /*dstUUID*/,
                                           off_t /*todo*/, dataaddrlist_type const& /*remote*/);
            virtual bool          getFile (uuid_type const& /*srcUUID*/, uuid_type const& /*dstUUID*/,
                                           off_t /*todo*/, dataaddrlist_type const& /*remote*/) NOTIMPLEMENTED;

            virtual bool          removeUUID(etdc::uuid_type const&);
            virtual std::string   status( void ) const NOTIMPLEMENTED;

            virtual ~ETDProxy() {}

        private:
            // Because we are a proxy we only have a connection to the other end
            etdc::etdc_fdptr    __m_connection;
    };

    //////////////////////////////////////////////////////////////////////
    //
    // This class does NOT implementing the ETDServerInterface but
    // takes a connection, instantiates its own ETDServer
    // and then loops, reading commands from the connection and sends
    // back replies
    //
    //////////////////////////////////////////////////////////////////////
    class ETDServerWrapper {
        public:
            ETDServerWrapper(ETDServerWrapper&&)                       = delete;
            ETDServerWrapper(ETDServerWrapper const&)                  = delete;
            ETDServerWrapper const& operator=(ETDServerWrapper const&) = delete;

            template <typename... Args>
            explicit ETDServerWrapper(etdc::etdc_fdptr conn, Args&&... args):
                __m_etdserver( std::forward<Args>(args)... ), __m_connection(conn)
            {
                ETDCASSERT(__m_connection, "The server wrapper must have a valid connection");
                this->handle();
            }

        private:
            // We operate on shared state
            ETDServer           __m_etdserver;
            etdc::etdc_fdptr    __m_connection;

            // Sucks the connection empty for commands
            void handle( void );
    };

    //////////////////////////////////////////////////////////////////////
    //
    //  This class also does NOT implement the ETDServerInterface;
    //  this is the ETDDataServer - it only deals with data connections
    //
    //////////////////////////////////////////////////////////////////////
    class ETDDataServer {
        public:
            ETDDataServer(etdc::etdc_fdptr conn, etdc::etd_state& shared_state):
                __m_connection(conn), __m_shared_state(shared_state)
            { ETDCASSERT(__m_connection, "The data server must have a valid connection");
              this->handle(); }

        private:
            etdc::etdc_fdptr                        __m_connection;
            std::reference_wrapper<etdc::etd_state> __m_shared_state;

            void handle( void );

            static void pull_n(size_t n, etdc::etdc_fdptr& src, etdc::etdc_fdptr& dst,
                               size_t rdPos, const size_t endPos, const size_t bufSz, std::unique_ptr<char[]>& buf);
            static void push_n(size_t n, etdc::etdc_fdptr& src, etdc::etdc_fdptr& dst,
                               size_t rdPos, const size_t endPos, const size_t bufSz, std::unique_ptr<char[]>& buf);

    };
} // namespace etdc

template <typename... Args>
etdc::etd_server_ptr mk_etdserver(Args&&... args) {
    return std::make_shared<etdc::ETDServer>( std::forward<Args>(args)... );
}

template <typename... Args>
etdc::etd_server_ptr mk_etdproxy(Args&&... args) {
    return std::make_shared<etdc::ETDProxy>( mk_client(std::forward<Args>(args)...) );
}
#endif
