// Implement an ACL mechanism for the daemon
// Copyright (C) 2007-2026 Marjolein Verkouter
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
// Author:  Marjolein Verkouter - verkouter@jive.eu
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#include <etd_acl.h>
#include <etdc_assert.h>

#include <fstream>

namespace etdc {

    //ACL::ACL( std::string const& s):
    //    _m_root = fkyaml::node::
    //{}

    // Our ACL requires a few things.
    // Permissions tested in order, first to match wins.
    // <path> may contain "*" and "?" globbing
    //
    // read:
    //  default:
    //    {deny|allow}: <path>
    //
    //  [allow:
    //  - <path>
    //  - <path>
    //  ...]
    //  [deny:
    //  - <path>
    //  - <path>
    //  ...]
    //
    // write:
    //  default:
    //    {deny|allow}: <path>
    //
    //  [allow:
    //  - ...]
    //  [deny:
    //  - ...]
    //
    void ACL::initFromYAML( void ) {
        ETDCASSERT( _m_root.is_mapping(), "Document root is not a mapping" );

        // We must have a read- and write section, that both are mappings
        ETDCASSERT( _m_root.contains("read") && _m_root.contains("write") &&
                    _m_root["read"].is_mapping() && _m_root["write"].is_mapping(),
                    "The ACL YAML does not have have 'read' and 'write' mappings" );

        // Each node needs:
        // default:
        //    {allow|deny}: <path>
        //
        // And may have:
        // allow:
        // - <path>
        // - ...
        // deny:
        // - <path>
        // - ...

        auto assert_sequence_of_strings = [](fkyaml::node const& n, std::string const& nm) {
            ETDCASSERT(n.is_string(), "Node '" << nm << " is not a sequence");
            for(auto& entry : n) {
                ETDCASSERT(entry.is_string(), "Node '" << nm << " contains non-string entries");
            }
        };

        auto node_ok = [&](fkyaml::node const& n, std::string const& nm) {
            ETDCASSERT(n.contains("default") && n["default"].is_mapping(),
                       "Node '" << nm << "': no 'default:' mapping entry");
            fkyaml::node m_def = n["default"];

            // Must have a string-valued 'allow:' or 'deny:' but not neither or both
            const bool allow = m_def.contains("allow");
            const bool deny  = m_def.contains("deny");

            ETDCASSERT(allow != deny, "Node '" << nm << "': needs exactly one of 'allow:' OR 'deny:'");

            // And the one that exists must be a string
            ETDCASSERT((allow && m_def["allow"].is_string()) || (deny && m_def["deny"].is_string()),
                       "Node '" << nm << "': key '" << (allow? "allow" : "deny") << "' is not a string (path)");

            // *IF* they have 'allow:' and/or 'deny:' they need to be
            // sequences-of-strings
            if( n.contains("allow") )
                assert_sequence_of_strings(n["allow"], nm+" allow:");
            if( n.contains("deny") )
                assert_sequence_of_strings(n["deny"], nm+" deny:");
        };

        node_ok( _m_root["read"],  "read"  );
        node_ok( _m_root["write"], "write" );
    }

    ACL ACL::readFromFile( std::string const& fn ) {
        // this shortcut don't compile because it doesn't
        // see the argument as "std::istream&"?
        //return ACL( std::ifstream(fn) );
        std::ifstream ifs(fn);
        return ACL( ifs );
    }
}
