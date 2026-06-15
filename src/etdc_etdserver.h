// The actual etransfer functionality
// Copyright (C) 2007-2016 Harro Verkouter
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
// 
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE.  See the GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
// 
// Author:  Harro Verkouter - verkouter@jive.eu
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#ifndef ETDC_ETDSERVER_H
#define ETDC_ETDSERVER_H

#include <tagged.h>
#include <notimplemented.h>
#include <etdc_uuid.h>
#include <etdc_assert.h>
#include <etdc_etd_state.h>

// C++ headers
#include <set>
#include <list>
#include <regex>
#include <string>
#include <memory>
#include <stdexcept>
#include <functional>
#include <type_traits>

namespace etdc {
    using filelist_type        = std::list<std::string>;
    using result_type          = std::tuple<etdc::uuid_type, off_t>;
    using protocolversion_type = unsigned long int;

    // Thrown when a transfer cannot possibly succeed as requested - e.g. the
    // source daemon shares no data-channel scheme with the destination (a
    // non-TLS source asked to dial a TLS-only destination). Such a failure is
    // deterministic, so the client treats it as non-retryable instead of
    // burning its retry budget (and inter-attempt delays) on it.
    struct xfer_not_possible: std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    // return the appropropritate sockname conversion function based on
    // actual protocol version (taking into account "unknownProtocolVersion")
    // Syntax tip from: https://stackoverflow.com/a/52111752
    using sockname2string_fn = auto (*)( sockname_type const& ) -> std::string;

    sockname2string_fn sockname2str( protocolversion_type v );

    // Progress callback signature. Invoked (optionally) by sendFile/getFile
    // implementations from inside the active byte-copy loop with:
    //   bytes_so_far     -- bytes successfully transferred so far
    //   total_bytes      -- total bytes expected for this transfer (the
    //                       original `todo` argument)
    //   elapsed_seconds  -- wall time since the byte-copy began
    //
    // The callback runs on whichever thread is driving the loop. For local
    // transfers (ETDServer) that is the caller's thread; for proxied
    // transfers (ETDProxy) it is the thread that called sendFile() on the
    // proxy, dispatched from inside the wire-reply parser. In both cases the
    // callback observes a consistent snapshot for a single transfer and must
    // not throw -- implementations are expected to swallow exceptions.
    using progress_fn = std::function<void(off_t /*bytes_so_far*/,
                                           off_t /*total_bytes*/,
                                           double /*elapsed_seconds*/)>;

    // The result of a transfer
    struct xfer_result {
        using duration_type = std::chrono::duration<double>;
        bool const             __m_Finished;
        off_t const            __m_BytesTransferred;
        std::string const      __m_Reason; // may contain error message
        duration_type const    __m_DeltaT;

        // no default objects
        xfer_result() = delete;

        // can only be initialized from an actual duration. we convert to
        // "double seconds"
        template <typename Rep, typename Period>
        xfer_result(bool success, off_t nb, std::string const& r, std::chrono::duration<Rep, Period> const& dt):
            __m_Finished(success), __m_BytesTransferred(nb), __m_Reason(r), __m_DeltaT(std::chrono::duration_cast<duration_type>(dt))
        {}
    };

    // Introduced in protocol version 5: features.
    // The code may be compiled with optional features, which are negotiated
    // between past and future daemons (older daemons don't ask so we don't
    // tell) and between peer and future daenons we negotiate the common
    // features.
    // Note: conditional compilation doesn't happen *here* this is the set
    // of features we know in the current version!
    // Requirement: keep enums (and their string values) unique!
    enum feature_t {
        // Map future features we don't recognize into this to eliminate
        // them from the common set and prevent older feature-aware daemons
        // choking on stuff they don't recognize
        unsupportedFeature,
        // TLS support is an optional feature
        tlsSupport
    };
    // Kept in ETDServerInterface
    using featureset_type     = std::set<feature_t>;

    // Keep a mapping from enum => string rep;
    // is used for all I/O functions (input looks for value and return key,
    // output finds key and outputs value).
    // Requirement: keep BOTH enums and values unique!
    // initialized in the accompanying .cc file
    using feature2string_type = std::map<feature_t, std::string>;
    //extern feature2string_type const feature2string;
    feature2string_type const feature2string{
        // Requirement: keep BOTH enums and values unique!
        {unsupportedFeature, "unsupportedFeature"},
        {tlsSupport,         "TLS"}
    };


    // Write normalized feature string to a stream, and also
    // the inverse: parse a feature string into something we recognize
    // (or map to unsupportedFeature, obviously)
    std::ostream& operator<<(std::ostream& os, feature_t const& feature);
    std::istream& operator>>(std::istream& is, feature_t& feature);
    std::ostream& operator<<(std::ostream& os, featureset_type const& featureset);

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
            virtual xfer_result   sendFile(uuid_type const& /*srcUUID*/, uuid_type const& /*dstUUID*/,
                                           off_t /*todo*/, dataaddrlist_type const& /*remote*/,
                                           progress_fn const& /*progress*/ = {}) = 0;
            // In the getFile canned sequence, we are the remote end, thus:
            //      srcUUID == remote UUID [assume: requestFileRead() was issued to that instance]
            //      dstUUID == own UUID of the requestFileWrite
            //  Then we attempt to connect from here to 'remote' and ask them to push
            virtual xfer_result   getFile (uuid_type const& /*srcUUID*/, uuid_type const& /*dstUUID*/,
                                           off_t /*todo*/, dataaddrlist_type const& /*remote*/,
                                           progress_fn const& /*progress*/ = {}) = 0;

            virtual bool          removeUUID(etdc::uuid_type const&) = 0;
            virtual std::string   status( void ) const = 0;

            // Cancel any transfer
            virtual void          cancel( etdc::uuid_type const& ) = 0;

            // Which protocol version is this one speaking?
            virtual protocolversion_type  protocolVersion( void ) const = 0;
            virtual protocolversion_type  set_protocolVersion( protocolversion_type ) = 0;

            // get/update the featureset:
            // default = compiled in featureset of this version
            // when setting featureset will be the intersection of the
            // given featureset and the ones this code understands
            virtual featureset_type      featureSet( void ) const = 0;

            // The version of the protocol this code understands.
            //   v0  legacy unversioned
            //   v1  versioned, data-channel-addr-ext, etc
            //   v3  + SRT data-channel scheme
            //   v4  + in-band PROGRESS lines during sendFile/getFile
            //   v5  + advertise feature(s) in protocol-version reply
            //         e.g. TLS data-channel scheme (tls/tls6) ONLY AVAIL WHEN COMPILED WITH TLS=1
            static const protocolversion_type currentProtocolVersion = 5;
            static const protocolversion_type unknownProtocolVersion = ~((protocolversion_type)0);

            // Here we initialize the featureset that this specific code is compiled with
            // has to be done out-of-line in the accompanying .cc file
            static const featureset_type      currentFeatureSet;

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
            virtual xfer_result   sendFile(uuid_type const& /*srcUUID*/, uuid_type const& /*dstUUID*/,
                                           off_t /*todo*/, dataaddrlist_type const& /*remote*/,
                                           progress_fn const& /*progress*/ = {});
            virtual xfer_result   getFile (uuid_type const& /*srcUUID*/, uuid_type const& /*dstUUID*/,
                                           off_t /*todo*/, dataaddrlist_type const& /*remote*/,
                                           progress_fn const& /*progress*/ = {});

            virtual bool          removeUUID(etdc::uuid_type const&);
            virtual std::string   status( void ) const NOTIMPLEMENTED;

            virtual void          cancel( etdc::uuid_type const&  );

            virtual protocolversion_type  protocolVersion( void ) const;
            virtual protocolversion_type  set_protocolVersion( protocolversion_type ) NOTIMPLEMENTED;

            virtual featureset_type       featureSet( void ) const;

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
                __m_connection( conn ), __m_protocolVersion( ETDServerInterface::unknownProtocolVersion ),
                __m_attemptExtendedProbe( true ), __m_featureSetInitialised( false )
            { ETDCASSERT(__m_connection, "The proxy must have a valid connection"); }

            virtual filelist_type     listPath(std::string const& /*path*/, bool /*allow tilde expansion*/) const;

            virtual result_type       requestFileWrite(std::string const&, openmode_type);
            virtual result_type       requestFileRead(std::string const&,  off_t);
            virtual dataaddrlist_type dataChannelAddr( void ) const;

            // Canned sequence?
            virtual xfer_result   sendFile(uuid_type const& /*srcUUID*/, uuid_type const& /*dstUUID*/,
                                           off_t /*todo*/, dataaddrlist_type const& /*remote*/,
                                           progress_fn const& /*progress*/ = {});
            virtual xfer_result   getFile (uuid_type const& /*srcUUID*/, uuid_type const& /*dstUUID*/,
                                          off_t /*todo*/, dataaddrlist_type const& /*remote*/,
                                          progress_fn const& /*progress*/ = {}) NOTIMPLEMENTED;

            virtual bool          removeUUID(etdc::uuid_type const&);
            virtual std::string   status( void ) const NOTIMPLEMENTED;

            virtual void          cancel( etdc::uuid_type const& );

            virtual protocolversion_type  protocolVersion( void ) const;
            // Returns previous protocol version
            virtual protocolversion_type  set_protocolVersion( protocolversion_type pvn );

            virtual featureset_type       featureSet( void ) const;

            void preferExtendedProbe(bool enable);

            template <typename... Options>
            int setsockopt(Options&&... options) {
                return etdc::setsockopt(__m_connection->__m_fd, std::forward<Options>(options)...);
            }

            virtual ~ETDProxy() {}

        private:
            // Because we are a proxy we only have a connection to the other end
            etdc::etdc_fdptr             __m_connection;
            mutable protocolversion_type __m_protocolVersion;
            mutable bool                 __m_attemptExtendedProbe;
            mutable featureset_type      __m_featureSet;
            mutable bool                 __m_featureSetInitialised;
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
                __m_etdserver( std::forward<Args>(args)... ), __m_connection(conn),
                __m_clientProtocolVersion( ETDServerInterface::unknownProtocolVersion )
            {
                ETDCASSERT(__m_connection, "The server wrapper must have a valid connection");
                this->handle();
            }

        private:
            // We operate on shared state
            ETDServer            __m_etdserver;
            featureset_type      __m_clientFeatures;
            etdc::etdc_fdptr     __m_connection;
            protocolversion_type __m_clientProtocolVersion;
            // Serialises writes to __m_connection across the wrapper's main
            // read-loop thread and any detached worker threads it may have
            // spawned (currently sendFile workers, which also emit in-band
            // PROGRESS lines during the transfer).
            std::mutex          __m_writeMutex;

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

            static void pull_n(size_t n, etdc::etdc_fdptr src, etdc::etdc_fdptr dst,
                               size_t rdPos, const size_t endPos, const size_t bufSz, std::unique_ptr<char[]>& buf,
                               std::function<void(void)>& update_f,
                               etdc::uuid_type const& uuid);
            static void push_n(size_t n, etdc::etdc_fdptr src, etdc::etdc_fdptr dst,
                               size_t rdPos, const size_t endPos, const size_t bufSz, std::unique_ptr<char[]>& buf,
                               std::function<void(void)>& update_f,
                               etdc::uuid_type const& uuid);

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
