// Implementation file
#include <utilities.h>
#include <etdc_etdserver.h>

// C++ headerts
#include <regex>
#include <mutex>
#include <memory>
#include <thread>
#include <functional>

// Plain-old-C
#include <glob.h>
#include <string.h>

namespace etdc {
    uuid_type const& get_uuid(result_type const& res) {
        return std::get<0>(res);
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
        std::unique_ptr<glob_t, void(*)(glob_t*)>  files{new etdc::Zero<glob_t>(),
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
        //ETDCASSERT(mode==openmode_type::New || mode==openmode_type::OverWrite || mode==openmode_type::Resume || mode==,
        ETDCASSERT(allowedModes.find(mode)!=std::end(allowedModes),
                   "invalid open mode for requestFileWrite(" << path << ")");

        // Before doing anything - see if this server already has an entry for this (normalized) path -
        // we cannot honour multiple write attempts (not even if it was already open for reading!)
        const auto  pathPresent = (std::find_if(std::begin(transfers), std::end(transfers),
                                                [&](transfermap_type::value_type const& vt) { return vt.second.path==nPath; })
                                   != std::end(transfers));
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
        etdc_fdptr      fd( new etdc_file(nPath, omode, 0644) );
        const off_t     fsize{ fd->lseek(fd->__m_fd, 0, SEEK_END) };
        //const uuid_type uuid{ uuid_type::mk() };

        ETDCASSERT(transfers.emplace(__m_uuid, etdc::transferprops_type(fd, nPath, mode)).second,
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
        ETDCASSERT(pathPtr==std::end(transfers) || pathPtr->second.openMode==openmode_type::Read,
                   "requestFileRead(" << path << ") - the path is already in use");

        // Transform to int argument to open(2) + append some flag(s) if necessary/available
        int  omode = static_cast<int>(etdc::openmode_type::Read);

#if O_LARGEFILE
        // set large file if the current system has it
        omode |= O_LARGEFILE;
#endif

        // Note: etdc_file(...) c'tor will create the whole directory tree if necessary.
        // Because openmode is read, then we don't have to pass the file permissions; either it's there or it isn't
        etdc_fdptr      fd( new etdc_file(nPath, omode) );
        const off_t     sz{ fd->lseek(fd->__m_fd, 0, SEEK_END) };
        //const uuid_type uuid{ uuid_type::mk() };

        // Assert that we can seek to the requested position
        ETDCASSERT(fd->lseek(fd->__m_fd, alreadyhave, SEEK_SET)!=static_cast<off_t>(-1),
                   "Cannot seek to position " << alreadyhave << " in file " << path << " - " << etdc::strerror(errno));

        auto insres = transfers.emplace(__m_uuid/*uuid_type::mk()*/, etdc::transferprops_type(fd, nPath, openmode_type::Read));
        ETDCASSERT(insres.second, "Failed to insert new entry, request file read '" << path << "'");
        //ETDCASSERT(transfers.emplace(uuid, etdc::transferprops_type(fd, nPath, mode)).second,
        //           "Failed to insert new entry, request file read '" << path << "'");
        // and return the uuid + alreadyhave
        // insres = pair<iterator, bool>
        //  => insres.first = iterator-to-std::map<uuid, transferprops>::value_type
        //  => value_type   = pair<uuid, transferprops>
        // thus insres.first->first == uuid!
        //return result_type(insres.first->first, sz-alreadyhave);
        return result_type(__m_uuid, sz-alreadyhave);
        //return result_type(uuid, fsize);
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
        etdc::etd_state&                 shared_state( __m_shared_state.get() );
        while( true ) {
            std::unique_ptr<std::mutex>      transfer_lock; // might hold ptr-to-mutex that was in the transfer, if any
            // 1. lock shared state
            std::unique_lock<std::mutex>     lk( shared_state.lock );
            // 2. find if there is an entry in the map for us
            etdc::transfermap_type::iterator ptr = shared_state.transfers.find(__m_uuid);
            
            // No? OK then we're done
            if( ptr==shared_state.transfers.end() )
                return false;

            // Now we must do try_lock on the transfer - if that fails we sleep and start from the beginning
            std::unique_lock<std::mutex>     sh( *ptr->second.lockPtr, std::try_to_lock );
            if( !sh ) {
                // sleep for a bit ...
                std::this_thread::sleep_for( std::chrono::microseconds(42) );
                // and by going out of scope and back into it again, the mutex is unlocked + re-acquired
                continue;
            }
            // Right, we now hold both locks!
            transferprops_type&  transfer( ptr->second );
            transfer.fd->close(transfer.fd->__m_fd);
            // We cannot erase the transfer immediately: we hold the lock that is contained in it
            // so what we do is transfer the lock out of the transfer and /then/ erase the entry.
            // And when we finally return, then the lock will be unlocked and the unique pointer
            // deleted
            transfer_lock = std::move(transfer.lockPtr);
            // OK lock is now moved out of the transfer, so now it's safe to erase the entry
            shared_state.transfers.erase( ptr );
            break;
        }
        return true;
    }

    bool ETDServer::sendFile(uuid_type const& srcUUID, uuid_type const& dstUUID, 
                             off_t todo, dataaddrlist_type const& dataAddrs) {
        // 1a. Verify that the srcUUID is our UUID
        ETDCASSERT(srcUUID!=__m_uuid, "The srcUUID '" << srcUUID << "' is not our UUID");

        // We need to protect our transfer so we need to do deadlock avoidance
        // with re-searching our UUID until we have both locks
        etdc::etd_state&                 shared_state( __m_shared_state.get() );

        // Make it loop until all bytes are transferred
        while( todo>0 ) {
            // 2a. lock shared state
            std::unique_lock<std::mutex>     lk( shared_state.lock );
            // 2b. assert that there is an entry for us, indicating that we ARE configured
            etdc::transfermap_type::iterator ptr = shared_state.transfers.find(__m_uuid);

            ETDCASSERT(ptr!=shared_state.transfers.end(), "This server was not initialized yet");
            
            // Now we must do try_lock on the transfer - if that fails we sleep and start from the beginning
            std::unique_lock<std::mutex>     sh( *ptr->second.lockPtr, std::try_to_lock );
            if( !sh ) {
                // sleep for a bit ...
                std::this_thread::sleep_for( std::chrono::microseconds(19) );
                // and by going out of scope and back into it again, the mutex is unlocked + re-acquired
                continue;
            }
            // Right, we now hold both locks!
            // At this point we don't need the shared_state lock anymore - we've found our entry and we've locked it
            // So no-one can remove the entry from under us until we're done
            lk.unlock();

            // Verify that indeed we are configured for file read
            transferprops_type&  transfer( ptr->second );

            ETDCASSERT(transfer.openMode==openmode_type::Read, "This server was initialized, but not for reading a file");

            // Great. Now we attempt to connect to the remote end
            etdc::etdc_fdptr    dstFD;
            std::ostringstream  tried;

            for(auto addr: dataAddrs) {
                try {
                    dstFD = mk_client(get_protocol(addr), get_host(addr), get_port(addr));
                    break;
                }
                catch( std::exception const& e ) {
                    tried << addr << ": " << e.what() << ", ";
                }
                catch( ... ) {
                    tried << addr << ": unknown exception" << ", ";
                }
            }
            ETDCASSERT(dstFD, "Failed to connect to any of the data servers: " << tried.str());

            // Weehee! we're connected!
            const size_t                     bufSz( 2*1024*1024 );
            std::unique_ptr<unsigned char[]> buffer(new unsigned char[bufSz]);

            // Create message header
            std::ostringstream  msg_buf;
            msg_buf << "{ uuid:" << dstUUID << ", sz:" << todo << "}";

            const std::string   msg( msg_buf.str() );
            dstFD->write(dstFD->__m_fd, msg.data(), msg.size());

            while( todo>0 ) {
                const size_t  n = std::min((size_t)todo, bufSz);
                ETDCASSERTX(transfer.fd->read(transfer.fd->__m_fd, &buffer[0], n)==(ssize_t)n);
                ETDCASSERTX(dstFD->write(dstFD->__m_fd, &buffer[0], n)==(ssize_t)n);
                todo -= (off_t)n;
            }
            // if we make it out of the loop, todo should be <= 0 and terminate the outer loop
        }
        return true;
    }

    bool ETDServer::getFile(uuid_type const& srcUUID, uuid_type const& dstUUID, 
                            off_t todo, dataaddrlist_type const& dataAddrs) {
        // 1a. Verify that the dstUUID is our UUID
        ETDCASSERT(dstUUID!=__m_uuid, "The dstUUID '" << dstUUID << "' is not our UUID");

        // We need to protect our transfer so we need to do deadlock avoidance
        // with re-searching our UUID until we have both locks
        etdc::etd_state&                 shared_state( __m_shared_state.get() );

        // Make it loop until all bytes are transferred
        while( todo>0 ) {
            // 2a. lock shared state
            std::unique_lock<std::mutex>     lk( shared_state.lock );
            // 2b. assert that there is an entry for us, indicating that we ARE configured
            etdc::transfermap_type::iterator ptr = shared_state.transfers.find(__m_uuid);

            ETDCASSERT(ptr!=shared_state.transfers.end(), "This server was not initialized yet");
            
            // Now we must do try_lock on the transfer - if that fails we sleep and start from the beginning
            std::unique_lock<std::mutex>     sh( *ptr->second.lockPtr, std::try_to_lock );
            if( !sh ) {
                // sleep for a bit ...
                std::this_thread::sleep_for( std::chrono::microseconds(23) );
                // and by going out of scope and back into it again, the mutex is unlocked + re-acquired
                continue;
            }
            // Right, we now hold both locks!
            // At this point we don't need the shared_state lock anymore - we've found our entry and we've locked it
            // So no-one can remove the entry from under us until we're done
            lk.unlock();

            // Verify that indeed we are configured for file write
            // Note that we do NOT include 'skip existing' in here - the
            // point is that we don't want to write to such a file!
            transferprops_type&  transfer( ptr->second );
            static const std::set<etdc::openmode_type>  allowedWriteModes{ openmode_type::OverWrite, openmode_type::New, openmode_type::Resume };

            ETDCASSERT(allowedWriteModes.find(transfer.openMode)!=allowedWriteModes.end(),
                       "This server was initialized, but not for writing to file");

            // Great. Now we attempt to connect to the remote end
            etdc::etdc_fdptr    dstFD;
            std::ostringstream  tried;

            for(auto addr: dataAddrs) {
                try {
                    dstFD = mk_client(get_protocol(addr), get_host(addr), get_port(addr));
                    break;
                }
                catch( std::exception const& e ) {
                    tried << addr << ": " << e.what() << ", ";
                }
                catch( ... ) {
                    tried << addr << ": unknown exception" << ", ";
                }
            }
            ETDCASSERT(dstFD, "Failed to connect to any of the data servers: " << tried.str());

            // Weehee! we're connected!
            const size_t                     bufSz( 2*1024*1024 );
            std::unique_ptr<unsigned char[]> buffer(new unsigned char[bufSz]);

            // Create message header
            std::ostringstream  msg_buf;
            msg_buf << "{ uuid:" << srcUUID << ", push:1, sz:" << todo << "}";

            const std::string   msg( msg_buf.str() );
            dstFD->write(dstFD->__m_fd, msg.data(), msg.size());

            while( todo>0 ) {
                // Read at most bufSz bytes
                const ssize_t n = dstFD->read(dstFD->__m_fd, &buffer[0], bufSz);
                if( n>0 ) {
                    ETDCASSERTX(transfer.fd->write(transfer.fd->__m_fd, &buffer[0], n)==n);
                    todo -= (off_t)n;
                }
            }
            // if we make it out of the loop, todo should be <= 0 and terminate the outer loop
        }
        return true;
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
    static const std::regex            rxLine("[^\\n]+\\n");
    static const std::regex            rxReply("^(OK|ERR)(\\s+(\\S.*)?)?$", etdc_rxFlags);
                                             //  1       2    3   submatch numbers
    static const std::sregex_iterator  srxIterEnd;
    static const std::cregex_iterator  crxIterEnd;

    static std::string                 strip(std::string const& s) {
        static std::regex  rxNL("\\n");
        return std::regex_replace(s, rxNL, "");
    }

    filelist_type ETDProxy::listPath(std::string const& path, bool) const {
        std::ostringstream   msgBuf;

        msgBuf << "list " << path << '\n';
        const std::string  msg( msgBuf.str() );

        ETDCDEBUG(3, "ETDProxy::listPath/sending message '" << msg << "'" << std::endl);
        ETDCASSERTX(__m_connection->write(__m_connection->__m_fd, msg.data(), msg.size())==(ssize_t)msg.size());

        // And await the reply
        const size_t            bufSz( 16384 );
        std::unique_ptr<char[]> buffer(new char[bufSz]);

        bool          finished{ false };
        size_t        curPos{ 0 };
        std::string   state;
        filelist_type rv;

        while( !finished && curPos<bufSz ) {
            ETDCDEBUG(3, "ETDProxy::listPath / start loop, curPos=" << curPos << std::endl);
            const ssize_t n = __m_connection->read(__m_connection->__m_fd, &buffer[curPos], bufSz-curPos);
            ETDCDEBUG(3, "ETDProxy::listPath / read n=" << n << " => nTotal=" << n+curPos << std::endl);

            // did we read anything?
            ETDCASSERT(n>0, "Failed to read data from remote end");
            curPos += n;

            // Parse the reply so far
            std::smatch::size_type endpos = 0;
            for(std::cregex_iterator line = std::cregex_iterator(&buffer[0], &buffer[curPos], rxLine); line!=crxIterEnd; line++) {
                // Got a line! Assert that it conforms to our expectation
                std::smatch       fields;
                const std::string lineS( strip(line->str()) );
                ETDCDEBUG(3, "listPath/reply from server: '" << lineS << "'" << std::endl);
                ETDCASSERT(std::regex_match(lineS, fields, rxReply), "Server replied with an invalid line");

                // error code must be either == current state (all lines starting with OK)
                // or state.empty && error code = ERR; we cannot have OK, OK, OK, ERR -> it's either ERR or OK, OK, OK, ... OK
                ETDCASSERT(state.empty() || (state=="OK" && fields[1].str()==state),
                           "The server changed its mind about the success of the call in the middle of the reply");
                state  = fields[1].str();
                endpos = line->position() + line->length();

                const std::string   info( strip(fields[3].str()) ); 

                // Translate error into an exception
                if( state=="ERR" )
                    throw std::runtime_error(std::string("listPath(")+path+") failed - " + (info.empty() ? "<unknown reason>" : info));

                // This is the end-of-reply sentinel: a single OK by itself
                if( (finished=(state=="OK" && info.empty()))==true )
                    break;
                //if( state=="OK" && info.empty() )
                //    break;
                // Append the entry to the list of paths
                rv.push_back( info );
            } 
            // Processed all lines in the reply so far.
            // So we move all processed bytes to begin of buffer
            ::memmove(&buffer[0], &buffer[endpos], curPos - endpos);
            curPos -= endpos;
        }
        return rv;
    }

    //////////////////////////////////////////////////////////////////////
    //
    // This class does NOT implementing the ETDServerInterface but
    // takes a connection, instantiates its own ETDServer
    // and then loops, reading commands from the connection and sends
    // back replies
    //
    //////////////////////////////////////////////////////////////////////
    static const std::regex         rxList("^list\\s+(\\S.*)$", etdc_rxFlags);

    void ETDServerWrapper::handle( void ) {
        // here we enter our while loop, reading commands and (attempt) to
        // interpret them
        const size_t            bufSz( 2*1024*1024 );
        std::unique_ptr<char[]> buffer(new char[bufSz]);

        size_t        curPos = 0;
        std::string   state;
        filelist_type rv;

        while( curPos<bufSz ) {
            ETDCDEBUG(2, "ETDServerWrapper::handle() / start loop, curPos=" << curPos << std::endl);
            const ssize_t n = __m_connection->read(__m_connection->__m_fd, &buffer[curPos], bufSz-curPos);
            ETDCDEBUG(2, "ETDServerWrapper::handle() / read n=" << n << " => nTotal=" << n + curPos << std::endl);
            // did we read anything?
            ETDCASSERT(n>0, "Failed to read data from remote end");
            curPos += n;

            // Parse the reply so far
            std::smatch::size_type endpos = 0;
            for(std::cregex_iterator line = std::cregex_iterator(&buffer[0], &buffer[curPos], rxLine); line!=crxIterEnd; line++) {
                // Got a line! Assert that it conforms to our expectation
                const std::string lineS( strip(line->str()) );
                ETDCDEBUG(2, "ETDServerWrapper::handle()/got line: '" << lineS << "'" << std::endl);

                // Match it against the known commands
                std::smatch              fields;
                std::vector<std::string> replies;

                try {
                    if( std::regex_match(lineS, fields, rxList) ) {
                        // we're a remote ETDServer (seen from the client)
                        // so we do not support ~ expansion
                        const auto entries = __m_etdserver.listPath(fields[1].str(), false);
                        std::transform(std::begin(entries), std::end(entries), std::back_inserter(replies),
                                       std::bind(std::plus<std::string>(), std::string("OK "), std::placeholders::_1));
                        // and add a final OK
                        replies.emplace_back("OK");
                    }
                }
                catch( std::exception const& e ) {
                    replies.emplace_back( std::string("ERR ")+e.what() );
                }
                catch( ... ) {
                    replies.emplace_back( "FAIL Unknown exception" );
                }

                // Now send back the replies
                for(auto const& r: replies) {
                    __m_connection->write(__m_connection->__m_fd, r.data(), r.size());
                    __m_connection->write(__m_connection->__m_fd, "\n", 1);
                }
            } 
            // Processed all lines in the reply so far.
            // So we move all processed bytes to begin of buffer
            ::memmove(&buffer[0], &buffer[endpos], curPos - endpos);
            curPos -= endpos;
        }
    }
} // namespace etdc
