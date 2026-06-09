// Data-channel transport preference parsing + ranking for the etransfer
// client. Kept free of any socket/UDT/SRT dependency (operates purely on
// scheme name strings) so the logic can be unit tested in isolation.
//
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
#ifndef ETDC_CHANNEL_PREF_H
#define ETDC_CHANNEL_PREF_H

#include <etdc_assert.h>

#include <string>
#include <vector>
#include <cstddef>

namespace etdc {
    // Address-family selector for a single data-channel preference token.
    // 'any' is produced by a bare protocol token (e.g. "srt") and matches
    // both the IPv4 and the IPv6 scheme of that protocol.
    enum class pref_family { any, v4, v6 };

    // One parsed preference token: a base protocol ("tcp", "udt", "srt")
    // plus an address-family selector.
    struct pref_token {
        std::string protocol;
        pref_family family;
    };

    // Split a data-channel scheme name as advertised on the wire
    // ("tcp", "tcp6", "udt", "udt6", "srt", "srt6") into its base protocol
    // and address family. The IPv6 schemes carry a trailing '6'; the bare
    // name is the IPv4 scheme.
    inline void split_scheme(std::string const& scheme, std::string& base, pref_family& fam) {
        if( !scheme.empty() && scheme.back()=='6' ) {
            base = scheme.substr(0, scheme.size()-1);
            fam  = pref_family::v6;
        } else {
            base = scheme;
            fam  = pref_family::v4;
        }
    }

    // Does the given preference token match the given wire scheme name?
    // A token with family 'any' matches either family of its protocol.
    inline bool pref_matches(pref_token const& tok, std::string const& scheme) {
        std::string base;
        pref_family fam{ pref_family::v4 };
        split_scheme(scheme, base, fam);
        if( tok.protocol!=base )
            return false;
        return tok.family==pref_family::any || tok.family==fam;
    }

    // Parse a "--prefer-data-channel" specification: a comma-separated list
    // of tokens, each one of {tcp,udt,srt} with an optional '4' or '6'
    // address-family suffix (bare = either family). Throws
    // etdc::assertion_error on any malformed/empty token so the argument
    // parser reports a usage error.
    inline std::vector<pref_token> parse_channel_preference(std::string const& spec) {
        std::vector<pref_token>  result;
        std::string::size_type   pos{ 0 };

        ETDCASSERT(!spec.empty(), "empty data-channel preference");
        while( pos<=spec.size() ) {
            std::string::size_type const comma = spec.find(',', pos);
            std::string const            tok   = spec.substr(pos, comma==std::string::npos ? std::string::npos : comma-pos);
            pos = (comma==std::string::npos ? spec.size()+1 : comma+1);

            ETDCASSERT(!tok.empty(), "empty token in data-channel preference '" << spec << "'");

            // Peel off an optional trailing address-family digit.
            pref_family   fam{ pref_family::any };
            std::string   proto{ tok };
            if( proto.back()=='4' || proto.back()=='6' ) {
                fam   = (proto.back()=='6' ? pref_family::v6 : pref_family::v4);
                proto = proto.substr(0, proto.size()-1);
            }
            ETDCASSERT(proto=="tcp" || proto=="udt" || proto=="srt",
                       "unknown protocol '" << proto << "' in data-channel preference token '" << tok << "'");
            result.push_back( pref_token{ proto, fam } );
        }
        return result;
    }

    // Rank of a wire scheme under the given preference: the index of the
    // first token that matches it, or prefs.size() if none does. A larger
    // rank sorts later, so when used as the key of a stable sort the
    // unmatched schemes retain their original (daemon-advertised) relative
    // order behind the preferred ones.
    inline std::size_t preference_rank(std::vector<pref_token> const& prefs, std::string const& scheme) {
        for(std::size_t i=0; i<prefs.size(); ++i)
            if( pref_matches(prefs[i], scheme) )
                return i;
        return prefs.size();
    }
} // namespace etdc

#endif
