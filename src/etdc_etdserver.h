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
#include <string>
#include <memory>

namespace etdc {
    using filelist_type     = std::list<std::string>;
    using result_type       = std::tuple<etdc::uuid_type, off_t>;

    uuid_type const& get_uuid(result_type const& res);

    // This is really just an interface, defining the API for the e-transfer thingamabob
    class ETDServerInterface {
        public:
            ETDServerInterface() {}

            // The methods' names are usually quite suggestive as to what they do or intend to trigger
            virtual filelist_type     listPath(std::string const& /*path*/, bool /*allow tilde expansion*/) const = 0;
            virtual result_type       requestFileWrite(std::string const& /*file name*/, openmode_type/*open mode*/ )     = 0;
            virtual result_type       requestFileRead(std::string const& /*file name*/, openmode_type /* open mode*/)       = 0;
            virtual dataaddrlist_type dataChannelAddr( void ) = 0;

            // Canned sequence?
            virtual bool          sendFile(std::string const&, etdc::uuid_type const&, off_t /*alreadyhave*//*, remote*/) = 0;
            virtual bool          getFile(std::string const&, etdc::uuid_type const&, off_t /*start*//*, src*/) = 0;

            virtual bool          removeUUID(etdc::uuid_type const&) = 0;
            virtual std::string   status( void ) const = 0;

            virtual ~ETDServerInterface() {}
    };

    // We can use refcounted pointers to serverinterfaces if we want to
    using ETDServerPtr = std::shared_ptr<ETDServerInterface>;



    //////////////////////////////////////////////////////////////////////
    //
    //  The concrete  ETDServer
    //
    //////////////////////////////////////////////////////////////////////
    class ETDServer: public ETDServerInterface {
        public:
            ETDServer(etdc::etd_state& shared_state):
                __m_shared_state( shared_state )
            {}

            virtual filelist_type     listPath(std::string const& /*path*/, bool /*allow tilde expansion*/) const;

            virtual result_type       requestFileWrite(std::string const&, openmode_type);
            virtual result_type       requestFileRead(std::string const&,  openmode_type);
            virtual dataaddrlist_type dataChannelAddr( void )                    NOTIMPLEMENTED;

            // Canned sequence?
            virtual bool          sendFile(std::string const&, etdc::uuid_type const&, off_t /*alreadyhave*//*, remote*/) NOTIMPLEMENTED;
            virtual bool          getFile(std::string const&, etdc::uuid_type const&, off_t /*start*//*, src*/) NOTIMPLEMENTED;

            virtual bool          removeUUID(etdc::uuid_type const&)  NOTIMPLEMENTED;
            virtual std::string   status( void ) const NOTIMPLEMENTED;

            virtual ~ETDServer() {}

        private:
            // We operate on shared state
            std::reference_wrapper<etdc::etd_state> __m_shared_state;
    };


} // namespace etdc

#endif
