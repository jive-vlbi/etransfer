// Implementation file
#include <utilities.h>
#include <etdc_etdserver.h>

// C++ headerts
#include <mutex>
#include <memory>

// Plain-old-C
#include <glob.h>

namespace etdc {
    uuid_type const& get_uuid(result_type const& res) {
        return std::get<0>(res);
    }

    filelist_type ETDServer::listPath(std::string const& path, bool allow_tilde) const {
        struct globiter_t {
            int i;
        };
        // glob() is MT unsafe so we had better make sure only one thread executes this
        //static std::mutex       globMutex;

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
        if( allow_tilde && path.find('~')!=std::string::npos )
            throw std::domain_error("The target O/S does not support the requested tilde expansion");
#endif
        // Make the glob go fast(er) [NOSORT] - we'll do that ourselves when we have all the paths in memory
        //::glob(path.c_str(), GLOB_MARK|/*GLOB_NOSORT|*/, nullptr, files.get());
        ::glob(path.c_str(), globFlags, nullptr, files.get());
        //
        //    std::lock_guard<std::mutex> scopedlock(globMutex);
        //}
        
        // Now that we have the results, we can 
        return filelist_type(&files->gl_pathv[0], &files->gl_pathv[files->gl_pathc]);
    }

    //////////////////////////////////////////////////////////////////////////////////////
    //
    // Attempt to set up resources for writing to a file
    // return a UUID that the client must use to write to the file
    //
    //////////////////////////////////////////////////////////////////////////////////////
    result_type ETDServer::requestFileWrite(std::string const& path, openmode_type mode) {
        const std::string nPath( detail::normalize_path(path) );

        // Attempt to open path new, write or append [reject read!]
        ETDCASSERT(mode==openmode_type::omNew || mode==openmode_type::omWrite || mode==openmode_type::omAppend,
                   "invalid open mode for requestFileWrite(" << path << ")");

        // We must check-and-insert-if-ok into shared state.
        // This has to be atomic, so we'll grab the lock
        // until we're completely done.
        auto&                       shared_state( __m_shared_state.get() );
        std::lock_guard<std::mutex> lk( shared_state.lock );
        auto&                       transfers( shared_state.transfers );

        // Before doing anything - see if this server already has an entry for this (normalized) path -
        // we cannot honour multiple write attempts (not even if it was already open for reading!)
        const auto  pathPresent = (std::find_if(std::begin(transfers), std::end(transfers),
                                                [&](transfermap_type::value_type const& vt) { return vt.second.path==nPath; })
                                   != std::end(transfers));
        ETDCASSERT(pathPresent==false, "requestFileWrite(" << path << ") - the path is already in use");

        // Transform to int argument to open(2) + append some flag(s) if necessary/available
        int  omode = static_cast<int>(mode);

#if O_LARGEFILE
        // set large file if the current system has it
        omode |= O_LARGEFILE;
#endif

        // Note: etdc_file(...) c'tor will create the whole directory tree if necessary.
        //       Because it may/may not have to create, we add the file permission bits
        etdc_fdptr      fd( new etdc_file(nPath, omode, 0644) );
        const off_t     fsize{ fd->lseek(fd->__m_fd, 0, SEEK_END) };
        const uuid_type uuid{ uuid_type::mk() };

        ETDCASSERT(transfers.emplace(uuid, etdc::transferprops_type(fd, nPath, mode)).second,
                   "Failed to insert new entry, request file write '" << path << "'");
        // and return the uuid + alreadyhave
        return result_type(uuid, fsize);
    }

    result_type ETDServer::requestFileRead(std::string const& path, openmode_type mode) {
        const std::string nPath( detail::normalize_path(path) );

        // Attempt to open path, only allow omRead
        ETDCASSERT(mode==openmode_type::omRead, "invalid open mode for requestFileRead(" << path << ")");

        // We must check-and-insert-if-ok into shared state.
        // This has to be atomic, so we'll grab the lock
        // until we're completely done.
        auto&                       shared_state( __m_shared_state.get() );
        std::lock_guard<std::mutex> lk( shared_state.lock );
        auto&                       transfers( shared_state.transfers );

        // Before doing anything - see if this server already has an entry for this (normalized) path -
        // we can only honour this request if it's opened for reading [multiple readers = ok]
        const auto  pathPtr = std::find_if(std::begin(transfers), std::end(transfers),
                                           [&](transfermap_type::value_type const& vt) {
                                                    return vt.second.path==nPath;
                                                });
        ETDCASSERT(pathPtr==std::end(transfers) || pathPtr->second.openMode==openmode_type::omRead,
                   "requestFileRead(" << path << ") - the path is already in use");

        // Transform to int argument to open(2) + append some flag(s) if necessary/available
        int  omode = static_cast<int>(mode);

#if O_LARGEFILE
        // set large file if the current system has it
        omode |= O_LARGEFILE;
#endif

        // Note: etdc_file(...) c'tor will create the whole directory tree if necessary.
        // Because openmode is read, then we don't have to pass the file permissions; either it's there or it isn't
        etdc_fdptr      fd( new etdc_file(nPath, omode) );
        const off_t     fsize{ fd->lseek(fd->__m_fd, 0, SEEK_END) };
        const uuid_type uuid{ uuid_type::mk() };

        ETDCASSERT(transfers.emplace(uuid, etdc::transferprops_type(fd, nPath, mode)).second,
                   "Failed to insert new entry, request file read '" << path << "'");
        // and return the uuid + alreadyhave
        return result_type(uuid, fsize);
    }

} // namespace etdc
