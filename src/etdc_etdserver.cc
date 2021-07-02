// Implementation file
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
#include <utilities.h>
#include <etdc_etdserver.h>

// C++ headerts
//#include <regex>
#include <mutex>
#include <memory>
#include <thread>
#include <functional>

// Plain-old-C
#include <glob.h>
#include <string.h>

namespace etdc {

    sockname2string_fn sockname2str( etdc::protocolversion_type v) {
        if( v==0 || v== ETDServerInterface::unknownProtocolVersion )
            return sockname2str_v0;
        if( v==1 )
            return sockname2str_v1;
        throw std::runtime_error("sockname2str/request for unsupported protocolversion " + etdc::repr(v));
    }

    // Two specializations of reading off_t from std::string
    template <>
    void string2off_t<long int>(std::string const& s, long int& o) {
        o = std::stol(s);
    }
    template <>
    void string2off_t<long long int>(std::string const& s, long long int& o) {
        o = std::stoll(s);
    }

    // parse "<proto/host:port[/opt=val[,opt2=val2]*]>" into sockname_type
    sockname_type decode_data_addr(std::string const& s) {
        static const std::string    ipv6_lit{ "[:0-9a-zA-Z]+(/[0-9]{1,3})?(%[a-zA-Z0-9]+)?" };
        //                                                  3             4
        // From: https://stackoverflow.com/a/3824105 
        // Also - need to check that host name length <= 255
        static const std::string    valid_host( "(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\\-]{0,61}[a-zA-Z0-9])"
        //                                       56
                                                "(\\.([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\\-]{0,61}[a-zA-Z0-9]))*)");
        //                                       7   8                
        static const std::string    keyVal( "[^ \t\v,=>]+=[^ \t\v,>]+" );
        static const std::string    options( "(/("+keyVal+ "(," + keyVal + ")*))?" );
        //                                    1011          12
        static const std::regex     rxSockName("^<([^/]+)/(\\["+ipv6_lit+"\\]|" + valid_host + "):([0-9]+)" + options + ">$");
                                            //    1       2                                       9 
                                            //    proto   host                                    port

        std::smatch fields;
        ETDCASSERT(std::regex_match(s, fields, rxSockName), "The string '" << s << "' is not a valid data address designator");
        ETDCASSERT(fields[5].length()<=255, "Host names can not be longer than 255 characters (RFC1123)");
        ETDCDEBUG(4, "decode_data_addr: 1='" << fields[1].str() << "' 2='" << fields[2].str() << "' 9='" << fields[9].str() << "'" <<
                     " 11='" << fields[11].str() << "'" << std::endl);

        // Break up the options into key=value pairs and see if there's
        // anything we recognize
        sockname_type     sn{ mk_sockname(fields[1].str(), unbracket(fields[2].str()), port(fields[9].str())) };
        std::string const opts{ fields[11].str() };

        if( !opts.empty() ) {
            std::vector<std::string>    keyvalues;

            etdc::string_split(opts, ',', std::back_inserter(keyvalues));
            for( const auto kv: keyvalues ) {
                // We already know the option matches keyVal (see above)
                // so this can be done basically blindly
                std::string::size_type  equal = kv.find('=');
                std::string const       key   = kv.substr(0, equal);
                std::string const       val   = kv.substr(equal+1);

                // *now* we can see if we recognize anything
                if( key=="mss" )
                    etdc::update_sockname(sn, mss(val));
                else if( key=="max-bw" ) {
                    // check for value of "0" - which means that the data
                    // channel does not have an explicit limit set
                    if( val!="0" )
                        etdc::update_sockname(sn, max_bw(val));
                } else
                    ETDCDEBUG(0, "Server sent unsupported socket option '" << kv << "' - ignoring" << std::endl);
            }
        }
        return sn; 
    }


    /////////////////////////////////////////////////////////////////////////////////////////
    //
    //     This is the real ETDServer.
    //     An instance of this may be running in the daemon but also inside
    //     the client, if either end of the transfer is a local path :-)
    //
    /////////////////////////////////////////////////////////////////////////////////////////
   
    filelist_type ETDServer::listPath(std::string const& path, bool allow_tilde) const {
        ETDCASSERT(!path.empty(), "We do not allow listing an empty path");

        // The special majick file /dev/zero:[0-9]+([kMGT]i?B)? may be
        // specified which (1) is implemented as not a real file (so what ...)
        // and (2) does not get globbed for it is just one file
        const bool              isDevZero( std::regex_match(path, etdc::rxDevZero) );

        if( isDevZero ) 
            return filelist_type{ path };

        // glob() is MT unsafe so we had better make sure only one thread executes this
        //static std::mutex       globMutex;
        std::string                                gPath( path );

        // If the path ends with "/" we add "*" because the client wishes to
        // list the contents of the directory
        if( *path.rbegin()=='/' )
            gPath.append("*");

        // Grab lock + execute 
        // Allocate zero-initialized struct and couple with correct deleter for when it goes out of scope
        int                                        globFlags{ GLOB_MARK };
        std::unique_ptr<glob_t, void(*)(glob_t*)>  files{etdc::Zero<glob_t>::mk(),
                                                         [](glob_t* p) { ::globfree(p); delete p;} };

#ifdef GLOB_TILDE
        globFlags |= (allow_tilde ? GLOB_TILDE : 0);
#else
        // No tilde support, so if we find '~' in path and ~ expansion requested
        // then we can return a useful error message
        if( allow_tilde && gPath.find('~')!=std::string::npos )
            throw std::domain_error("The target O/S does not support the requested tilde expansion");
#endif
        // Make the glob go fast(er) [NOSORT] - we'll do that ourselves when we have all the paths in memory
        //::glob(path.c_str(), GLOB_MARK|/*GLOB_NOSORT|*/, nullptr, files.get());
        ::glob(gPath.c_str(), globFlags, nullptr, files.get());
        //
        //    std::lock_guard<std::mutex> scopedlock(globMutex);
        //}
        
        // Now that we have the results, we can 
        return filelist_type(&files->gl_pathv[0], &files->gl_pathv[files->gl_pathc]);
    }

    //////////////////////////////////////////////////////////////////////////////////////
    //
    // Attempt to set up resources for writing to a file
    // return our UUID that the client must use to write to the file
    //
    //////////////////////////////////////////////////////////////////////////////////////
    result_type ETDServer::requestFileWrite(std::string const& path, openmode_type mode) {
        static const std::set<openmode_type> allowedModes{openmode_type::New, openmode_type::OverWrite, openmode_type::Resume, openmode_type::SkipExisting};

        // We must check-and-insert-if-ok into shared state.
        // This has to be atomic, so we'll grab the lock
        // until we're completely done.
        auto&                       shared_state( __m_shared_state.get() );
        std::lock_guard<std::mutex> lk( shared_state.lock );
        auto&                       transfers( shared_state.transfers );

        // Before we allow doing anything at all we must make sure
        // that we're not already busy doing something else
        ETDCASSERT(transfers.find(__m_uuid)==transfers.end(), "requestFileWrite: this server is already busy");

        const std::string nPath( detail::normalize_path(path) );

        // Attempt to open path new, write or append [reject read!]
        ETDCASSERT(allowedModes.find(mode)!=std::end(allowedModes),
                   "invalid open mode for requestFileWrite(" << path << ")");

        // Before doing anything - see if this server already has an entry for this (normalized) path -
        // we cannot honour multiple write attempts (not even if it was already open for reading!)
        // 9/Nov/2017 - That is, writing to /dev/null can be done any number of times
        //              !a && !b => !(a || b)
        //                  a: writing to dev/null?
        //                  b: already reading/writing from/to requested file?
        const auto  pathPresent = !((nPath=="/dev/null") ||
                                    (std::find_if(std::begin(transfers), std::end(transfers),
                                                [&](transfermap_type::value_type const& vt) { return vt.second->path==nPath; })
                                    == std::end(transfers)));
        ETDCASSERT(pathPresent==false, "requestFileWrite(" << path << ") - the path is already in use");

        // Transform to int argument to open(2) + append some flag(s) if necessary/available
        int  omode = static_cast<int>(mode);

        // Insider trick ... SkipExisting is bitwise complement of the real open flags
        if( mode==openmode_type::SkipExisting )
            omode = ~omode;

#if O_LARGEFILE
        // set large file if the current system has it
        omode |= O_LARGEFILE;
#endif

        // Note: etdc_file(...) c'tor will create the whole directory tree if necessary.
        //       Because it may/may not have to create, we add the file permission bits
        etdc_fdptr      fd( nPath=="/dev/null" ? mk_fd<devzeronull>(nPath, omode) :
                            (mode==openmode_type::New ? mk_fd<etdc_file<detail::ThrowOnExistThatShouldNotExist>>(nPath, omode, 0644) :
                                                        mk_fd<etdc_file<>>(nPath, omode, 0644)) );
        const off_t     fsize{ fd->lseek(fd->__m_fd, 0, SEEK_END) };
        //const uuid_type uuid{ uuid_type::mk() };

        ETDCASSERT(transfers.emplace(__m_uuid, std::unique_ptr<transferprops_type>(new etdc::transferprops_type(fd, nPath, mode))).second,
                   "Failed to insert new entry, request file write '" << path << "'");
        // and return the uuid + alreadyhave
        return result_type(__m_uuid, fsize);
    }

    result_type ETDServer::requestFileRead(std::string const& path, off_t alreadyhave) {
        // We must check-and-insert-if-ok into shared state.
        // This has to be atomic, so we'll grab the lock
        // until we're completely done.
        auto&                       shared_state( __m_shared_state.get() );
        std::lock_guard<std::mutex> lk( shared_state.lock );
        auto&                       transfers( shared_state.transfers );

        // Check if we're not already busy
        ETDCASSERT(transfers.find(__m_uuid)==transfers.end(), "requestFileRead: this server is already busy");

        // Before doing anything - see if this server already has an entry for this (normalized) path -
        // we can only honour this request if it's opened for reading [multiple readers = ok]
        const std::string nPath( detail::normalize_path(path) );
        const auto  pathPtr = std::find_if(std::begin(transfers), std::end(transfers),
                                           std::bind([&](std::string const& p) { return p==nPath; },
                                                     std::bind(std::mem_fn(&transferprops_type::path), std::bind(etdc::snd_type(), std::placeholders::_1))));
        ETDCASSERT(pathPtr==std::end(transfers) || pathPtr->second->openMode==openmode_type::Read,
                   "requestFileRead(" << path << ") - the path is already in use");

        // Transform to int argument to open(2) + append some flag(s) if necessary/available
        int  omode = static_cast<int>(etdc::openmode_type::Read);

#if O_LARGEFILE
        // set large file if the current system has it
        omode |= O_LARGEFILE;
#endif

        // Note: etdc_file(...) c'tor will create the whole directory tree if necessary.
        // Because openmode is read, then we don't have to pass the file permissions; either it's there or it isn't
        //etdc_fdptr      fd( new etdc_file(nPath, omode) );
        etdc_fdptr      fd( std::regex_match(nPath, etdc::rxDevZero) ? mk_fd<devzeronull>(nPath, omode) : mk_fd<etdc_file<>>(nPath, omode) );
        const off_t     sz{ fd->lseek(fd->__m_fd, 0, SEEK_END) };
        //const uuid_type uuid{ uuid_type::mk() };

        // Assert that we can seek to the requested position
        ETDCASSERT(fd->lseek(fd->__m_fd, alreadyhave, SEEK_SET)!=static_cast<off_t>(-1),
                   "Cannot seek to position " << alreadyhave << " in file " << path << " - " << etdc::strerror(errno));

        auto insres = transfers.emplace(__m_uuid, std::unique_ptr<transferprops_type>( new etdc::transferprops_type(fd, nPath, openmode_type::Read)));
        ETDCASSERT(insres.second, "Failed to insert new entry, request file read '" << path << "'");
        return result_type(__m_uuid, sz-alreadyhave);
    }

    dataaddrlist_type ETDServer::dataChannelAddr( void ) const {
        auto&                       shared_state( __m_shared_state.get() );
        std::lock_guard<std::mutex> lk( shared_state.lock );
        return shared_state.dataaddrs;
    }

    bool ETDServer::removeUUID(etdc::uuid_type const& uuid) {
        ETDCASSERT(uuid==__m_uuid, "Cannot remove someone else's UUID!");

        // We need to do some thinking about locking sequence because we need
        // a lock on the shared state *and* a lock on the transfer
        // before we can attempt to remove it.
        // To prevent deadlock we may have to relinquish the locks and start again.
        // What that means is that if we fail to lock both atomically, we must start over:
        //  lock shared state and (attempt to) find the transfer
        // because after we've released the shared state lock, someone else may have snuck in
        // and deleted or done something bad with the transfer i.e. we cannot do a ".find(uuid)" once 
        // and assume the iterator will remain valid after releasing the lock on shared_state
        etdc::etd_state&                    shared_state( __m_shared_state.get() );
        std::unique_ptr<transferprops_type> removed;

        while( true ) {
            // 1. lock shared state
            std::unique_lock<std::mutex>     lk( shared_state.lock );
            // 2. find if there is an entry in the map for us
            etdc::transfermap_type::iterator ptr = shared_state.transfers.find(__m_uuid);
            
            // No? OK then we're done
            if( ptr==shared_state.transfers.end() )
                return false;

            // If we're doing a transfer, make it fall out of the loop?
            // Note that the lock on the transfer itself is held during
            // the whole transfer
            ptr->second->fd->close( ptr->second->fd->__m_fd );
            if( ptr->second->data_fd )
                ptr->second->data_fd->close( ptr->second->data_fd->__m_fd );

            // Now we must do try_lock on the transfer - if that fails we sleep and start from the beginning
            std::unique_lock<std::mutex>     sh( ptr->second->xfer_lock, std::try_to_lock );
            if( !sh.owns_lock() ) {
                // we must release the lock on shared state before sleeping
                // for a bit or else no-one can change anything [because we
                // hold the lock to shared state ...]
                lk.unlock();
                // *now* we sleep for a bit and then try again
                std::this_thread::sleep_for( std::chrono::microseconds(42) );
                continue;
            }
            // Right, we now hold both locks!

            // We cannot erase the transfer immediately: we hold the lock that is contained in it
            // so what we do is transfer the lock out of the transfer and /then/ erase the entry.
            // And when we finally return, then the lock will be unlocked and the unique pointer
            // deleted
            //transfer_lock = std::move(transfer.lockPtr);
            // move the data out of the transfermap
            removed.swap( ptr->second );
            // OK lock is now moved out of the transfer, so now it's safe to erase the entry
            // OK the uniqueptr to the transfer is now moved out of the transfermap, so now it's safe to erase the entry
            shared_state.transfers.erase( ptr );
            break;
        }
        return true;
    }

    xfer_result ETDServer::sendFile(uuid_type const& srcUUID, uuid_type const& dstUUID, 
                             off_t todo, dataaddrlist_type const& dataAddrs) {
        // 1a. Verify that the srcUUID is our UUID
        ETDCASSERT(srcUUID==__m_uuid, "The srcUUID '" << srcUUID << "' is not our UUID");

        // We need to protect our transfer so we need to do deadlock avoidance
        // with re-searching our UUID until we have both locks
        bool                             have_both_locks{ false }, cancelled{ false };
        off_t const                      nTodo( todo );
        etdc::etd_state&                 shared_state( __m_shared_state.get() );

        // Make it loop until we got dem loks
        while( !have_both_locks && !(cancelled = shared_state.cancelled.load()) ) {
            // 2a. lock shared state
            std::unique_lock<std::mutex>     lk( shared_state.lock );
            // 2b. assert that there is an entry for us, indicating that we ARE configured
            etdc::transfermap_type::iterator ptr = shared_state.transfers.find(__m_uuid);

            ETDCASSERT(ptr!=shared_state.transfers.end(), "This server was not initialized yet");

            // We can read the state of the atomic bool
            if( (cancelled = ptr->second->cancelled.load()) )
                break;
            
            // Now we must do try_lock on the transfer - if that fails we sleep and start from the beginning
            std::unique_lock<std::mutex>     sh( ptr->second->xfer_lock, std::try_to_lock );
            if( !sh ) {
                // we must manually unlock the shared state before sleeping
                // or else no-one will be able to change anything
                lk.unlock();
                // *now* sleep for a bit and then try again ...
                std::this_thread::sleep_for( std::chrono::microseconds(19) );
                continue;
            }
            // Right, we now hold both locks!
            have_both_locks = true;

            // Copy relevant values from shared state to here whilst we
            // still have the lock
            const size_t            bufSz{ shared_state.bufSize };
            const etdc::mss_type    ourMSS{ shared_state.udtMSS };
            const etdc::max_bw_type ourBW{ shared_state.udtMaxBW };
            std::ostringstream      tried;
            // At this point we don't need the shared_state lock anymore - we've found our entry and we've locked it
            // So no-one can remove the entry from under us until we're done
            lk.unlock();

            // Get a reference to the actual transfer properties
            transferprops_type&         transfer( *ptr->second );
            etdc::detail::cancelfn_type isCancelled{ [&]( void ) { return shared_state.cancelled.load() || transfer.cancelled.load(); } };

            // Verify that indeed we are configured for file read
            ETDCASSERT(transfer.openMode==openmode_type::Read, "This server was initialized, but not for reading a file");

            // Great. Now we attempt to connect to the remote end

            for(auto addr: dataAddrs) {
                if( (cancelled = isCancelled()) ) 
                    break;
                try {
                    // This is 'sendFile' so our data channel will have to
                    // have a big send buffer
                    const auto    proto = get_protocol(addr);
                    auto          clnt  = etdc::detail::client_defaults.find( untag(proto) )->second();

                    // Merge our settings with the default client settings
                    etdc::detail::update_clnt( clnt, get_host(addr), get_port(addr),
                                                     etdc::udt_rcvbuf{bufSz}, etdc::udt_sndbuf{bufSz},
                                                     etdc::so_rcvbuf{bufSz}, etdc::so_sndbuf{bufSz},
                                                     isCancelled );
                    // decide on which mss to use
                    // If set to 0 (default) do not change
                    using key_type    = std::pair<bool, bool>;
                    using mssmap_type = std::map<key_type, std::function<etdc::udt_mss(int, int)>>;
                    static const mssmap_type mss_map{
                        // If both sides have an MSS setting, use the minimum
                        {key_type{true, true},   [](int o, int t) { return etdc::udt_mss{ std::min(o, t)}; }},
                        // either have one set? use that one
                        {key_type{true, false},  [](int o, int  ) { return etdc::udt_mss{ o }; }},
                        {key_type{false, true},  [](int  , int t) { return etdc::udt_mss{ t }; }},
                        // neither have set it, don't set it here either
                        {key_type{false, false}, [](int  , int  ) { return etdc::udt_mss{ 0 }; }}
                    };

                    auto       oMSS{ untag(ourMSS) };
                    auto       tMSS{ untag(get_mss(addr)) };
                    const auto mss_to_use = mss_map.find( key_type{oMSS>0, tMSS>0} )->second(oMSS, tMSS);

                    ETDCDEBUG(4, "ETDServer::sendFile/use MSS=" << untag(mss_to_use) << " [ours=" << oMSS << ", "
                                                                << get_host(addr) << "=" << tMSS << "]" << std::endl);
                    if( untag(mss_to_use) ) 
                        etdc::detail::update_clnt( clnt, mss_to_use );

                    // same applies to bandwidth constraints?
                    auto     oBW{ untag(ourBW) };
                    auto     tBW{ untag(get_max_bw(addr)) };

                    // If either side has a bw restriction we adapt to that
                    using maxbwmap_type = std::map<key_type, std::function<etdc::udt_max_bw(int64_t, int64_t)>>;
                    static const maxbwmap_type maxbw_map{
                        // both restricted - return minimum
                        {key_type{true, true},   [](int64_t o, int64_t t) { return etdc::udt_max_bw{ std::min(o, t)}; }},
                        {key_type{true, false},  [](int64_t o, int64_t  ) { return etdc::udt_max_bw{ o }; }},
                        {key_type{false, true},  [](int64_t  , int64_t t) { return etdc::udt_max_bw{ t }; }},
                        {key_type{false, false}, [](int64_t  , int64_t  ) { return etdc::udt_max_bw{ -1 }; }}
                    };

                    const auto maxbw = maxbw_map.find( key_type{oBW>0, tBW>0} )->second(oBW, tBW);

                    ETDCDEBUG(4, "ETDServer::sendFile/use MaxBW=" << untag(maxbw) << " [ours=" << oBW << ", "
                                                                << get_host(addr) << "=" << tBW << "]" << std::endl);
                        
                    etdc::detail::update_clnt( clnt, maxbw );

                    transfer.data_fd = mk_client( get_protocol(addr), clnt );
                    ETDCDEBUG(2, "sendFile/connected to " << addr << std::endl);
                    break;
                }
                catch( std::exception const& e ) {
                    tried << addr << ": " << e.what() << ", ";
                }
                catch( ... ) {
                    tried << addr << ": unknown exception" << ", ";
                }
            }
            if( (cancelled = isCancelled()) ) 
                break;

            ETDCASSERT(transfer.data_fd, "Failed to connect to any of the data servers: " << tried.str());

            // EskilSpecial!
            ETDCDEBUG( __m_daemon ? 1  : 1000, "sendFile[" << ptr->second->path << "] start sending to " << 
                                               transfer.data_fd->getpeername( transfer.data_fd->__m_fd ) << std::endl);

            // Weehee! we're connected!
            // Need buffer and record the data channel
            std::unique_ptr<unsigned char[]> buffer(new unsigned char[bufSz]);

            // Create message header
            std::ostringstream  msg_buf;
            msg_buf << "{ uuid:" << dstUUID << ", sz:" << todo << "}";

            bool                remoteOK{ true };
            std::string         reason;
            const std::string   msg( msg_buf.str() );
            auto const          start_tm = std::chrono::high_resolution_clock::now();
            transfer.data_fd->write(transfer.data_fd->__m_fd, msg.data(), msg.size());

            while( todo>0 && !(cancelled = isCancelled()) ) {
                size_t const  n = std::min((size_t)todo, bufSz);
                ssize_t       nWritten{0};
                const ssize_t nRead = transfer.fd->read(transfer.fd->__m_fd, &buffer[0], n);

                if( nRead<=0 ) {
                    reason = ((nRead==-1) ? std::string(etdc::strerror(errno)) : std::string("read() returned 0 - hung up"));
                    break;
                }

                // Keep on writing untill all bytes that were read are actually written
                while( nWritten<nRead && !shared_state.cancelled.load() ) {
                    ssize_t const thisWrite = transfer.data_fd->write(transfer.data_fd->__m_fd, &buffer[nWritten], nRead-nWritten);

                    if( thisWrite<=0 ) {
                        reason   = ((thisWrite==-1) ? std::string(etdc::strerror(errno)) : std::string("write should never have returned 0"));
                        remoteOK = false;
                        break;
                    }
                    nWritten += thisWrite;
                }
                if( nWritten<nRead )
                    break;
                todo -= (off_t)nWritten;
                if( (cancelled = isCancelled()) )
                    break;
            }
            // if we make it out of the loop, todo should be <= 0 and terminate the outer loop
            // wait here until the recipient has acknowledged receipt of all bytes
            // But that only makes sense if the destination is still alive!
            // (it should send an ACK as soon as it breaks from the loop and has flushed all bytes it has)
            if( remoteOK && !cancelled ) {
                char    ack;

                ETDCDEBUG(4, "sendFile: waiting for remote ACK ..." << std::endl);
                transfer.data_fd->read(transfer.data_fd->__m_fd, &ack, 1);
                ETDCDEBUG(4, "sendFile: ... got it" << std::endl);
            }
            auto const end_tm = std::chrono::high_resolution_clock::now();
            auto const res    = cancelled ? xfer_result(false, 0, "Cancelled", xfer_result::duration_type()) :
                                            xfer_result((todo==0), nTodo - todo, reason, (end_tm-start_tm));
            ETDCDEBUG( __m_daemon ? 1  : 1000, "sendFile[" << ptr->second->path << "]: " << std::boolalpha <<
                                               res.__m_Finished << " " <<
                                               res.__m_Reason << " " <<
                                               res.__m_BytesTransferred << " bytes in " <<
                                               res.__m_DeltaT.count() << " seconds" <<
                                               std::endl);
            return res;
        }
        return xfer_result(false, 0, (cancelled  ? "Cancelled" : "Failed to get both locks"), xfer_result::duration_type());
    }

    xfer_result ETDServer::getFile(uuid_type const& srcUUID, uuid_type const& dstUUID, 
                            off_t todo, dataaddrlist_type const& dataAddrs) {
        // 1a. Verify that the dstUUID is our UUID
        ETDCASSERT(dstUUID==__m_uuid, "The dstUUID '" << dstUUID << "' is not our UUID");

        // We need to protect our transfer so we need to do deadlock avoidance
        // with re-searching our UUID until we have both locks
        bool                             have_both_locks{ false }, cancelled{ false };
        off_t const                      nTodo( todo );
        etdc::etd_state&                 shared_state( __m_shared_state.get() );

        // Make it loop until we get both locks
        while( !have_both_locks && !(cancelled = shared_state.cancelled.load()) ) {
            // 2a. lock shared state
            std::unique_lock<std::mutex>     lk( shared_state.lock );
            // 2b. assert that there is an entry for us, indicating that we ARE configured
            etdc::transfermap_type::iterator ptr = shared_state.transfers.find(__m_uuid);

            ETDCASSERT(ptr!=shared_state.transfers.end(), "This server was not initialized yet");

            // We can read the state of the atomic bool
            if( (cancelled = ptr->second->cancelled.load()) )
                break;
            
            // Now we must do try_lock on the transfer - if that fails we sleep and start from the beginning
            std::unique_lock<std::mutex>     sh( ptr->second->xfer_lock, std::try_to_lock );
            if( !sh ) {
                // Manually unlock the shared state or else nobody won't be
                // able to change anything!
                lk.unlock();

                // *now* we sleep for a bit and then try again
                std::this_thread::sleep_for( std::chrono::microseconds(23) );
                continue;
            }
            // Right, we now hold both locks!
            have_both_locks = true;

            // At this point we don't need the shared_state lock anymore - we've found our entry and we've locked it
            // So no-one can remove the entry from under us until we're done
            lk.unlock();

            // Verify that indeed we are configured for file write
            // Note that we do NOT include 'skip existing' in here - the
            // point is that we don't want to write to such a file!
            transferprops_type&                        transfer( *ptr->second );
            etdc::detail::cancelfn_type                isCancelled{ [&]( void ) { return shared_state.cancelled.load() || transfer.cancelled.load(); } };
            static const std::set<etdc::openmode_type> allowedWriteModes{ openmode_type::OverWrite, openmode_type::New, openmode_type::Resume };

            ETDCASSERT(allowedWriteModes.find(transfer.openMode)!=allowedWriteModes.end(),
                       "This server was initialized, but not for writing to file");

            // Great. Now we attempt to connect to the remote end
            const size_t            bufSz( shared_state.bufSize );
            const etdc::mss_type    ourMSS{ shared_state.udtMSS };
            const etdc::max_bw_type ourBW{ shared_state.udtMaxBW };
            std::ostringstream      tried;

            for(auto addr: dataAddrs) {
                if( (cancelled = isCancelled()) )
                    break;
                try {
                    // This is 'getFile' so our data channel will have to
                    // have a big read buffer
                    const auto    proto = get_protocol(addr);
                    auto          clnt  = etdc::detail::client_defaults.find( untag(proto) )->second();

                    // Update client defaults with ours
                    etdc::detail::update_clnt( clnt, get_host(addr), get_port(addr), 
                                                     etdc::udt_rcvbuf{bufSz}, etdc::udt_sndbuf{bufSz},
                                                     etdc::so_rcvbuf{bufSz}, etdc::so_sndbuf{bufSz},
                                                     isCancelled );

                    // decide on which mss to use
                    // If set to 0 (default) do not change
                    using key_type    = std::pair<bool, bool>;
                    using mssmap_type = std::map<key_type, std::function<etdc::udt_mss(int, int)>>;
                    static const mssmap_type mss_map{
                        // If both sides have an MSS setting, use the minimum
                        {key_type{true, true},   [](int o, int t) { return etdc::udt_mss{ std::min(o, t)}; }},
                        // either have one set? use that one
                        {key_type{true, false},  [](int o, int  ) { return etdc::udt_mss{ o }; }},
                        {key_type{false, true},  [](int  , int t) { return etdc::udt_mss{ t }; }},
                        // neither have set it, don't set it here either
                        {key_type{false, false}, [](int  , int  ) { return etdc::udt_mss{ 0 }; }}
                    };

                    auto       oMSS{ untag(ourMSS) };
                    auto       tMSS{ untag(get_mss(addr)) };
                    const auto mss_to_use = mss_map.find( key_type{oMSS>0, tMSS>0} )->second(oMSS, tMSS);

                    ETDCDEBUG(4, "ETDServer::getFile/use MSS=" << untag(mss_to_use) << " [ours=" << oMSS << ", "
                                                               << get_host(addr) << "=" << tMSS << "]" << std::endl);
                    if( untag(mss_to_use) ) 
                        etdc::detail::update_clnt( clnt, mss_to_use );

                    // same applies to bandwidth constraints?
                    auto     oBW{ untag(ourBW) };
                    auto     tBW{ untag(get_max_bw(addr)) };

                    // If either side has a bw restriction we adapt to that
                    using maxbwmap_type = std::map<key_type, std::function<etdc::udt_max_bw(int64_t, int64_t)>>;
                    static const maxbwmap_type maxbw_map{
                        // both restricted - return minimum
                        {key_type{true, true},   [](int64_t o, int64_t t) { return etdc::udt_max_bw{ std::min(o, t)}; }},
                        {key_type{true, false},  [](int64_t o, int64_t  ) { return etdc::udt_max_bw{ o }; }},
                        {key_type{false, true},  [](int64_t  , int64_t t) { return etdc::udt_max_bw{ t }; }},
                        {key_type{false, false}, [](int64_t  , int64_t  ) { return etdc::udt_max_bw{ -1 }; }}
                    };

                    const auto maxbw = maxbw_map.find( key_type{oBW>0, tBW>0} )->second(oBW, tBW);

                    ETDCDEBUG(4, "ETDServer::getFile/use MaxBW=" << untag(maxbw) << " [ours=" << oBW << ", "
                                                                 << get_host(addr) << "=" << tBW << "]" << std::endl);

                    etdc::detail::update_clnt( clnt, maxbw );


                    transfer.data_fd = mk_client( get_protocol(addr), clnt );
                    ETDCDEBUG(2, "getFile/connected to " << addr << std::endl);
                    break;
                }
                catch( std::exception const& e ) {
                    tried << addr << ": " << e.what() << ", ";
                }
                catch( ... ) {
                    tried << addr << ": unknown exception" << ", ";
                }
            }
            if( (cancelled = isCancelled()) )
                break;
            ETDCASSERT(transfer.data_fd, "Failed to connect to any of the data servers: " << tried.str());

            // EskilSpecial!
            ETDCDEBUG( __m_daemon ? 1  : 1000, "getFile[" << ptr->second->path << "] start reading from " << 
                                               transfer.data_fd->getpeername( transfer.data_fd->__m_fd ) << std::endl);

            // Weehee! we're connected!
            std::unique_ptr<unsigned char[]> buffer(new unsigned char[bufSz]);

            // Create message header
            std::ostringstream  msg_buf;
            msg_buf << "{ uuid:" << srcUUID << ", push:1, sz:" << todo << "}";

            bool              remoteOK{ true };
            std::string       reason;
            std::string const msg( msg_buf.str() );
            auto const        start_tm = std::chrono::high_resolution_clock::now();
            transfer.data_fd->write(transfer.data_fd->__m_fd, msg.data(), msg.size());

            while( todo>0 && !(cancelled = isCancelled()) ) {
                // Read at most bufSz bytes
                // Note: we do blocking I/O so a read of size zero means
                //       other side hung up
                ssize_t       nWritten{0};
                const ssize_t nRead = transfer.data_fd->read(transfer.data_fd->__m_fd, &buffer[0], bufSz);

                if( nRead<=0 ) {
                    reason = std::string("getFile/problem: ") + (nRead==0 ? std::string("remote side hung up") : etdc::strerror(errno));
                    break;
                }
                while( nWritten<nRead ) {
                    ssize_t const thisWrite = transfer.fd->write(transfer.fd->__m_fd, &buffer[nWritten], nRead-nWritten);

                    if( thisWrite<=0 ) {
                        reason   = ((thisWrite==-1) ? std::string(etdc::strerror(errno)) : std::string("write should never have returned 0"));
                        remoteOK = false;
                        break;
                    }
                    nWritten += thisWrite;
                }
                if( nWritten<nRead )
                    break;
                todo -= (off_t)nWritten;
                if( (cancelled = isCancelled()) )
                    break;
            }
            // if we make it out of the loop, todo should be <= 0 and terminate the outer loop
            // Send ACK but only if it makes sense
            if( remoteOK && !cancelled ) {
                const char ack{ 'y' };
                ETDCDEBUG(4, "ETDServer::getFile/got all bytes, sending ACK ..." << std::endl);
                transfer.data_fd->write(transfer.data_fd->__m_fd, &ack, 1);
                ETDCDEBUG(4, "ETDServer::getFile/... done." << std::endl);
            }
            auto const end_tm = std::chrono::high_resolution_clock::now();
            auto const res    = cancelled ? xfer_result(false, 0, "Cancelled", xfer_result::duration_type()) :
                                            xfer_result((todo==0), nTodo - todo, reason, (end_tm-start_tm));
            ETDCDEBUG( __m_daemon ? 1  : 1000, "getFile[" << ptr->second->path << "]: " << std::boolalpha <<
                                               res.__m_Finished << " " <<
                                               res.__m_Reason << " " <<
                                               res.__m_BytesTransferred << " bytes in " <<
                                               res.__m_DeltaT.count() << " seconds" <<
                                               std::endl);
            return res;
            //return cancelled ? xfer_result(false, 0, "Cancelled", xfer_result::duration_type()) :
            //                   xfer_result((todo==0), nTodo - todo, reason, (end_tm-start_tm));
        }
        return xfer_result(false, 0, cancelled ? "Cancelled" : "Failed to grab both locks", xfer_result::duration_type());
    }

    // Cancel any ongoing data transfer
    void ETDServer::cancel( etdc::uuid_type const& uuid ) {
        ETDCASSERT(uuid==__m_uuid, "Cannot cancel someone else's UUID!");

        // We need to do some thinking about locking sequence because we need
        // a lock on the shared state *and* a lock on the transfer
        // before we can attempt to remove it.
        // To prevent deadlock we may have to relinquish the locks and start again.
        // What that means is that if we fail to lock both atomically, we must start over:
        //  lock shared state and (attempt to) find the transfer
        // because after we've released the shared state lock, someone else may have snuck in
        // and deleted or done something bad with the transfer i.e. we cannot do a ".find(uuid)" once 
        // and assume the iterator will remain valid after releasing the lock on shared_state
        etdc::etd_state&                    shared_state( __m_shared_state.get() );
        std::unique_ptr<transferprops_type> removed;

        // 1. lock shared state
        std::unique_lock<std::mutex>     lk( shared_state.lock );
        // 2. find if there is an entry in the map for us
        etdc::transfermap_type::iterator ptr = shared_state.transfers.find(__m_uuid);

        // No? OK then we're done
        if( ptr==shared_state.transfers.end() )
            return;

        // If we're doing a transfer, make it fall out of the loop?
        // Note that the lock on the transfer itself is held during
        // the whole transfer
        ptr->second->cancelled.store( true );
        if( ptr->second->data_fd )
            ptr->second->data_fd->close( ptr->second->data_fd->__m_fd );
        return;
    }

    protocolversion_type ETDServer::protocolVersion( void ) const {
        return ETDServerInterface::currentProtocolVersion;
    }

    ETDServer::~ETDServer() {
        // we must clean up our UUID!
        try {
            this->removeUUID( __m_uuid );
        }
        catch(...) {}
    }


    /////////////////////////////////////////////////////////////////////////////////////////
    //
    //     This is an ETDProxy.
    //     It /looks/ like a real ETDServer but behind the scenes it communicates
    //     with a remote instance of ETDServer, w/o the client knowing anything 
    //     about that
    //
    /////////////////////////////////////////////////////////////////////////////////////////
    //static std::basic_regex<unsigned char> rxLine;
    static const std::regex::flag_type etdc_rxFlags = (std::regex::ECMAScript | std::regex::icase);
    static const std::regex            rxLine("([^\\r\\n]+)[\\r\\n]+");
    static const std::regex            rxReply("^(OK|ERR)(\\s+(\\S.*)?)?$", etdc_rxFlags);
                                             //  1       2    3   submatch numbers
    static const bool                  noMatch = false;
    // Update Jun 2018: we need sendFile/getFile to return more detail than
    //              OK | ERR <reason>
    //     we need to be able to return #-of-bytes transferred (integer) and a time
    //     span (double, seconds).
    //     to not break backward compatibility they're going to be
    //     comma-separated after OK/ERR. Reason will remain.
    //     If those fields are missing
    static const std::regex            rxXferResultReply("^(OK|ERR)(,([0-9]+),([-0-9\\.\\+eE]+))?(\\s+\\S.*)?$", etdc_rxFlags);
    //                                     submatches:     1       2 3        4                  5

    template <typename InputIter, typename OutputIter,
              typename RegexIter  = std::regex_iterator<InputIter>,
              typename RetvalType = typename std::match_results<InputIter>::size_type>
    static RetvalType getReplies(InputIter f, InputIter l, OutputIter o) {
        static const RegexIter  rxIterEnd{};

        RetvalType endpos = 0;
        for(RegexIter line = RegexIter(f, l, rxLine); line!=rxIterEnd; line++) {
            // Found another line: append just the non-newline stuff to output and update endpos
            *o++   = line->str(1);
            endpos = line->position() + line->length();
        }
        return endpos;
    }

    filelist_type ETDProxy::listPath(std::string const& path, bool) const {
        std::ostringstream   msgBuf;

        msgBuf << "list " << path << '\n';
        const std::string  msg( msgBuf.str() );

        ETDCDEBUG(4, "ETDProxy::listPath/sending message '" << msg << "'" << std::endl);
        ETDCASSERTX(__m_connection->write(__m_connection->__m_fd, msg.data(), msg.size())==(ssize_t)msg.size());

        // And await the reply
        const size_t            bufSz( 16384 );
        std::unique_ptr<char[]> buffer(new char[bufSz]);

        bool          finished{ false };
        size_t        curPos{ 0 };
        std::string   state;
        filelist_type rv;

        while( !finished && curPos<bufSz ) {
            const ssize_t n = __m_connection->read(__m_connection->__m_fd, &buffer[curPos], bufSz-curPos);

            // did we read anything?
            ETDCASSERT(n>0, "Failed to read data from remote end");
            curPos += n;

            // Parse the reply so far
            std::list<std::string> lines;
            std::smatch::size_type endpos = getReplies(&buffer[0], &buffer[curPos], std::back_inserter(lines));
            auto                   line = lines.begin();

            // Check what we got back
            for(; !finished && line!=lines.end(); line++) {
                std::smatch   fields;

                ETDCDEBUG(4, "listPath/reply from server: '" << *line << "'" << std::endl);
                ETDCASSERT(std::regex_match(*line, fields, rxReply), "Server replied with an invalid line");
                // error code must be either == current state (all lines starting with OK)
                // or state.empty && error code = ERR; we cannot have OK, OK, OK, ERR -> it's either ERR or OK, OK, OK, ... OK
                ETDCASSERT(state.empty() || (state=="OK" && fields[1].str()==state),
                           "The server changed its mind about the success of the call in the middle of the reply");
                state  = fields[1].str();

                const std::string   info( fields[3].str() ); 

                // Translate error into an exception
                if( state=="ERR" )
                    throw std::runtime_error(std::string("listPath(")+path+") failed - " + (info.empty() ? "<unknown reason>" : info));

                // This is the end-of-reply sentinel: a single OK by itself
                if( (finished=(state=="OK" && info.empty()))==true )
                    continue;
                // Otherwise append the entry to the list of paths
                rv.push_back( info );
            }
            ETDCASSERT(line==lines.end(), "There are unprocessed lines of reply from the server. This is probably a protocol error.");
            // Processed all lines in the reply so far.
            // So we move all processed bytes to begin of buffer
            ::memmove(&buffer[0], &buffer[endpos], curPos - endpos);
            curPos -= endpos;
        }
        ETDCASSERT(curPos==0, "listPath: there are " << curPos << " unconsumed bytes left in the input. This is likely a protocol error.");
        return rv;
    }

    result_type ETDProxy::requestFileWrite(std::string const& file, openmode_type om) {
        static const std::regex  rxUUID( "^UUID:(\\S+)$", etdc_rxFlags);
        static const std::regex  rxAlreadyHave( "^AlreadyHave:([0-9]+)$", etdc_rxFlags);
        std::ostringstream       msgBuf;

        msgBuf << "write-file-" << om << " " << file << '\n';
        const std::string  msg( msgBuf.str() );

        ETDCDEBUG(4, "ETDProxy::requestFileWrite/sending message '" << msg << "' sz=" << msg.size() << std::endl);
        ETDCASSERTX(__m_connection->write(__m_connection->__m_fd, msg.data(), msg.size())==(ssize_t)msg.size());

        // And await the reply
        const size_t               bufSz( 2048 );
        std::unique_ptr<char[]>    buffer(new char[bufSz]);

        bool                       finished{ false };
        size_t                     curPos{ 0 };
        std::string                status_s, info;
        std::unique_ptr<off_t>     filePos{};
        std::unique_ptr<uuid_type> curUUID{};

        while( !finished && curPos<bufSz ) {
            const ssize_t n = __m_connection->read(__m_connection->__m_fd, &buffer[curPos], bufSz-curPos);

            // did we read anything?
            ETDCASSERT(n>0, "Failed to read data from remote end");
            curPos += n;
            std::vector<std::string>  lines;
            auto                      endpos = getReplies(&buffer[0], &buffer[curPos], std::back_inserter(lines));
            auto                      line = lines.cbegin();

            // Check what we got back
            for(; !finished && line!=lines.end(); line++) {
                std::smatch   fields;

                if( std::regex_match(*line, fields, rxUUID) ) {
                    ETDCASSERT(!curUUID, "Server had already sent a UUID");
                    curUUID = std::unique_ptr<uuid_type>(new uuid_type(fields.str(1)));
                } else if( std::regex_match(*line, fields, rxAlreadyHave) ) {
                    ETDCASSERT(!filePos, "Server had already sent file position");
                    filePos = std::unique_ptr<off_t>(new off_t);
                    string2off_t(fields.str(1), *filePos);
                } else if( std::regex_match(*line, fields, rxReply) ) {
                    // We get OK (optional stuff)
                    // or     ERR (optional error message)
                    // Either will mean end-of-parsing
                    status_s = fields.str(1);
                    info     = fields.str(3);
                    finished = true;
                } else {
                    ETDCASSERT(noMatch, "requestFileWrite: the server sent a reply we did not recognize: '" << *line << "'");
                }
            }
            ETDCASSERT(line==lines.end(), "requestFileWrite: there are unprocessed lines of input left, this means the server sent an erroneous reply.");
            // Now we're sure we've processed all lines in the reply so far.
            // So we move all processed bytes to begin of buffer
            ::memmove(&buffer[0], &buffer[endpos], curPos - endpos);
            curPos -= endpos;
        }
        // We must have consumed all output from the server
        ETDCASSERT(curPos==0, "requestFileWrite: there are " << curPos << " unconsumed server bytes left in the input. This is likely a protocol error.");
        // We must have seen a success reply
        // Update: if the info contains the string 'File exists' we
        // translate the error into the magic "exist that should not exist"
        // error code
        // If the user has not changed their system language it may even
        // work with older versions of the daemon that send the strerror(3)
        // message literally *fingers crossed*
        // (Daemon w/ protocol version 1 and up send 'File exists' always.
        if( info.find("File exists")!=std::string::npos )
            throw etdc::detail::ThrowOnExistThatShouldNotExist{};

        ETDCASSERT(status_s=="OK", "requestFileWrite(" << file << ") failed - " << (info.empty() ? "<unknown reason>" : info));
        // And we must have received both a UUID as well as an AlreadyHave
        ETDCASSERT(filePos && curUUID, "requestFileWrite: the server did NOT send all required fields");
        return result_type{*curUUID, *filePos};
    }

    result_type ETDProxy::requestFileRead(std::string const& file, off_t already_have) {
        static const std::regex  rxUUID( "^UUID:(\\S+)$", etdc_rxFlags);
        static const std::regex  rxRemain( "^Remain:(-?[0-9]+)$", etdc_rxFlags);
        std::ostringstream       msgBuf;

        msgBuf << "read-file " << already_have << " " << file << '\n';
        const std::string  msg( msgBuf.str() );

        ETDCDEBUG(4, "ETDProxy::requestFileRead/sending message '" << msg << "'" << std::endl);
        ETDCASSERTX(__m_connection->write(__m_connection->__m_fd, msg.data(), msg.size())==(ssize_t)msg.size());

        // And await the reply
        const size_t               bufSz( 2048 );
        std::unique_ptr<char[]>    buffer(new char[bufSz]);

        bool                       finished{ false };
        size_t                     curPos{ 0 };
        std::string                info, status_s;
        std::unique_ptr<off_t>     remain{};
        std::unique_ptr<uuid_type> curUUID{};

        while( !finished && curPos<bufSz ) {
            const ssize_t n = __m_connection->read(__m_connection->__m_fd, &buffer[curPos], bufSz-curPos);

            // did we read anything?
            ETDCASSERT(n>0, "Failed to read data from remote end");
            curPos += n;

            std::vector<std::string>  lines{};
            auto                      endpos = getReplies(&buffer[0], &buffer[curPos], std::back_inserter(lines));
            auto                      line = lines.cbegin();

            // Check what we got back
            for(; !finished && line!=lines.end(); line++) {
                std::smatch   fields;

                if( std::regex_match(*line, fields, rxUUID) ) {
                    ETDCASSERT(!curUUID, "Server already sent a UUID");
                    curUUID = std::unique_ptr<uuid_type>(new uuid_type(fields.str(1)));
                } else if( std::regex_match(*line, fields, rxRemain) ) {
                    ETDCASSERT(!remain, "Server already sent a file position");
                    remain = std::unique_ptr<off_t>(new off_t);
                    string2off_t(fields.str(1), *remain);
                } else if( std::regex_match(*line, fields, rxReply) ) {
                    // We get OK (optional stuff)
                    // or     ERR (optional error message)
                    // Either will mean end-of-parsing
                    status_s = fields.str(1);
                    info     = fields.str(3); 
                    finished = true;
                } else {
                    ETDCASSERT(noMatch, "requestFileRead: the server sent a reply we did not recognize: " << *line);
                }
            }
            ETDCASSERT(line==lines.end(), "requestFileRead: there are unprocessed lines of input left, this means the server sent an erroneous reply.");
            // Now we're sure we've processed all lines in the reply so far.
            // So we move all processed bytes to begin of buffer
            ::memmove(&buffer[0], &buffer[endpos], curPos - endpos);
            curPos -= endpos;
        }
        // We must have consumed all output from the server
        ETDCASSERT(curPos==0, "requestFileRead: there are " << curPos << " unconsumed server bytes left in the input. This is likely a protocol error.");
        // We must have seen a success reply
        ETDCASSERT(status_s=="OK", "requestFileRead(" << file << ") failed - " << (info.empty() ? "<unknown reason>" : info));
        // And we must have received both a UUID as well as an AlreadyHave
        ETDCASSERT(remain && curUUID, "requestFileRead: the server did NOT send all required fields");
        return result_type{*curUUID, *remain};
    }

    dataaddrlist_type ETDProxy::dataChannelAddr( void ) const {
        // We are a proxy for a remote end and if we know that the remote end supports extended
        // data channel specification we ask for that
        static const std::string msg{ (__m_protocolVersion == 0 || __m_protocolVersion == ETDServerInterface::unknownProtocolVersion) ?
                                      "data-channel-addr\n" : "data-channel-addr-ext\n" };
        ETDCDEBUG(4, "ETDProxy::dataChannelAddr/sending message '" << msg << "'" << std::endl);
        ETDCASSERTX(__m_connection->write(__m_connection->__m_fd, msg.data(), msg.size())==(ssize_t)msg.size());

        // And await the reply. We don't expect /a lot/ of data channel addrs so don't need a really big buf
        const size_t            bufSz( 2048 );
        std::unique_ptr<char[]> buffer(new char[bufSz]);

        bool              finished{ false };
        size_t            curPos{ 0 };
        std::string       state;
        dataaddrlist_type rv;

        while( !finished && curPos<bufSz ) {
            const ssize_t n = __m_connection->read(__m_connection->__m_fd, &buffer[curPos], bufSz-curPos);

            // did we read anything?
            ETDCASSERT(n>0, "Failed to read data from remote end");
            curPos += n;

            // Parse the reply so far
            std::list<std::string> lines;
            std::smatch::size_type endpos = getReplies(&buffer[0], &buffer[curPos], std::back_inserter(lines));
            auto                   line = lines.begin();

            // Check what we got back
            for(; !finished && line!=lines.end(); line++) {
                std::smatch   fields;

                ETDCDEBUG(4, "dataChannelAddr/reply from server: '" << *line << "'" << std::endl);
                ETDCASSERT(std::regex_match(*line, fields, rxReply), "Server replied with an invalid line");
                // error code must be either == current state (all lines starting with OK)
                // or state.empty && error code = ERR; we cannot have OK, OK, OK, ERR -> it's either ERR or OK, OK, OK, ... OK
                ETDCASSERT(state.empty() || (state=="OK" && fields[1].str()==state),
                           "The server changed its mind about the success of the call in the middle of the reply");
                state  = fields[1].str();

                const std::string   info( fields[3].str() ); 

                // Translate error into an exception
                if( state=="ERR" )
                    throw std::runtime_error(std::string("dataChannelAddr() failed - ") + (info.empty() ? "<unknown reason>" : info));

                // This is the end-of-reply sentinel: a single OK by itself
                // despite it looks like we continue, the while loop will terminate because finish now is .TRUE.
                if( (finished=(state=="OK" && info.empty()))==true )
                    continue;
                // Otherwise append the entry to the list of paths
                rv.push_back( decode_data_addr(info) );
            }
            ETDCASSERT(line==lines.end(), "There are unprocessed lines of reply from the server. This is probably a protocol error.");
            // Processed all lines in the reply so far.
            // So we move all processed bytes to begin of buffer
            ::memmove(&buffer[0], &buffer[endpos], curPos - endpos);
            curPos -= endpos;
        }
        ETDCASSERT(curPos==0, "dataChannelAddr: there are " << curPos << " unconsumed bytes left in the input. This is likely a protocol error.");
        return rv;
    }

    bool ETDProxy::removeUUID(uuid_type const& uuid) {
        std::ostringstream       msgBuf;

        msgBuf << "remove-uuid " << uuid << '\n';
        const std::string  msg( msgBuf.str() );

        ETDCDEBUG(4, "ETDProxy::removeUUID/sending message '" << msg << "'" << " fd=" << __m_connection->__m_fd << std::endl);
        ETDCASSERTX(__m_connection->write(__m_connection->__m_fd, msg.data(), msg.size())==(ssize_t)msg.size());

        // And await the reply. We only allow "OK" or "ERR <msg>"
        // if we allow ~1kB for the <msg> that's quite generous I'd say
        size_t                     curPos{ 0 };
        const size_t               bufSz( 2048 );
        std::unique_ptr<char[]>    buffer(new char[bufSz]);

        while( curPos<bufSz ) {
            const ssize_t n = __m_connection->read(__m_connection->__m_fd, &buffer[curPos], bufSz-curPos);

            // did we read anything?
            ETDCASSERT(n>0, "Failed to read data from remote end");
            curPos += n;

            std::vector<std::string>  lines;
            std::smatch               fields;

            // Discard the return value from getReplies - we don't need to remember where we end in the buffer
            (void)getReplies(&buffer[0], &buffer[curPos], std::back_inserter(lines));

            // If no line(s) yet, read more bytes
            if( lines.empty() )
                continue;

            // If we get >1 line, the client's messin' wiv de heads - we only allow 1 (one) line of reply
            ETDCASSERT(lines.size()==1, "The client sent wrong number of responses - this is likely a protocol error");
            // And that line should match our expectations
            ETDCASSERT(std::regex_match(*lines.begin(), fields, rxReply), "The client sent a non-conforming response");
            // Translate "ERR <Reason>" into an exception
            ETDCASSERT(fields[1].str()=="OK", "removeUUID failed: " << fields[2].str());
            // Otherwise we're done
            break;
        }
        ETDCDEBUG(4, "ETDProxy::removeUUID/uuid removed succesfully" << std::endl);
        return true;
    }

    protocolversion_type ETDProxy::set_protocolVersion( protocolversion_type pvn ) {
        // unfortunately std::swap() is declared as "void std::swap(...)"
        protocolversion_type const previous = __m_protocolVersion;

        __m_protocolVersion = pvn;
        return previous;
    }

    xfer_result ETDProxy::sendFile(uuid_type const& srcUUID, uuid_type const& dstUUID, off_t todo, dataaddrlist_type const& dataaddrs) {
        sockname2string_fn       f{ sockname2str( __m_protocolVersion ) };
        std::ostringstream       msgBuf;

        msgBuf << "send-file " << srcUUID << " " << dstUUID << " " << todo << " ";
        for(auto p = dataaddrs.begin(); p!=dataaddrs.end(); p++)
            msgBuf << ((p!=dataaddrs.begin()) ? "," : "") << f( *p );
        msgBuf << '\n';
        const std::string  msg( msgBuf.str() );

        ETDCDEBUG(4, "ETDProxy::sendFile/sending message '" << msg << "'" << " fd=" << __m_connection->__m_fd << std::endl);
        ETDCASSERTX(__m_connection->write(__m_connection->__m_fd, msg.data(), msg.size())==(ssize_t)msg.size());

        // The values we need to parse from the reply
        bool                       success{false};         // can be inferred from "OK" or "ERR ..." response
        off_t                      nbyte_transferred{ 0 }; // provide defaults; older servers don't return this
        double                     delta_t{ 0.0 };         //    id.
        std::string                reason{};

        // And await the reply. Update Jun 2018: accept more elaborate reply
        // if we allow ~2kB for the <msg> that's quite generous I'd say
        size_t                     curPos{ 0 };
        const size_t               bufSz( 2048 );
        std::unique_ptr<char[]>    buffer(new char[bufSz]);

        while( curPos<bufSz ) {
            const ssize_t n = __m_connection->read(__m_connection->__m_fd, &buffer[curPos], bufSz-curPos);

            // did we read anything?
            ETDCASSERT(n>0, "Failed to read data from remote end");
            curPos += n;

            std::vector<std::string>  lines;
            std::smatch               fields;

            // Discard the return value from getReplies - we don't need to remember where we end in the buffer
            (void)getReplies(&buffer[0], &buffer[curPos], std::back_inserter(lines));

            // If no line(s) yet, read more bytes
            if( lines.empty() )
                continue;

            // If we get >1 line, the client's messin' wiv de heads - we only allow 1 (one) line of reply
            ETDCASSERT(lines.size()==1, "The client sent wrong number of responses - this is likely a protocol error");
            // And that line should match our expectations
            ETDCASSERT(std::regex_match(*lines.begin(), fields, rxXferResultReply), "The client sent a non-conforming response");
            //    "^(OK|ERR)(,([0-9]+),([-0-9\\.\\+eE]+))?(\\s+\\S.*)?$"
            //      1       2 3        4                  5
            // Field 1 always exists
            success = (fields[1].str()=="OK");

            // Check optional fields
            std::string     tmp;
            if( (tmp = fields[3].str()).empty()==false ) {
                // have new-style reply!
                string2off_t(tmp, nbyte_transferred);
                // then we also *know* we have field 4!
                delta_t = std::stod(fields[4].str());
            }
            // Was there a reason?
            reason = fields[5].str();
            // Otherwise we're done
            break;
        }
        return xfer_result(success, nbyte_transferred, reason, xfer_result::duration_type(delta_t));
    }

    // Cancel the current transfer
    void ETDProxy::cancel( etdc::uuid_type const& uuid ) {
        // remote end w/ protocol version 0 doesn't have cancel so try removeUUID()
        if( __m_protocolVersion==0 || __m_protocolVersion==ETDServerInterface::unknownProtocolVersion ) {
            ETDCDEBUG(4, "ETDProxy::cancel(" << uuid << ") - remote end doesn't support it, trying removeUUID instead" << std::endl);
            this->removeUUID( uuid );
            return;
        }
        //  OK send cancel message
        std::ostringstream       msgBuf;

        msgBuf << "cancel " << uuid << '\n';
        const std::string  msg( msgBuf.str() );
        ETDCDEBUG(4, "ETDProxy::cancel/sending message '" << msg << "'" << std::endl);
        ETDCASSERTX(__m_connection->write(__m_connection->__m_fd, msg.data(), msg.size())==(ssize_t)msg.size());

        // This one does NOT solicit a reply
        return;
    }


    protocolversion_type ETDProxy::protocolVersion( void ) const {
        // If we already enquired the protocol Version no need to bother the
        // server again
        if( __m_protocolVersion!=unknownProtocolVersion )
            return __m_protocolVersion;

        // Hmmm don't know what's at the other end, better check
        static const std::string msg{ "protocol-version\n" };
        ETDCDEBUG(4, "ETDProxy::protocolVersion/sending message '" << msg << "'" << std::endl);
        ETDCASSERTX(__m_connection->write(__m_connection->__m_fd, msg.data(), msg.size())==(ssize_t)msg.size());

        // And await the reply. We don't expect /a lot/ of data channel addrs so don't need a really big buf
        const size_t            bufSz( 2048 );
        std::unique_ptr<char[]> buffer(new char[bufSz]);

        size_t            curPos{ 0 };
        std::string       state;

        while( curPos<bufSz ) {
            const ssize_t n = __m_connection->read(__m_connection->__m_fd, &buffer[curPos], bufSz-curPos);

            // did we read anything?
            ETDCASSERT(n>0, "Failed to read data from remote end");
            curPos += n;

            std::vector<std::string>  lines;
            std::smatch               fields;

            // Discard the return value from getReplies - we don't need to remember where we end in the buffer
            (void)getReplies(&buffer[0], &buffer[curPos], std::back_inserter(lines));

            // If no line(s) yet, read more bytes
            if( lines.empty() )
                continue;

            // If we get >1 line, the client's messin' wiv de heads - we only allow 1 (one) line of reply
            ETDCASSERT(lines.size()==1, "The client sent wrong number of responses - this is likely a protocol error");
            // And that line should match our expectations
            ETDCASSERT(std::regex_match(*lines.begin(), fields, rxReply), "The client sent a non-conforming response");
            // Translate "ERR <Reason>" into an exception
            ETDCASSERT(fields[1].str()=="OK", "protocolVersion failed: " << fields[2].str());

            // The format should be "OK <number>"
            __m_protocolVersion = std::stoul( fields[3].str() );

            // Otherwise we're done
            break;
        }
        return __m_protocolVersion;
    }

    //////////////////////////////////////////////////////////////////////
    //
    // This class does NOT implementing the ETDServerInterface but
    // takes a connection, instantiates its own ETDServer
    // and then loops, reading commands from the connection and sends
    // back replies
    //
    //////////////////////////////////////////////////////////////////////

    void ETDServerWrapper::handle( void ) {
        // here we enter our while loop, reading commands and (attempt) to
        // interpret them.
        // If we go 2kB w/o seeing an actual command we call it a day
        // I mean, our commands are typically *very* small
        const size_t            bufSz( 2*1024/**1024*/ );
        std::unique_ptr<char[]> buffer(new char[bufSz]);

        bool          terminated = false;
        size_t        curPos = 0;

        while( !terminated && curPos<bufSz ) {
            ETDCDEBUG(5, "ETDServerWrapper::handle() / start loop, curPos=" << curPos << std::endl);
            const ssize_t n = __m_connection->read(__m_connection->__m_fd, &buffer[curPos], bufSz-curPos);
            ETDCDEBUG(5, "ETDServerWrapper::handle() / read n=" << n << " => nTotal=" << n + curPos << std::endl);
            // did we read anything?
            ETDCASSERT(n>0, "Failed to read data from remote end");
            curPos += n;

            // Parse the reply so far
            std::list<std::string> lines;
            std::smatch::size_type endpos = getReplies(&buffer[0], &buffer[curPos], std::back_inserter(lines));
            auto                   line = lines.begin();

            for( ; line!=lines.end(); line++ ) {
                // Got a line! Assert that it conforms to our expectation
                ETDCDEBUG(4, "ETDServerWrapper::handle()/got line: '" << *line << "'" << std::endl);

                // The known commands
                static const std::regex  rxList("^list\\s+(\\S.*)$", etdc_rxFlags);
                static const std::regex  rxReqFileWrite("^write-file-(\\S+)\\s+(\\S.*)$", etdc_rxFlags);
                                                //                   1         2
                                                //                   openmode  file name
                static const std::regex  rxReqFileRead("^read-file\\s+([0-9]+)\\s+(\\S.*)$", etdc_rxFlags);
                                                //                    1           2
                                                //                    already have
                                                //                                file name
                static const std::regex  rxSendFile("^send-file\\s+(\\S+)\\s+(\\S+)\\s+([0-9]+)\\s+(\\S+)$", etdc_rxFlags);
                                                //                 1         2         3           4
                                                //                 srcUUID   dstUUID   todo        data-channel
                static const std::regex  rxDataChannelAddr("^data-channel-addr(-ext)?$", etdc_rxFlags);
                                                //                            1 extended info?
                static const std::regex  rxRemoveUUID("^(remove-uuid|cancel)\\s+(\\S+)$", etdc_rxFlags);
                                                //      1              2
                                                //      what to do     UUID
                static const std::regex  rxProtocolVersion("^protocol-version$", etdc_rxFlags);

                // Match it against the known commands
                std::smatch              fields;
                std::vector<std::string> replies;

                try {
                    if( std::regex_match(*line, fields, rxList) ) {
                        // we're a remote ETDServer (seen from the client)
                        // so we do not support ~ expansion
                        const auto entries = __m_etdserver.listPath(fields[1].str(), false);
                        std::transform(std::begin(entries), std::end(entries), std::back_inserter(replies),
                                       std::bind(std::plus<std::string>(), std::string("OK "), std::placeholders::_1));
                        // and add a final OK
                        replies.emplace_back("OK");
                    } else if( std::regex_match(*line, fields, rxReqFileWrite) ) {
                        openmode_type      om;
                        std::istringstream iss( fields[1].str() );
                        // Transform openmode string to actual openmode enum
                        iss >> om;
                        // Do the actual filewrite request
                        const auto         fwresult = __m_etdserver.requestFileWrite(fields[2].str(), om);
                        std::ostringstream oss;
                        // Prepare replies
                        oss << "AlreadyHave:" << get_filepos(fwresult);
                        replies.emplace_back(oss.str());
                        replies.emplace_back("UUID:"+get_uuid(fwresult));
                        replies.emplace_back("OK");
                    } else if( std::regex_match(*line, fields, rxReqFileRead) ) {
                        // Decode the filepos from the sent command into
                        // local, correctly typed, variable
                        off_t               already_have;;
                        string2off_t(fields[1].str(), already_have);

                        // Do the actual fileread request
                        const auto frresult = __m_etdserver.requestFileRead(fields[2].str(), already_have);

                        // Prepare replies
                        std::ostringstream  oss;
                        oss << "Remain:" << get_filepos(frresult);
                        replies.emplace_back(oss.str());
                        replies.emplace_back("UUID:"+get_uuid(frresult));
                        replies.emplace_back("OK");
                    } else if( std::regex_match(*line, fields, rxSendFile) ) {
                        // Decode the fields 
                        off_t                 todo;
                        const std::string     dataAddrs_s( fields[4].str() );
                        dataaddrlist_type     dataAddrs;
                        const etdc::uuid_type src_uuid{ fields[1].str() };
                        const etdc::uuid_type dst_uuid{ fields[2].str() };

                        string2off_t(fields[3].str(), todo);
                        // transform data channel addresses into list-of-*
                        static const std::regex data_sep( "<[^>]+>" );
                        std::transform( std::sregex_iterator(std::begin(dataAddrs_s), std::end(dataAddrs_s), data_sep),
                                        std::sregex_iterator(), std::back_inserter(dataAddrs), 
                                        [](std::smatch const& sm) { return decode_data_addr(sm.str()); });

                        // Execute the sendFile in a separate thread to free up this handler
                        std::thread( [=]() {
                                ETDCDEBUG(4, "ETDServerWrapper: thread " << std::this_thread::get_id() << "/executing sendFile()" << std::endl);
                                std::ostringstream reply_s;
                                try {
                                    const xfer_result  rv = __m_etdserver.sendFile(src_uuid, dst_uuid, todo, dataAddrs);
                                    reply_s << (rv.__m_Finished ? "OK" : "ERR")
                                            << ',' << rv.__m_BytesTransferred
                                            // make sure we have seconds as units of duration
                                            << ',' << rv.__m_DeltaT.count();
                                    if( !rv.__m_Reason.empty() )
                                        reply_s << ' ' << rv.__m_Reason;
                                    reply_s << '\n';
                                }
                                catch( std::exception const& e ) {
                                    reply_s << "ERR,0,0.00 " << e.what() << '\n';
                                }
                                catch( ... ) {
                                    reply_s << "ERR,0,0.00 Unknown exception in sendFile thread\n";
                                }
                                std::string const reply{ reply_s.str() };
                                ETDCDEBUG(4, "ETDServerWrapper: thread " << std::this_thread::get_id() << "/sending sendFile() reply '" << reply << "'" << std::endl);
                                __m_connection->write(__m_connection->__m_fd, reply.data(), reply.size());
                            } ).detach();
                        //replies.emplace_back( rv ? "OK" : "ERR Failed to send file" );
                    } else if( std::regex_match(*line, fields, rxDataChannelAddr) ) {
                        // Did client ask for data-channel-addr-ext?
                        // Note we do not use "sockname2str(protocolVersion)" here because this
                        // is _us_ answering a query from someone else, we are not the *proxy* for someone else
                        auto       f       = (fields[1].str().empty() ? sockname2str_v0 : sockname2str_v1);
                        const auto entries = __m_etdserver.dataChannelAddr();

                        std::transform(std::begin(entries), std::end(entries), std::back_inserter(replies),
                                       [&](sockname_type const& sn) { std::ostringstream oss; oss << "OK " << f(sn); return oss.str(); });
                        // and add a final OK
                        replies.emplace_back("OK");
                    } else if( std::regex_match(*line, fields, rxRemoveUUID) ) {
                        // Could be remove | cancel
                        etdc::uuid_type const  uuid{ fields[2].str() };

                        if( fields[1].str() == "cancel" ) {
                            ETDCDEBUG(4, "ETDServerWrapper: canelling UUID " << uuid << std::endl);
                            __m_etdserver.cancel( uuid );
                            // note: this done does _not_ solicit a return
                        } else {
                            const bool removeResult = __m_etdserver.removeUUID( uuid );
                            ETDCDEBUG(4, "ETDServerWrapper: removeUUID(" << uuid << " yields " << removeResult << std::endl);
                            replies.emplace_back( removeResult ? "OK" : "ERR Failed to remove UUID" );
                        }
                    } else if( std::regex_match(*line, fields, rxProtocolVersion) ) {
                        // and add a final OK
                        replies.emplace_back("OK "+repr(__m_etdserver.protocolVersion()));
                    } else {
                        ETDCDEBUG(4, "line '" << *line << "' did not match any regex" << std::endl);
                        __m_connection->close( __m_connection->__m_fd );
                        throw std::string("client sent unknown command");
                    }
                }
                catch( std::string const& e ) {
                    ETDCDEBUG(-1, "ETDServerWrapper: terminating because of condition " << e << std::endl);
                    terminated = true;
                }
                catch( etdc::detail::ThrowOnExistThatShouldNotExist const& ) {
                    // Thrown as result on request file write
                    replies.emplace_back( "ERR File exists" );
                }
                catch( std::exception const& e ) {
                    replies.emplace_back( std::string("ERR ")+e.what() );
                }
                catch( ... ) {
                    replies.emplace_back( "ERR Unknown exception" );
                }

                // Now send back the replies
                for(auto const& r: replies) {
                    ETDCDEBUG(4, "ETDServerWrapper: sending reply '" << r << "'" << std::endl);
                    __m_connection->write(__m_connection->__m_fd, r.data(), r.size());
                    __m_connection->write(__m_connection->__m_fd, "\n", 1);
                }
            } 
            ETDCASSERT(line==lines.end(), "There were unprocessed lines of input from the client. This is likely a logical error in this server");
            // Processed all lines in the reply so far.
            // So we move all processed bytes to begin of buffer
            ::memmove(&buffer[0], &buffer[endpos], curPos - endpos);
            curPos -= endpos;
        }
        ETDCDEBUG(3, "ETDServerWrapper: terminated." << std::endl);
    }


    //////////////////////////////////////////////////////////////////////
    //
    //  This class also does NOT implement the ETDServerInterface;
    //  this is the ETDDataServer - it only deals with data connections
    //
    //////////////////////////////////////////////////////////////////////
    static const std::regex rxCommand("^(\\{([^\\}]*)\\})");
    //               subgroup indices:  1   2
    //                                      just the fields
    //                                  whole command

    // we support key:value pairs
    using  kvmap_type = std::map<std::string, std::string, etdc::case_insensitive_lt>;
    static const std::regex rxKeyValue("\\b([a-zA-Z][a-zA-Z0-9_-]+)\\s*:\\s*(\"(.*(?!\\\\))\"|[^, \\t\\v]+)", etdc_rxFlags);
    //             subgroup indices:       1                          2   3 quoted string literal
    //                                     key                        complete value 
    // NOTE: in the rxKeyValue we write out [a-zA-Z][a-zA-Z...] despite
    //       etdc_rxFlags contains std::regex::icase. Turns out GCC libstdc++ 
    //       has a bugz - 'case insensitive match' on character range(s)
    //       don't work!
    //          https://gcc.gnu.org/bugzilla/show_bug.cgi?id=71500
    //       At the time of writing this (29 Jun 2017) this was not yet
    //       fixed in the gcc version(s) that I had access to.
    static const std::regex rxSlash("\\\\");
    static const auto       unSlash = [](std::string const& s) { return std::regex_replace(s, rxSlash, ""); };

    template <typename InputIter, typename OutputIter,
              typename RegexIter  = std::regex_iterator<InputIter>,
              typename RetvalType = typename std::match_results<InputIter>::size_type>
    static RetvalType getKeyValuePairs(InputIter f, InputIter l, OutputIter o) {
        static const RegexIter  rxIterEnd{};

        RetvalType endpos = 0;
        for(RegexIter kv = RegexIter(f, l, rxKeyValue); kv!=rxIterEnd; kv++) {
            // Found another key-value pair: append to output and update endpos
            const auto this_kv = *kv;
            *o++   = kvmap_type::value_type{ this_kv[1].str(), unSlash((this_kv[3].length() ? this_kv[3].str() : this_kv[2].str())) };
            endpos = this_kv.position() + this_kv.length();
        }
        return endpos;
    }

    void ETDDataServer::handle( void ) {
        // When writing to a file these are the allowed modes
        static const std::set<openmode_type> allowedWriteModes{openmode_type::New, openmode_type::OverWrite, openmode_type::Resume};
        static const std::set<openmode_type> allowedReadModes{openmode_type::Read};

        // here we enter our while loop, reading commands and (attempt) to
        // interpret them.
        // If we go 2kB w/o seeing an actual command we call it a day
        // I mean, our commands are typically *very* small
        const size_t            maxNoCmdSz( 4*1024 );
        const size_t            bufSz( 10*1024*1024 );
        std::unique_ptr<char[]> buffer(new char[bufSz]);

        bool          terminated = false;
        size_t        curPos = 0;

        // Read at most maxNoCmdSz bytes to see if there is a command
        // embedded. If not, then we assume the client is broken or trying
        // to break us so we just terminate
        while( !terminated && curPos<maxNoCmdSz ) {
            ETDCDEBUG(5, "ETDDataServer::handle() / start loop, curPos=" << curPos << std::endl);
            const ssize_t n = __m_connection->read(__m_connection->__m_fd, &buffer[curPos], maxNoCmdSz-curPos);
            ETDCDEBUG(5, "ETDDataServer::handle() / read n=" << n << " => nTotal=" << n + curPos << std::endl);
            // did we read anything?
            ETDCASSERT(n>0, "Failed to read data from remote end");
            curPos += n;

            // We know that we have a non-zero amount of bytes read from the client.
            // If the first byte is not '{' then we're screwed
            ETDCASSERT(buffer[0]=='{', "Client is messing with us - doesn't look like it is going to send a command");

            // If we end up here we're looking for commands:
            // '{ uuid:.... , sz: ..., [push: 1, data_addr: ....] }' + binary data
            kvmap_type             kvpairs;
            std::cmatch            command;

            if( !std::regex_search((const char*)&buffer[0], (const char*)&buffer[curPos], command, rxCommand) ) {
                ETDCDEBUG(4, "ETDDataServer: so far no command in bytes 0.." << curPos << std::endl);
                continue;
            }
            // OK we found "{ ... }" in the current buffer
            ETDCDEBUG(4, "ETDDataServer: found command @" << command.position() << " + " << command.length() << std::endl);

            // Ignore the return value of getKeyValuePairs - we already
            // have the command match doing that for us
            (void)getKeyValuePairs(&buffer[command.position() + 1], &buffer[command.position() + command.length() - 1],
                                   etdc::no_duplicates_inserter(kvpairs, kvpairs.end()));

            ETDCDEBUG(4, "ETDDataServer: found " << kvpairs.size() << " key-value pairs inside:" << std::endl);
            for(const auto& kv: kvpairs)
                ETDCDEBUG(4, "   " << kv.first << ":" << kv.second << std::endl);

            // By the time we get here, we know for sure:
            //  1.) there was a command '{ ... }' in our buffer
            //  2.From: ) it may have had a number of key-value pairs in there
            //
            // Now it's time to verify:
            //  - we need 'uuid:'  and 'sz:' key-value pairs
            //  - there may be 'push:1' 
            off_t      sz;
            const auto uuidptr = kvpairs.find("uuid");
            const auto szptr   = kvpairs.find("sz");
            const auto pushptr = kvpairs.find("push");

            ETDCASSERT(uuidptr!=kvpairs.end(), "No UUID was sent");
            ETDCASSERT(szptr!=kvpairs.end(), "No amount was sent");
            ETDCASSERT(pushptr==kvpairs.end() || pushptr->second=="1", "push keyword may only take one specific value");
            // The size must be an off_t value
            string2off_t(szptr->second, sz);

            // Verification = complete.
            // Now we must grab a lock on the transfer (if there is one)
            // and do our thang
            const bool                       push = (pushptr!=kvpairs.end());
            etdc::etd_state&                 shared_state( __m_shared_state.get() );
            std::unique_lock<std::mutex>     transfer_lock;
            etdc::transfermap_type::iterator xfer_ptr;

            // Loop until we've got the lock acquired
            while( !transfer_lock.owns_lock() /*true*/ ) {
                // 2a. lock shared state
                std::unique_lock<std::mutex>     lk( shared_state.lock );
                // 2b. assert that there is an entry for the indicated uuid
                xfer_ptr = shared_state.transfers.find(uuid_type(uuidptr->second));

                ETDCASSERT(xfer_ptr!=shared_state.transfers.end(), "No transfer associated with the UUID");

                // Now we must do try_lock on the transfer - if that fails we sleep and start from the beginning
                std::unique_lock<std::mutex>     sh( xfer_ptr->second->xfer_lock, std::try_to_lock );
                if( !sh.owns_lock() ) {
                    // Manually unlock the shared state or else nobody won't be
                    // able to change anything!
                    lk.unlock();

                    // *now* we sleep for a bit and then try again
                    std::this_thread::sleep_for( std::chrono::microseconds(9) );
                    //std::this_thread::sleep_for( std::chrono::seconds(5) );
                    continue;
                }
                // Technically we could've tested the following /before/ getting a
                // lock on the transfer; we're only checking the transfer's
                // properties to make sure it is compatible with the current
                // request.
                // But putting the test in before attempting to lock the
                // transfer would mean that we would be doing this test over
                // and over again until we actually managed to lock the
                // transfer, which sounds a bit wasteful.
                // So now we test it once, after we've acquired the lock
                ETDCASSERT( (push ? allowedReadModes.find(xfer_ptr->second->openMode)!=allowedReadModes.end() :
                                    allowedWriteModes.find(xfer_ptr->second->openMode)!=allowedWriteModes.end()),
                            "The referred-to transfer's open mode (" << xfer_ptr->second->openMode << ") is not compatible with the current data request");
                // move the transfer lock out of this loop;
                // breaking out of the loop will unlock the shared state
                transfer_lock = std::move( sh );
            }
            ETDCDEBUG(5, "ETDDataServer/owning transfer lock, now sucking data!" << std::endl);

            ETDCDEBUG(1, "ETDDataServer: " << (push ? "PUSH" : "PULL") << " " << xfer_ptr->second->path << " " <<
                         (push ? "to" : "from" ) << " " << __m_connection->getpeername( __m_connection->__m_fd ) << std::endl);

            // If we end up here we know that the transfer is locked and
            // that xfer_ptr is pointing at it and that all is good
            // Now defer to appropriate subordinate fn
            
            // We found a valid command in the buffer, there may be raw bytes left following that command.
            // Therefore we initialize our read position to the end of the command we found.
            const size_t  rdPos( command.position() + command.length() ); 
            if( push )
                ETDDataServer::push_n(sz, xfer_ptr->second->fd, __m_connection, rdPos, curPos, bufSz, buffer);
            else
                ETDDataServer::pull_n(sz, __m_connection, xfer_ptr->second->fd, rdPos, curPos, bufSz, buffer);
            // This command has been served, ready to accept next
            curPos = 0;
        }
        ETDCDEBUG(4, "ETDDataServer::handle() / terminated" << std::endl);
    }

    // PUSH n bytes src to dst, using buffer of size bufSz.
    // the bytes between endPos and rdPos is are what was read from the
    // client, following the command. But since we're pushing we're going to 
    // ignore any extra bytes sent by the client and overwrite everything in
    // the buffer
    void ETDDataServer::push_n(size_t n, etdc::etdc_fdptr src, etdc::etdc_fdptr dst,
                               size_t /*rdPos*/, const size_t /*endPos*/, const size_t bufSz, std::unique_ptr<char[]>& buf) {
        while( n>0 ) {
            // Amount of bytes to process in this iteration
            const ssize_t nRead = std::min(n, bufSz);
            ssize_t       aRead, nWritten{ 0 };

            ETDCDEBUG(5, "ETDDataServer::push_n/pushing " << n << " bytes" << std::endl);

            ETDCASSERT((aRead=src->read(src->__m_fd, &buf[0], nRead))>0,
                       ((aRead==-1) ? std::string(etdc::strerror(errno)) : std::string("read() returned 0 - hung up?!")));

            // Keep on writing untill all bytes that were read are actually written
            while( aRead>0 ) {
                ssize_t thisWrite;
                ETDCASSERT((thisWrite=dst->write(dst->__m_fd, &buf[nWritten], aRead))>0,
                           ((thisWrite==-1) ? std::string(etdc::strerror(errno)) : std::string("write should never have returned 0?!")) );
                aRead    -= thisWrite;
                nWritten += thisWrite;
            }
            n -= (size_t)nWritten;
        }
        // Do a read from the destination such that we know it is finished
        char ack;
        ETDCDEBUG(5, "ETDDataServer::push_n/waiting for ACK " << std::endl);
        dst->read(dst->__m_fd, &ack, 1);
        ETDCDEBUG(5, "ETDDataServer::push_n/done." << std::endl);
    }
    // PULL n bytes from rc to dst, using buffer of size bufSz
    // the bytes between endPos and rdPos are what was read from the client,
    // raw bytes immediately following the command. We flush those to the
    // file first and then we can use the whole buffer for reading bytes.
    void ETDDataServer::pull_n(size_t n, etdc::etdc_fdptr src, etdc::etdc_fdptr dst,
                               size_t rdPos, const size_t endPos, const size_t bufSz, std::unique_ptr<char[]>& buf) {
        // rdPos:  current start of read area in buf
        // endPos: passed in from above; this is where the initial command
        //         reader left off
        // wrPos:  current end of read aread in buf
        // bufSz:  size of buf
        size_t  wrEnd( endPos );

        while( n>0 ) {
            // Attempt read as many bytes into our buffer as we can; there
            // should be room for bufSz - wrEnd bytes. Amount of bytes still/already in buf = wrEnd - rdPos
            // (thus: "n - (wrEnd - rdPos)" amount still to be read, if any; and "n - (wrEnd - rdPos)" == "n + rdPos - wrEnd"
            ssize_t       aRead;
            const ssize_t nRead = std::min(n + rdPos - wrEnd, bufSz - wrEnd);

            ETDCDEBUG(5, "ETDDataServer::pull_n/pulling " << n << " bytes" << std::endl);
        
            // Attempt to read bytes. <0 is an error
            ETDCASSERT((aRead = src->read(src->__m_fd, &buf[wrEnd], nRead))>=0, "Failed to read bytes from client - " << etdc::strerror(errno));
            // Now we can bump wrEnd by that amount [at this point aRead might still be zero]
            wrEnd += aRead;

            // If there are no bytes to write to file that means that 0
            // bytes were read and no bytes still left in buffer == error
            ETDCASSERT((wrEnd - rdPos)>0, "No bytes read from client and no more bytes still left in buffer");

            // Now flush the amount of available bytes to the destination
            ETDCASSERTX(dst->write(dst->__m_fd, &buf[rdPos], wrEnd-rdPos)==ssize_t(wrEnd-rdPos));
            n -= (wrEnd - rdPos);

            // Now we are sure we can use the whole buffer for reading bytes
            // from the client
            wrEnd = rdPos = 0;
        }
        const char ack{ 'y' };
        ETDCDEBUG(5, "ETDDataServer::pull_n/got all bytes, sending ACK " << std::endl);
        src->write(src->__m_fd, &ack, 1);
        ETDCDEBUG(5, "ETDDataServer::pull_n/done." << std::endl);
    }

} // namespace etdc
