// name to IP resolving
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
#ifndef ETDC_ETDC_RESOLVE_H
#define ETDC_ETDC_RESOLVE_H

#include <etdc_assert.h>
#include <string>
#include <memory>
#include <stdexcept>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <string.h>   // ::memset()

namespace etdc {
    // https://sourceware.org/bugzilla/show_bug.cgi?id=17082
    //   "htons" is sometimes a macro and sometimes a function, depending on
    //   optimization level in glibc.
    //   Apparently that means that it cannot be used in C++11 lambdas?! FFS!
    inline uint16_t htons_(uint16_t p) {
        return htons(p);
    }
    inline uint16_t ntohs_(uint16_t p) {
        return ntohs(p);
    }
    inline uint32_t htonl_(uint32_t p) {
        return htonl(p);
    }
    inline uint32_t ntohl_(uint32_t p) {
        return ntohl(p);
    }

    namespace detail {
        using addrinfo_ptr = std::shared_ptr<struct addrinfo>;

        // Wrapper around getaddrinfo(3) that either throws up or returns
        // a shared pointer to addrinfo, which gets automagically correctly deleted if no-one
        // refers to it anymore
        inline addrinfo_ptr getaddrinfo(char const* hostname, char const* servname, struct addrinfo const* hints) {
            int              gai_error;
            struct addrinfo* resultptr;
            ETDCSYSCALL( (gai_error=::getaddrinfo(hostname, servname, hints, &resultptr))==0,
                         "::getaddrinfo[\"" << hostname << "\"] says " << ::gai_strerror(gai_error) );
            return addrinfo_ptr(resultptr, ::freeaddrinfo);
        }
    }

    /////////////////////////////////////////////////////////////
    //    Policies about how to interpret an empty host name
    /////////////////////////////////////////////////////////////
    struct EmptyMeansAny {
        bool operator()(struct sockaddr_in& dst) const {
            dst.sin_addr.s_addr = INADDR_ANY;
            return true;
        }
    };
    // According to POSIX, INADDR_NONE {aka: ((uint32_t)-1), "255.255.255.255"}
    // is a valid IPv4 address and as such had better not used as sentinel.
    // In fact, POSIX.1-2008 doesn't define INADDR_NONE no more.
    // In our code we say 'EmptyMeansNone' when we mean to resolve a client's
    // IPv4 address for making a connection to, i.e. if we don't specify a host/ip
    // we should resolve to (an invalid) host address. INADDR_NONE cannot be used to
    // connect to so that should be good enough. But it means that EmptyMeansAny and
    // EmptyMeansNone actually resolve to the same sentinel value ... oh well.
    struct EmptyMeansNone {
        bool operator()(struct sockaddr_in& dst) const {
            dst.sin_addr.s_addr = INADDR_ANY;
            return true;
        }
    };
    struct EmptyMeansInvalid {
        bool operator()(struct sockaddr_in&) const {
            return false;
        }
    };

    //  Resolve a hostname in dotted quad notation or canonical name format
    //  to an IPv4 address. Fills in dst.sin_addr if succesful.
    //  socktype = SOCK_STREAM/SOCK_DGRAM/SOCK_RAW
    //  protocol = IPPROTO_UDP/IPPROTO_TCP
    template <typename EmptyHostPolicy>
    bool resolve_host(std::string const& host, const int socktype, const int protocol, struct sockaddr_in& dst) {
        // Make sure that we're clear about this
        dst.sin_family  = AF_INET;
        if( host.empty() )
            return EmptyHostPolicy()(dst);

        // First try the simple conversion, otherwise we need to do a lookup
        // inet_pton is POSIX and returns 0 or -1 if the string is
        // NOT in 'presentation' format or a system error occurs (which we ignore).
        // Then we fall back to getaddrinfo(3)
        if( ::inet_pton(AF_INET, host.c_str(), &dst.sin_addr)==1 )
            return true;

        // OK. Give getaddrinfo(3) a try
        bool                 found( false );
        struct addrinfo      hints;
        detail::addrinfo_ptr resultptr;

        // Provide some hints to the address resolver about
        // what it is what we're looking for
        ::memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_family   = AF_INET;     // IPv4 only at the moment
        hints.ai_socktype = socktype;    // only the socket type we require
        hints.ai_protocol = protocol;    // Id. for the protocol

        resultptr = detail::getaddrinfo(host.c_str(), 0, &hints);

        // Scan the results for an IPv4 address
        for(auto rp = resultptr.get(); rp!=0 && !found; rp=rp->ai_next )
            if( rp->ai_family==AF_INET )
                dst.sin_addr   = ((struct sockaddr_in const*)rp->ai_addr)->sin_addr, found = true;
        return found;
    }
}
#endif
